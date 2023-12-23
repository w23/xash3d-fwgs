#include "vk_overlay.h"

#include "vk_buffer.h"
#include "vk_core.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_framectl.h"
#include "vk_renderstate.h"
#include "vk_pipeline.h"
#include "vk_descriptor.h"
#include "vk_logs.h"

#include "com_strings.h"
#include "eiface.h"


typedef struct vertex_2d_s {
	float x, y;
	float u, v;
	color_rgba8_t color;
} vertex_2d_t;

// TODO should these be dynamic?
#define MAX_PICS 16384
#define MAX_VERTICES (MAX_PICS * 6)
#define MAX_BATCHES 16384

typedef struct {
	uint32_t vertex_offset, vertex_count;
	int texture;
	int pipeline_mode;
} batch_t;

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kRenderTransAdd + kRenderLineMode + 1];

	vk_buffer_t pics_buffer;
	r_flipping_buffer_t pics_buffer_alloc;

	vk_buffer_t lines_buffer;
	r_flipping_buffer_t lines_buffer_alloc;

	qboolean exhausted_this_frame;

	batch_t batch[MAX_BATCHES];
	int batch_count;

	int white_texture_ref_index;
	// TODO texture bindings?
} g2d;

static vertex_2d_t* allocQuadVerts(int pipeline_mode, int texnum) {
	const uint32_t pics_offset = R_FlippingBuffer_Alloc(&g2d.pics_buffer_alloc, 6, 1);
	vertex_2d_t* const ptr = ((vertex_2d_t*)(g2d.pics_buffer.mapped)) + pics_offset;
	batch_t *batch = g2d.batch + (g2d.batch_count-1);

	if (pics_offset == ALO_ALLOC_FAILED) {
		if (!g2d.exhausted_this_frame) {
			gEngine.Con_Printf(S_ERROR "2d: ran out of vertex memory\n");
			g2d.exhausted_this_frame = true;
		}
		return NULL;
	}

	if (batch->texture != texnum
		|| batch->pipeline_mode != pipeline_mode
		|| batch->vertex_offset > pics_offset) {
		if (batch->vertex_count != 0) {
			if (g2d.batch_count == MAX_BATCHES) {
				if (!g2d.exhausted_this_frame) {
					gEngine.Con_Printf(S_ERROR "2d: ran out of batch memory\n");
					g2d.exhausted_this_frame = true;
				}
				return NULL;
			}

			++g2d.batch_count;
			batch++;
		}

		batch->vertex_offset = pics_offset;
		batch->vertex_count = 0;
		batch->texture = texnum;
		batch->pipeline_mode = pipeline_mode;
	}

	batch->vertex_count += 6;
	ASSERT(batch->vertex_count + batch->vertex_offset <= MAX_VERTICES);
	return ptr;
}

static vertex_2d_t* allocDoubleVerts(int pipeline_mode, int texnum) {
	const uint32_t lines_offset = (R_FlippingBuffer_Alloc(&g2d.lines_buffer_alloc, 2, 1) / 2);
	vertex_2d_t* const ptr = ((vertex_2d_t*)(g2d.lines_buffer.mapped)) + lines_offset;
	batch_t *batch = g2d.batch + (g2d.batch_count-1);

	if (lines_offset == ALO_ALLOC_FAILED) {
		if (!g2d.exhausted_this_frame) {
			gEngine.Con_Printf(S_ERROR "2d: ran out of vertex memory\n");
			g2d.exhausted_this_frame = true;
		}
		return NULL;
	}
	if (batch->texture != texnum ||
		batch->pipeline_mode != pipeline_mode
		|| batch->vertex_offset > lines_offset) {
		if (batch->vertex_count != 0) {
			if (g2d.batch_count == MAX_BATCHES) {
				if (!g2d.exhausted_this_frame) {
					gEngine.Con_Printf(S_ERROR "2d: ran out of batch memory\n");
					g2d.exhausted_this_frame = true;
				}
				return NULL;
			}

			++g2d.batch_count;
			batch++;
		}

		batch->vertex_offset = lines_offset;
		batch->vertex_count = 0;
		batch->texture = texnum; 
		batch->pipeline_mode = pipeline_mode;
	}

	batch->vertex_count += 2;
	ASSERT(batch->vertex_count + batch->vertex_offset <= MAX_VERTICES);
	return ptr;
}

void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	vertex_2d_t *const p = allocQuadVerts(vk_renderstate.blending_mode, texnum);

	if (!p) {
		/* gEngine.Con_Printf(S_ERROR "VK FIXME %s(%f, %f, %f, %f, %f, %f, %f, %f, %d(%s))\n", __FUNCTION__, */
		/* 	x, y, w, h, s1, t1, s2, t2, texnum, R_TextureGetByIndex(texnum)->name); */
		return;
	}

	{
		// TODO do this in shader bro
		const float vw = vk_frame.width;
		const float vh = vk_frame.height;
		const float x1 = (x / vw)*2.f - 1.f;
		const float y1 = (y / vh)*2.f - 1.f;
		const float x2 = ((x + w) / vw)*2.f - 1.f;
		const float y2 = ((y + h) / vh)*2.f - 1.f;
		const color_rgba8_t color = vk_renderstate.tri_color;

		p[0] = (vertex_2d_t){x1, y1, s1, t1, color};
		p[1] = (vertex_2d_t){x1, y2, s1, t2, color};
		p[2] = (vertex_2d_t){x2, y1, s2, t1, color};
		p[3] = (vertex_2d_t){x2, y1, s2, t1, color};
		p[4] = (vertex_2d_t){x1, y2, s1, t2, color};
		p[5] = (vertex_2d_t){x2, y2, s2, t2, color};
	}
}

static void drawLine(float x1, float y1, float x2, float y2, float r, float g, float b) 
{
	const color_rgba8_t prev_color = vk_renderstate.tri_color;
	const int prev_blending = vk_renderstate.blending_mode;
	
	// TODO: Line with transparency
	vk_renderstate.blending_mode = kRenderLineMode;
	vk_renderstate.tri_color = (color_rgba8_t){r, g, b, 255};
	
	vertex_2d_t *const p = allocDoubleVerts(vk_renderstate.blending_mode, g2d.white_texture_ref_index);

	if (!p) 
		return;
	

	{
		const color_rgba8_t color = vk_renderstate.tri_color;

		const float vw = vk_frame.width;
		const float vh = vk_frame.height;
		
		x1 = (x1 / vw) * 2.f - 1.f;
		y1 = (y1 / vh) * 2.f - 1.f;
		x2 = (x2 / vw) * 2.f - 1.f;
		y2 = (y2 / vh) * 2.f - 1.f;
		
		p[0] = (vertex_2d_t){x1, y1, 0, 0, color};
		p[1] = (vertex_2d_t){x2, y2, 0, 0, color};
	}
	
	vk_renderstate.tri_color = prev_color;
	vk_renderstate.blending_mode = prev_blending;
}

static void drawFill( float x, float y, float w, float h, int r, int g, int b, int a, int blending_mode )
{
	const color_rgba8_t prev_color = vk_renderstate.tri_color;
	const int prev_blending = vk_renderstate.blending_mode;
	vk_renderstate.blending_mode = blending_mode;
	vk_renderstate.tri_color = (color_rgba8_t){r, g, b, a};
	R_DrawStretchPic(x, y, w, h, 0, 0, 1, 1, g2d.white_texture_ref_index);
	vk_renderstate.tri_color = prev_color;
	vk_renderstate.blending_mode = prev_blending;
}

static void clearAccumulated( void ) {
	R_FlippingBuffer_Flip(&g2d.pics_buffer_alloc);
	R_FlippingBuffer_Flip(&g2d.lines_buffer_alloc);

	g2d.batch_count = 1;
	g2d.batch[0].texture = -1;
	g2d.batch[0].vertex_offset = 0;
	g2d.batch[0].vertex_count = 0;
	g2d.exhausted_this_frame = false;
}

static qboolean createPipelines( void )
{
	{
		/* VkPushConstantRange push_const = { */
		/* 	.offset = 0, */
		/* 	.size = sizeof(AVec3f), */
		/* 	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, */
		/* }; */

		VkDescriptorSetLayout descriptor_layouts[] = {
			vk_desc_fixme.one_texture_layout,
		};

		VkPipelineLayoutCreateInfo plci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = ARRAYSIZE(descriptor_layouts),
			.pSetLayouts = descriptor_layouts,
			/* .pushConstantRangeCount = 1, */
			/* .pPushConstantRanges = &push_const, */
		};

		XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &g2d.pipeline_layout));
	}

	{
		const VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex_2d_t, x)},
			{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex_2d_t, u)},
			{.binding = 0, .location = 2, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(vertex_2d_t, color)},
		};

		const vk_shader_stage_t shader_stages[] = {
		{
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.filename = "2d.vert.spv",
		}, {
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.filename = "2d.frag.spv",
		}};

		vk_pipeline_graphics_create_info_t pci = {
			.layout = g2d.pipeline_layout,
			.attribs = attribs,
			.num_attribs = ARRAYSIZE(attribs),
			.stages = shader_stages,
			.num_stages = ARRAYSIZE(shader_stages),
			.vertex_stride = sizeof(vertex_2d_t),
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_ALWAYS,
			.cullMode = VK_CULL_MODE_NONE,
			.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, // By default 
		};

		for (int i = 0; i < ARRAYSIZE(g2d.pipelines); ++i)
		{
			switch (i)
			{
				case kRenderNormal:
					pci.blendEnable = VK_FALSE;
					break;

				case kRenderTransColor:
				case kRenderTransTexture:
					pci.blendEnable = VK_TRUE;
					pci.colorBlendOp = VK_BLEND_OP_ADD;
					pci.srcAlphaBlendFactor = pci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					pci.dstAlphaBlendFactor = pci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;

				case kRenderTransAlpha:
					pci.blendEnable = VK_FALSE;
					// FIXME pglEnable( GL_ALPHA_TEST );
					break;

				case kRenderGlow:
				case kRenderTransAdd:
					pci.blendEnable = VK_TRUE;
					pci.colorBlendOp = VK_BLEND_OP_ADD;
					pci.srcAlphaBlendFactor = pci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					pci.dstAlphaBlendFactor = pci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					break;
				case kRenderLineMode: 
					pci.blendEnable = VK_FALSE;
					pci.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
					break;
			}
			
			g2d.pipelines[i] = VK_PipelineGraphicsCreate(&pci);

			if (!g2d.pipelines[i])
			{
				ERR("Pipeline %i is not created. Probably ci->num_stages > MAX_STAGES", i);
				return false;
			}
		}
	}

	return true;
}

qboolean R_VkOverlay_Init( void ) {
	if (!createPipelines())
		return false;

	// TODO this doesn't need to be host visible, could use staging too
	if (!VK_BufferCreate("2d pics_buffer", &g2d.pics_buffer, sizeof(vertex_2d_t) * MAX_VERTICES,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ))
		// FIXME cleanup
		return false;

	R_FlippingBuffer_Init(&g2d.pics_buffer_alloc, MAX_VERTICES);

	if (!VK_BufferCreate("2d lines_buffer", &g2d.lines_buffer, sizeof(vertex_2d_t) * MAX_VERTICES,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ))
		return false;

	R_FlippingBuffer_Init(&g2d.lines_buffer_alloc, MAX_VERTICES);

	g2d.white_texture_ref_index = R_TextureFindByName(REF_WHITE_TEXTURE);

	return true;
}

void R_VkOverlay_Shutdown( void ) {
	VK_BufferDestroy(&g2d.pics_buffer);
	VK_BufferDestroy(&g2d.lines_buffer);
	
	for (int i = 0; i < ARRAYSIZE(g2d.pipelines); ++i)
		vkDestroyPipeline(vk_core.device, g2d.pipelines[i], NULL);

	vkDestroyPipelineLayout(vk_core.device, g2d.pipeline_layout, NULL);
}

static void drawImages(VkCommandBuffer cmdbuf)
{
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmdbuf, 0, 1, &g2d.pics_buffer.buffer, &offset);
	
	for (int i = 0; i < g2d.batch_count && g2d.batch[i].vertex_count > 0; ++i)
	{
		const int pipeline_mode = g2d.batch[i].pipeline_mode;
		if(pipeline_mode == kRenderLineMode)
			continue;

		const VkPipeline pipeline = g2d.pipelines[pipeline_mode];
		const VkDescriptorSet tex_unorm = R_VkTextureGetDescriptorUnorm( g2d.batch[i].texture );
		if (tex_unorm)
		{
			vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g2d.pipeline_layout, 0, 1, &tex_unorm, 0, NULL);
			vkCmdDraw(cmdbuf, g2d.batch[i].vertex_count, 1, g2d.batch[i].vertex_offset, 0);
		} // FIXME else what?
	}
	
}

static void drawLines(VkCommandBuffer cmdbuf)
{
	const VkDeviceSize offset = 0;

	vkCmdBindVertexBuffers(cmdbuf, 0, 1, &g2d.lines_buffer.buffer, &offset);
	
	for (int i = 0; i < g2d.batch_count && g2d.batch[i].vertex_count > 0; ++i)
	{
		const int pipeline_mode = g2d.batch[i].pipeline_mode;
		if(pipeline_mode != kRenderLineMode)
			continue;

		const VkDescriptorSet tex_unorm = R_VkTextureGetDescriptorUnorm(g2d.batch[i].texture);
		const VkPipeline pipeline = g2d.pipelines[pipeline_mode];

		if (tex_unorm)
		{
			vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g2d.pipeline_layout, 0, 1, &tex_unorm, 0, NULL);
			vkCmdDraw(cmdbuf, g2d.batch[i].vertex_count, 1, g2d.batch[i].vertex_offset, 0);
		} // FIXME else what?
	}

}

static void drawOverlay( VkCommandBuffer cmdbuf ) {
	DEBUG_BEGIN(cmdbuf, "2d overlay");
	
	drawImages(cmdbuf);
	drawLines(cmdbuf);

	DEBUG_END(cmdbuf);
}

void R_VkOverlay_DrawAndFlip( VkCommandBuffer cmdbuf, qboolean draw ) {
	if (draw)
		drawOverlay(cmdbuf);

	clearAccumulated();
}

void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{
	PRINT_NOT_IMPLEMENTED();
}

void R_DrawTileClear( int texnum, int x, int y, int w, int h )
{
	PRINT_NOT_IMPLEMENTED_ARGS("%s", R_TextureGetNameByIndex(texnum));
}

void CL_FillRGBA( float x, float y, float w, float h, int r, int g, int b, int a )
{
	drawFill(x, y, w, h, r, g, b, a, kRenderTransAdd);
}

void CL_FillRGBABlend( float x, float y, float w, float h, int r, int g, int b, int a )
{
	drawFill(x, y, w, h, r, g, b, a, kRenderTransColor);
}

void R_DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b) 
{
	drawLine(x1, y1, x2, y2, r, g, b);
}