#include "vk_raster.h"
#include "vk_core.h"

#include "vk_render.h"
#include "vk_geometry.h"
#include "vk_cvar.h"
#include "vk_math.h"
#include "vk_textures.h"

#include "vulkan/VBarrier.h"
#include "vulkan/VDescriptor.h"
#include "vulkan/VPipeline.h"
#include "vulkan/VResource.h"

#include "xash3d_mathlib.h"
#include "xash3d_types.h"
#include "protocol.h" // MAX_DLIGHTS

#define MODULE_NAME "raster"

#define MAX_UNIFORM_SLOTS (MAX_SCENE_ENTITIES * 2 /* solid + trans */ + 1)

typedef struct {
	matrix4x4 mvp;
	vec4_t color;
} uniform_data_t;

typedef struct {
	matrix4x4 mvp;
	matrix4x4 inv_proj;
	matrix4x4 inv_view;
	vec2_t resolution;
	float pad_[2];
} sky_uniform_data_t;

enum {
	// These correspond to kVkRenderType*
	kVkPipeline_Solid,    // no blending, depth RW
	kVkPipeline_A_1mA_RW, // blend: src*a + dst*(1-a), depth: RW
	kVkPipeline_A_1mA_R,  // blend: src*a + dst*(1-a), depth test
	kVkPipeline_A_1,      // blend: src*a + dst, no depth test or write
	kVkPipeline_A_1_R,    // blend: src*a + dst, depth test
	kVkPipeline_AT,       // no blend, depth RW, alpha test
	kVkPipeline_1_1_R,    // blend: src + dst, depth test

	kVkPipeline_COUNT,
};

typedef struct {
	VkPipeline pipeline;
#define MAX_CONCURRENT_FRAMES 2
	VkDescriptorSet sets[MAX_CONCURRENT_FRAMES];
	VkDescriptorSetLayoutBinding bindings[2];
	vk_descriptor_value_t values[2];
	vk_descriptors_t descs;
} r_pipeline_sky_t;

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kVkPipeline_COUNT];

	r_pipeline_sky_t pipeline_sky;

	vk_resource_buffer_t *geometry;

	vk_buffer_t uniform_buffer;
	uint32_t ubo_align;

	cvar_t *use_material_textures;

	struct {
		int dynamic_model_count;
		int models_count;
	} stats;
} g_raster;

typedef struct {
	uint32_t num_lights;
	uint32_t debug_r_lightmap;
	uint32_t padding_[2];
	struct {
		vec4_t pos_r;
		vec4_t color;
	} light[MAX_DLIGHTS];
} vk_ubo_lights_t;

#define MAX_DRAW_COMMANDS 8192 // TODO estimate
#define MAX_DEBUG_NAME_LENGTH 32

typedef struct render_draw_s {
	uint32_t ubo_offset; // FIXME move this to draw
	int lightmap, texture;
	int pipeline_index;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
} render_draw_t;

typedef struct render_draw_sky_s {
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
	// TODO matrix4x4 model;
} render_draw_sky_t;

enum draw_command_type_e {
	DrawLabelBegin,
	DrawLabelEnd,
	DrawDraw,
	DrawSky,
};

typedef struct {
	enum draw_command_type_e type;
	union {
		char debug_label[MAX_DEBUG_NAME_LENGTH];
		render_draw_t draw;
		render_draw_sky_t draw_sky;
	};
} draw_command_t;

static struct {
	int uniform_data_set_mask;
	uniform_data_t current_uniform_data;
	uniform_data_t dirty_uniform_data;

	r_flipping_buffer_t uniform_alloc;
	uint32_t current_ubo_offset_FIXME;

	draw_command_t draw_commands[MAX_DRAW_COMMANDS];
	int num_draw_commands;
} g_render_state;

enum {
	UNIFORM_UNSET = 0,
	UNIFORM_UPLOADED = 16,
};

static qboolean createPipeline( VkPipeline* out, const char *name, const vk_pipeline_graphics_create_info_t *ci ) {
	*out = VK_PipelineGraphicsCreate(ci);

	if (*out == VK_NULL_HANDLE)
	{
		gEngine.Con_Printf(S_ERROR "Cannot create render pipeline \"%s\"\n", name);
		return false;
	}

	if (vk_core.debug)
	{
		VkDebugUtilsObjectNameInfoEXT debug_name = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.objectHandle = (uint64_t)*out,
			.objectType = VK_OBJECT_TYPE_PIPELINE,
			.pObjectName = name,
		};
		XVK_CHECK(vkSetDebugUtilsObjectNameEXT(vk_core.device, &debug_name));
	}

	return true;
}

static qboolean createSkyboxPipeline( void ) {
	const vk_shader_stage_t sky_shaders[] = {
	{
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filename = "sky.vert.spv",
		.specialization_info = NULL,
	}, {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filename = "sky.frag.spv",
		.specialization_info = NULL,
	}};

	const VkVertexInputAttributeDescription attribs[] = {
		{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, pos)},
	};

	g_raster.pipeline_sky.bindings[0] = (VkDescriptorSetLayoutBinding){
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.pImmutableSamplers = NULL,
	};
	g_raster.pipeline_sky.bindings[1] = (VkDescriptorSetLayoutBinding) {
    .binding = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.pImmutableSamplers = NULL,
	};

	g_raster.pipeline_sky.descs = (vk_descriptors_t){
		.num_bindings = COUNTOF(g_raster.pipeline_sky.bindings),
		.bindings = g_raster.pipeline_sky.bindings,

		.values = g_raster.pipeline_sky.values,

		.push_constants = (VkPushConstantRange){0},

		.num_sets = COUNTOF(g_raster.pipeline_sky.sets),
		.desc_sets = g_raster.pipeline_sky.sets,
	};

	VK_DescriptorsCreate(&g_raster.pipeline_sky.descs);

	vk_pipeline_graphics_create_info_t ci = {
		.layout = g_raster.pipeline_sky.descs.pipeline_layout,

		.attribs = attribs,
		.num_attribs = COUNTOF(attribs),

		.stages = sky_shaders,
		.num_stages = COUNTOF(sky_shaders),

		.vertex_stride = sizeof(vk_vertex_t),

		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,

		.blendEnable = VK_FALSE,

		.cullMode = VK_CULL_MODE_FRONT_BIT,
	};

	return createPipeline(&g_raster.pipeline_sky.pipeline, "sky", &ci);
}

static qboolean createPipelines( void )
{
	VkDescriptorSetLayout descriptor_layouts[] = {
		vk_desc_fixme.one_uniform_buffer_layout,
		vk_desc_fixme.one_texture_layout,
		vk_desc_fixme.one_texture_layout,
		vk_desc_fixme.one_uniform_buffer_layout,
	};

	VkPipelineLayoutCreateInfo plci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = COUNTOF(descriptor_layouts),
		.pSetLayouts = descriptor_layouts,
	};

	// FIXME store layout separately
	XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &g_raster.pipeline_layout));

	{
		struct ShaderSpec {
			float alpha_test_threshold;
			uint32_t max_dlights;
		} spec_data = { .25f, MAX_DLIGHTS };
		const VkSpecializationMapEntry spec_map[] = {
			{.constantID = 0, .offset = offsetof(struct ShaderSpec, alpha_test_threshold), .size = sizeof(float) },
			{.constantID = 1, .offset = offsetof(struct ShaderSpec, max_dlights), .size = sizeof(uint32_t) },
		};

		VkSpecializationInfo shader_spec = {
			.mapEntryCount = COUNTOF(spec_map),
			.pMapEntries = spec_map,
			.dataSize = sizeof(struct ShaderSpec),
			.pData = &spec_data
		};

		const VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, pos)},
			{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, normal)},
			{.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, gl_tc)},
			{.binding = 0, .location = 3, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, lm_tc)},
			{.binding = 0, .location = 4, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(vk_vertex_t, color)},
			// Not used {.binding = 0, .location = 6, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, prev_pos)},
		};

		const vk_shader_stage_t shader_stages[] = {
		{
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.filename = "brush.vert.spv",
			.specialization_info = NULL,
		}, {
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.filename = "brush.frag.spv",
			.specialization_info = &shader_spec,
		}};

		vk_pipeline_graphics_create_info_t ci = {
			.layout = g_raster.pipeline_layout,
			.attribs = attribs,
			.num_attribs = COUNTOF(attribs),

			.stages = shader_stages,
			.num_stages = COUNTOF(shader_stages),

			.vertex_stride = sizeof(vk_vertex_t),

			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS,

			.blendEnable = VK_FALSE,

			.cullMode = VK_CULL_MODE_FRONT_BIT,
		};

		{
			spec_data.alpha_test_threshold = 0.f;
			ci.blendEnable = VK_FALSE;
			ci.depthWriteEnable = VK_TRUE;
			ci.depthTestEnable = VK_TRUE;
			if (!createPipeline(g_raster.pipelines + kVkPipeline_Solid, "solid", &ci))
				return false;
		}

		{
			spec_data.alpha_test_threshold = 0.f;
			ci.depthWriteEnable = VK_TRUE;
			ci.depthTestEnable = VK_TRUE;
			ci.blendEnable = VK_TRUE;
			ci.colorBlendOp = VK_BLEND_OP_ADD;
			ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			if (!createPipeline(g_raster.pipelines + kVkPipeline_A_1mA_RW, "A_1ma_RW", &ci))
				return false;
		}

		{
			spec_data.alpha_test_threshold = 0.f;
			ci.depthWriteEnable = VK_FALSE;
			ci.depthTestEnable = VK_TRUE;
			ci.blendEnable = VK_TRUE;
			ci.colorBlendOp = VK_BLEND_OP_ADD;
			ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			if (!createPipeline(g_raster.pipelines + kVkPipeline_A_1mA_R, "A_1ma_R", &ci))
				return false;
		}

		{
			spec_data.alpha_test_threshold = 0.f;
			ci.depthWriteEnable = VK_FALSE;
			ci.depthTestEnable = VK_FALSE; // Fake bloom, should be over geometry too
			ci.blendEnable = VK_TRUE;
			ci.colorBlendOp = VK_BLEND_OP_ADD;
			ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			if (!createPipeline(g_raster.pipelines + kVkPipeline_A_1, "A_1", &ci))
				return false;
		}

		{
			spec_data.alpha_test_threshold = 0.f;
			ci.depthWriteEnable = VK_FALSE;
			ci.depthTestEnable = VK_TRUE;
			ci.blendEnable = VK_TRUE;
			ci.colorBlendOp = VK_BLEND_OP_ADD;
			ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			if (!createPipeline(g_raster.pipelines + kVkPipeline_A_1_R, "A_1_R", &ci))
				return false;
		}

		{
			spec_data.alpha_test_threshold = .25f;
			ci.depthWriteEnable = VK_TRUE;
			ci.depthTestEnable = VK_TRUE;
			ci.blendEnable = VK_FALSE;
			if (!createPipeline(g_raster.pipelines + kVkPipeline_AT, "AT", &ci))
				return false;
		}

		{
			spec_data.alpha_test_threshold = 0.f;
			ci.depthWriteEnable = VK_FALSE;
			ci.depthTestEnable = VK_TRUE;
			ci.blendEnable = VK_TRUE;
			ci.colorBlendOp = VK_BLEND_OP_ADD;
			ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			if (!createPipeline(g_raster.pipelines + kVkPipeline_1_1_R, "1_1_R", &ci))
				return false;
		}
	}

	if (!createSkyboxPipeline())
		return false;

	return true;
}

qboolean R_VkRasterInit( void ) {
	g_raster.use_material_textures = gEngine.Cvar_Get( "vk_use_material_textures", "0", FCVAR_GLCONFIG, "Use PBR material textures for traditional rendering too" );

	g_raster.ubo_align = Q_max(4, v_device_info.properties.limits.minUniformBufferOffsetAlignment);

	const uint32_t uniform_unit_size = ((sizeof(uniform_data_t) + g_raster.ubo_align - 1) / g_raster.ubo_align) * g_raster.ubo_align;
	const uint32_t uniform_buffer_size = uniform_unit_size * MAX_UNIFORM_SLOTS;
	R_FlippingBuffer_Init(&g_render_state.uniform_alloc, uniform_buffer_size);

	if (!VK_BufferCreate("render uniform_buffer", &g_raster.uniform_buffer, uniform_buffer_size,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | (vk_core.rtx ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0)))
		return false;

	{
		VkDescriptorBufferInfo dbi_uniform_data = {
			.buffer = g_raster.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(uniform_data_t),
		};
		VkDescriptorBufferInfo dbi_uniform_lights = {
			.buffer = g_raster.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(vk_ubo_lights_t),
		};
		VkWriteDescriptorSet wds[] = {{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &dbi_uniform_data,
				.dstSet = vk_desc_fixme.ubo_sets[0], // FIXME
			}, {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &dbi_uniform_lights,
				.dstSet = vk_desc_fixme.ubo_sets[1], // FIXME
			}};
		vkUpdateDescriptorSets(vk_core.device, COUNTOF(wds), wds, 0, NULL);
	}

	if (!createPipelines())
		return false;

	// TODO type safety
	g_raster.geometry = (void*)R_VkResourceFindByName("geometry");
	ASSERT(g_raster.geometry);

	return true;
}

void R_VkRasterShutdown( void )
{
	for (int i = 0; i < COUNTOF(g_raster.pipelines); ++i)
		vkDestroyPipeline(vk_core.device, g_raster.pipelines[i], NULL);
	vkDestroyPipelineLayout( vk_core.device, g_raster.pipeline_layout, NULL );

	vkDestroyPipeline(vk_core.device, g_raster.pipeline_sky.pipeline, NULL);
	VK_DescriptorsDestroy(&g_raster.pipeline_sky.descs);

	VK_BufferDestroy( &g_raster.uniform_buffer );
}

void R_VkRasterBeginFrame(void) {
	g_render_state.uniform_data_set_mask = UNIFORM_UNSET;
	g_render_state.current_ubo_offset_FIXME = UINT32_MAX;
	memset(&g_render_state.current_uniform_data, 0, sizeof(g_render_state.current_uniform_data));
	memset(&g_render_state.dirty_uniform_data, 0, sizeof(g_render_state.dirty_uniform_data));
	R_FlippingBuffer_Flip(&g_render_state.uniform_alloc);

	g_render_state.num_draw_commands = 0;
}

static uint32_t allocUniform( uint32_t size, uint32_t alignment ) {
	// FIXME Q_max is not correct, we need NAIMENSCHEEE OBSCHEEE KRATNOE
	const uint32_t align = Q_max(alignment, g_raster.ubo_align);
	const uint32_t offset = R_FlippingBuffer_Alloc(&g_render_state.uniform_alloc, size, align);
	return offset;
}

static draw_command_t *drawCmdAlloc( void ) {
	ASSERT(g_render_state.num_draw_commands < COUNTOF(g_render_state.draw_commands));
	return g_render_state.draw_commands + (g_render_state.num_draw_commands++);
}

static void drawCmdPushDebugLabelBegin( const char *debug_label ) {
	if (vk_core.debug) {
		draw_command_t *draw_command = drawCmdAlloc();
		draw_command->type = DrawLabelBegin;
		Q_strncpy(draw_command->debug_label, debug_label, sizeof draw_command->debug_label);
	}
}

static void drawCmdPushDebugLabelEnd( void ) {
	if (vk_core.debug) {
		draw_command_t *draw_command = drawCmdAlloc();
		draw_command->type = DrawLabelEnd;
	}
}

// FIXME get rid of this garbage
static uint32_t getUboOffset_FIXME( void ) {
	// Figure out whether we need to update UBO data, and upload new data if we do
	// TODO generally it's not safe to do memcmp for structures comparison
	if (g_render_state.current_ubo_offset_FIXME == UINT32_MAX
		|| ((g_render_state.uniform_data_set_mask & UNIFORM_UPLOADED) == 0)
		|| memcmp(&g_render_state.current_uniform_data, &g_render_state.dirty_uniform_data, sizeof(g_render_state.current_uniform_data)) != 0) {
		g_render_state.current_ubo_offset_FIXME = allocUniform(sizeof(uniform_data_t), 16 /* why 16? vec4? */);

		if (g_render_state.current_ubo_offset_FIXME == ALO_ALLOC_FAILED)
			return UINT32_MAX;

		uniform_data_t *const ubo = PTR_CAST(uniform_data_t, (byte*)g_raster.uniform_buffer.mapped + g_render_state.current_ubo_offset_FIXME);
		memcpy(&g_render_state.current_uniform_data, &g_render_state.dirty_uniform_data, sizeof(g_render_state.dirty_uniform_data));
		memcpy(ubo, &g_render_state.current_uniform_data, sizeof(*ubo));
		g_render_state.uniform_data_set_mask |= UNIFORM_UPLOADED;
	}

	return g_render_state.current_ubo_offset_FIXME;
}

static void drawCmdPushDraw( const render_draw_t *draw )
{
	draw_command_t *draw_command;

	ASSERT(draw->pipeline_index >= 0);
	ASSERT(draw->pipeline_index < COUNTOF(g_raster.pipelines));
	ASSERT(draw->lightmap >= 0);
	ASSERT(draw->texture >= 0);
	ASSERT(draw->texture < MAX_TEXTURES);

	if (g_render_state.num_draw_commands >= COUNTOF(g_render_state.draw_commands)) {
		gEngine.Con_Printf( S_ERROR "Maximum number of draw commands reached\n" );
		return;
	}

	const uint32_t ubo_offset = getUboOffset_FIXME();
	if (ubo_offset == ALO_ALLOC_FAILED) {
		// TODO stagger this
		gEngine.Con_Printf( S_ERROR "Ran out of uniform slots\n" );
		return;
	}

	draw_command = drawCmdAlloc();
	draw_command->draw = *draw;
	draw_command->draw.ubo_offset = ubo_offset;
	draw_command->type = DrawDraw;
}

static void drawCmdPushDrawSky( const render_draw_sky_t *draw_sky )
{
	draw_command_t *draw_command;

	if (g_render_state.num_draw_commands >= COUNTOF(g_render_state.draw_commands)) {
		gEngine.Con_Printf( S_ERROR "Maximum number of draw commands reached\n" );
		return;
	}

	draw_command = drawCmdAlloc();
	draw_command->draw_sky = *draw_sky;
	draw_command->type = DrawSky;
}

// Return offset of dlights data into UBO buffer
static uint32_t writeDlightsToUBO( void )
{
	vk_ubo_lights_t* ubo_lights;
	int num_lights = 0;
	const uint32_t ubo_lights_offset = allocUniform(sizeof(*ubo_lights), 4);
	if (ubo_lights_offset == UINT32_MAX) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate UBO for DLights\n");
		return UINT32_MAX;
	}
	ubo_lights = PTR_CAST(vk_ubo_lights_t, (byte*)(g_raster.uniform_buffer.mapped) + ubo_lights_offset);

	// TODO this should not be here (where? vk_scene?)
	for (int i = 0; i < MAX_DLIGHTS && num_lights < COUNTOF(ubo_lights->light); ++i) {
		const dlight_t *l = globals.dlights + i;
		if( !l || l->die < gp_cl->time || !l->radius )
			continue;
		Vector4Set(
			ubo_lights->light[num_lights].color,
			l->color.r / 255.f,
			l->color.g / 255.f,
			l->color.b / 255.f,
			1.f);
		Vector4Set(
			ubo_lights->light[num_lights].pos_r,
			l->origin[0],
			l->origin[1],
			l->origin[2],
			l->radius);

		num_lights++;
	}

	ubo_lights->num_lights = num_lights;
	ubo_lights->debug_r_lightmap = r_lightmap->value != 0;
	return ubo_lights_offset;
}

// Needed to avoid VUID-vkCmdCopyBuffer-renderpass
void R_VkRasterPrepareFrame( struct vk_combuf_s* combuf, const FrameContext *ctx ) {
	R_VkResourceProduce(&g_raster.geometry->header, combuf, ctx);

	Barrier barrier = barrierMake(VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
	barrierAddBuffer(&barrier, (r_vkcombuf_barrier_buffer_t){
		.buffer = g_raster.geometry->buffer,
		.access = VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
	});
	barrierCommit(&barrier, combuf);
}

void R_VkRasterSubmit(vk_raster_submit_t args) {
	VkCommandBuffer cmdbuf = args.combuf->cmdbuf;

	// TODO we can sort collected draw commands for more efficient and correct rendering
	// that requires adding info about distance to camera for correct order-dependent blending

	struct {
		VkPipeline pipeline;
		int texture;
		int lightmap;
		uint32_t ubo_offset;
	} cur = {
		.pipeline = VK_NULL_HANDLE,
		.texture = -1,
		.lightmap = -1,
		.ubo_offset = -1,
	};

	const uint32_t dlights_ubo_offset = writeDlightsToUBO();
	if (dlights_ubo_offset == UINT32_MAX)
		return;

	{
		vk_buffer_t* const geom = g_raster.geometry->buffer;
		ASSERT(geom->sync.read.stage & VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
		ASSERT(geom->sync.read.access & VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
		ASSERT(geom->sync.read.access & VK_ACCESS_2_INDEX_READ_BIT);
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmdbuf, 0, 1, &geom->buffer, &offset);
		vkCmdBindIndexBuffer(cmdbuf, geom->buffer, 0, VK_INDEX_TYPE_UINT16);
	}

	for (int i = 0; i < g_render_state.num_draw_commands; ++i) {
		const draw_command_t *const draw = g_render_state.draw_commands + i;

		switch (draw->type) {
			case DrawLabelBegin:
			{
				const VkDebugUtilsLabelEXT label = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
					.pLabelName = draw->debug_label,
				};
				vkCmdBeginDebugUtilsLabelEXT(cmdbuf, &label);
				continue;
			}

			case DrawLabelEnd:
				vkCmdEndDebugUtilsLabelEXT(cmdbuf);
				continue;

			case DrawSky:
			{
				const render_draw_sky_t *draw_sky = &draw->draw_sky;

				if (cur.pipeline != g_raster.pipeline_sky.pipeline) {
					const uint32_t ubo_offset = allocUniform(sizeof(sky_uniform_data_t), 16 /*?*/);
					if (g_render_state.current_ubo_offset_FIXME == ALO_ALLOC_FAILED)
						continue;

					// Compute and upload UBO stuff
					{
						sky_uniform_data_t* const sky_ubo = PTR_CAST(sky_uniform_data_t, (byte*)g_raster.uniform_buffer.mapped + ubo_offset);

						// FIXME model matrix
						Matrix4x4_ToArrayFloatGL(*args.projection_view, (float*)sky_ubo->mvp);

						sky_ubo->resolution[0] = args.width;
						sky_ubo->resolution[1] = args.height;

						// TODO DRY, this is copypasted from vk_rtx.c
						matrix4x4 proj_inv, view_inv;
						Matrix4x4_Invert_Full(proj_inv, *args.projection);
						Matrix4x4_ToArrayFloatGL(proj_inv, (float*)sky_ubo->inv_proj);

						// TODO there's a more efficient way to construct an inverse view matrix
						// from vforward/right/up vectors and origin in g_camera
						Matrix4x4_Invert_Full(view_inv, *args.view);
						Matrix4x4_ToArrayFloatGL(view_inv, (float*)sky_ubo->inv_view);
					}

					cur.pipeline = g_raster.pipeline_sky.pipeline;
					vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, cur.pipeline);

					g_raster.pipeline_sky.values[0].buffer = (VkDescriptorBufferInfo){
						.buffer = g_raster.uniform_buffer.buffer,
						.offset = 0,
						.range = sizeof(sky_uniform_data_t),
					};
					g_raster.pipeline_sky.values[1].image = R_VkTexturesGetSkyboxDescriptorImageInfo( kSkyboxOriginal );
					VK_DescriptorsWrite(&g_raster.pipeline_sky.descs, args.frame_index);

					vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
						g_raster.pipeline_sky.descs.pipeline_layout, 0, 1, g_raster.pipeline_sky.sets + args.frame_index, 1, &ubo_offset);
				}

				ASSERT(draw_sky->index_offset >= 0);
				vkCmdDrawIndexed(cmdbuf, draw_sky->element_count, 1, draw_sky->index_offset, draw_sky->vertex_offset, 0);

				// Reset current draw state
				cur.texture = -1;
				cur.lightmap = -1;
				cur.ubo_offset = -1;

				continue;
			}

			case DrawDraw:
				// Continue drawing below
				break;
		}

		ASSERT(draw->draw.pipeline_index >= 0);
		ASSERT(draw->draw.pipeline_index < COUNTOF(g_raster.pipelines));
		const VkPipeline pipeline = g_raster.pipelines[draw->draw.pipeline_index];

		if (cur.pipeline != pipeline) {
			cur.pipeline = pipeline;
			vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, cur.pipeline);

			// Make sure that after pipeline change we have this bound correctly
			// Pipeline change might be due to previous pipeline being skybox, which has
			// incompatible layout
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_raster.pipeline_layout, 3, 1, vk_desc_fixme.ubo_sets + 1, 1, &dlights_ubo_offset);
		}

		if (cur.ubo_offset != draw->draw.ubo_offset)
		{
			cur.ubo_offset = draw->draw.ubo_offset;
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_raster.pipeline_layout, 0, 1, vk_desc_fixme.ubo_sets, 1, &cur.ubo_offset);
		}

		if (cur.lightmap != draw->draw.lightmap) {
			cur.lightmap = draw->draw.lightmap;
			const VkDescriptorSet lm_unorm = R_VkTextureGetDescriptorUnorm(cur.lightmap);
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_raster.pipeline_layout, 2, 1, &lm_unorm, 0, NULL);
		}

		if (cur.texture != draw->draw.texture)
		{
			cur.texture = draw->draw.texture;
			const VkDescriptorSet tex_unorm = R_VkTextureGetDescriptorUnorm(cur.texture);
			// TODO names/enums for binding points
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_raster.pipeline_layout, 1, 1, &tex_unorm, 0, NULL);
		}

		// Only indexed mode is supported
		ASSERT(draw->draw.index_offset >= 0);
		vkCmdDrawIndexed(cmdbuf, draw->draw.element_count, 1, draw->draw.index_offset, draw->draw.vertex_offset, 0);
	}
}

void VK_RenderDebugLabelBegin( const char *name )
{
	drawCmdPushDebugLabelBegin(name);
}

void VK_RenderDebugLabelEnd( void )
{
	drawCmdPushDebugLabelEnd();
}

static void uboComputeAndSetMVPFromModel( const matrix4x4 model, const matrix4x4 projection_view ) {
	matrix4x4 mvp;
	Matrix4x4_Concat(mvp, projection_view, model);
	Matrix4x4_ToArrayFloatGL(mvp, (float*)g_render_state.dirty_uniform_data.mvp);
}

void R_VkRasterAddModel( vk_raster_add_model_t args ) {
	int current_texture = args.textures_override;
	int element_count = 0;
	int index_offset = -1;
	int vertex_offset = 0;

	// TODO get rid of this dirty ubo thing
	uboComputeAndSetMVPFromModel( *args.transform, *args.projection_view );
	Vector4Copy(*args.color, g_render_state.dirty_uniform_data.color);

	ASSERT(args.lightmap <= MAX_LIGHTMAPS);
	const int lightmap = args.lightmap > 0 ? tglob.lightmapTextures[args.lightmap - 1] : tglob.whiteTexture;

	drawCmdPushDebugLabelBegin( args.debug_name );

	for (int i = 0; i < args.geometries_count; ++i) {
		const vk_render_geometry_t *geom = args.geometries + i;
		const int tex_mat = geom->material.tex_base_color;
		const int geom_tex = g_raster.use_material_textures->value && (tex_mat > 0 && tex_mat < MAX_TEXTURES) ? tex_mat : geom->ye_olde_texture;
		const int tex = args.textures_override > 0 ? args.textures_override : geom_tex;
		const qboolean split =
			   current_texture != tex
			|| vertex_offset != geom->vertex_offset
			|| (index_offset + element_count) != geom->index_offset;

		// We only support indexed geometry
		ASSERT(geom->index_offset >= 0);

		if (tex < 0)
			continue;

		// TODO consider tracking contiguousness in drawCmdPushDraw(Sky)()
		// Why: we could easily check that the previous command in the command list
		// is contiguous, and could just increase its counts w/o submitting a new command
		// This would make this code here a bit more readable and single-purpose.
		if (split) {
			if (element_count) {
				if (current_texture == TEX_BASE_SKYBOX) {
					drawCmdPushDrawSky(&(render_draw_sky_t){
						.element_count = element_count,
						.vertex_offset = vertex_offset,
						.index_offset = index_offset,
					});
				} else {
					render_draw_t draw = {
						.lightmap = lightmap,
						.texture = current_texture,
						.pipeline_index = args.render_type,
						.element_count = element_count,
						.vertex_offset = vertex_offset,
						.index_offset = index_offset,
					};

					drawCmdPushDraw( &draw );
				}
			}

			current_texture = tex;
			index_offset = geom->index_offset;
			vertex_offset = geom->vertex_offset;
			element_count = 0;
		}

		// Make sure that all surfaces are concatenated in buffers
		ASSERT(index_offset + element_count == geom->index_offset);
		element_count += geom->element_count;
	}

	if (element_count) {
		if (current_texture == TEX_BASE_SKYBOX) {
			drawCmdPushDrawSky(&(render_draw_sky_t){
				.element_count = element_count,
				.vertex_offset = vertex_offset,
				.index_offset = index_offset,
			});
		} else {
			const render_draw_t draw = {
				.lightmap = lightmap,
				.texture = current_texture,
				.pipeline_index = args.render_type,
				.element_count = element_count,
				.vertex_offset = vertex_offset,
				.index_offset = index_offset,
			};

			drawCmdPushDraw( &draw );
		}
	}

	drawCmdPushDebugLabelEnd();
}
