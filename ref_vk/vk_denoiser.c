#include "vk_denoiser.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"

#include "eiface.h" // ARRAYSIZE

enum {
	DenoiserBinding_DestImage = 0,

	DenoiserBinding_Source_BaseColor = 1,
	DenoiserBinding_Source_DiffuseGI = 2,
	DenoiserBinding_Source_Specular = 3,
	DenoiserBinding_Source_Additive = 4,
	DenoiserBinding_Source_Normals = 5,
	DenoiserBinding_Source_SH1 = 6,
	DenoiserBinding_Source_SH2 = 7,

	DenoiserBinding_COUNT
};

enum {
	DenoiserSH_Binding_SH1_DestImage = 0,
	DenoiserSH_Binding_SH2_DestImage = 1,

	DenoiserSH_Binding_SH1_Source = 2,
	DenoiserSH_Binding_SH2_Source = 3,

	DenoiserSH_Binding_COUNT
};

static struct {
	vk_descriptors_t descriptors;
	vk_descriptor_value_t desc_values[DenoiserBinding_COUNT];

	VkDescriptorSetLayoutBinding desc_bindings[DenoiserBinding_COUNT];
	VkDescriptorSet desc_sets[1];

	VkPipeline pipeline;
} g_denoiser = { 0 };

static struct {
	vk_descriptors_t descriptors;
	vk_descriptor_value_t desc_values[DenoiserSH_Binding_COUNT];

	VkDescriptorSetLayoutBinding desc_bindings[DenoiserSH_Binding_COUNT];
	VkDescriptorSet desc_sets[1];

	VkPipeline pipeline;
} g_denoiser_sh = { 0 };

static void createLayouts(void) {
	g_denoiser.descriptors.bindings = g_denoiser.desc_bindings;
	g_denoiser.descriptors.num_bindings = ARRAYSIZE(g_denoiser.desc_bindings);
	g_denoiser.descriptors.values = g_denoiser.desc_values;
	g_denoiser.descriptors.num_sets = 1;
	g_denoiser.descriptors.desc_sets = g_denoiser.desc_sets;
	g_denoiser.descriptors.push_constants = (VkPushConstantRange){
		.offset = 0,
		.size = 0,
		.stageFlags = 0,
	};

	g_denoiser.desc_bindings[DenoiserBinding_DestImage] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_DestImage,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_BaseColor] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_BaseColor,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_DiffuseGI] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_DiffuseGI,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_Specular] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_Specular,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_Additive] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_Additive,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_Normals] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_Normals,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_SH1] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_SH1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_SH2] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_SH2,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	VK_DescriptorsCreate(&g_denoiser.descriptors);
}


static void createLayoutsSH(void) {
	g_denoiser_sh.descriptors.bindings = g_denoiser_sh.desc_bindings;
	g_denoiser_sh.descriptors.num_bindings = ARRAYSIZE(g_denoiser_sh.desc_bindings);
	g_denoiser_sh.descriptors.values = g_denoiser_sh.desc_values;
	g_denoiser_sh.descriptors.num_sets = 1;
	g_denoiser_sh.descriptors.desc_sets = g_denoiser_sh.desc_sets;
	g_denoiser_sh.descriptors.push_constants = (VkPushConstantRange){
		.offset = 0,
		.size = 0,
		.stageFlags = 0,
	};

	g_denoiser_sh.desc_bindings[DenoiserSH_Binding_SH1_DestImage] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserSH_Binding_SH1_DestImage,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser_sh.desc_bindings[DenoiserSH_Binding_SH2_DestImage] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserSH_Binding_SH2_DestImage,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser_sh.desc_bindings[DenoiserSH_Binding_SH1_Source] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserSH_Binding_SH1_Source,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser_sh.desc_bindings[DenoiserSH_Binding_SH2_Source] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserSH_Binding_SH2_Source,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	VK_DescriptorsCreate(&g_denoiser_sh.descriptors);
}

static VkPipeline createPipeline(void) {
	const vk_pipeline_compute_create_info_t pcci = {
		.layout = g_denoiser.descriptors.pipeline_layout,
		.shader_filename = "denoiser.comp.spv",
		.specialization_info = NULL,
	};

	return VK_PipelineComputeCreate(&pcci);
}

static VkPipeline createPipelineSH(void) {
	const vk_pipeline_compute_create_info_t pcci = {
		.layout = g_denoiser_sh.descriptors.pipeline_layout,
		.shader_filename = "denoiser_sh.comp.spv",
		.specialization_info = NULL,
	};

	return VK_PipelineComputeCreate(&pcci);
}

qboolean XVK_DenoiserInit(void) {
	ASSERT(vk_core.rtx);

	createLayouts();

	ASSERT(!g_denoiser.pipeline);
	g_denoiser.pipeline = createPipeline();

	createLayoutsSH();

	ASSERT(!g_denoiser_sh.pipeline);
	g_denoiser_sh.pipeline = createPipelineSH();

	return g_denoiser.pipeline != VK_NULL_HANDLE && g_denoiser_sh.pipeline != VK_NULL_HANDLE;
}

void XVK_DenoiserDestroy(void) {
	ASSERT(vk_core.rtx);
	ASSERT(g_denoiser.pipeline);

	vkDestroyPipeline(vk_core.device, g_denoiser.pipeline, NULL);
	VK_DescriptorsDestroy(&g_denoiser.descriptors);

	ASSERT(g_denoiser_sh.pipeline);

	vkDestroyPipeline(vk_core.device, g_denoiser_sh.pipeline, NULL);
	VK_DescriptorsDestroy(&g_denoiser_sh.descriptors);
}

void XVK_DenoiserReloadPipeline(void) {
	// TODO handle errors gracefully
	vkDestroyPipeline(vk_core.device, g_denoiser.pipeline, NULL);
	g_denoiser.pipeline = createPipeline();

	vkDestroyPipeline(vk_core.device, g_denoiser_sh.pipeline, NULL);
	g_denoiser_sh.pipeline = createPipelineSH();
}

void XVK_DenoiserDenoise(const xvk_denoiser_args_t* args) {
	const uint32_t WG_W = 16;
	const uint32_t WG_H = 8;


	// Create spherical harmonics from directions and colors of indirectional lighting

	g_denoiser_sh.desc_values[DenoiserSH_Binding_SH1_Source].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.indirect_sh1_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser_sh.desc_values[DenoiserSH_Binding_SH2_Source].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.indirect_sh2_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser_sh.desc_values[DenoiserSH_Binding_SH1_DestImage].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->sh1_blured_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser_sh.desc_values[DenoiserSH_Binding_SH2_DestImage].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->sh2_blured_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VK_DescriptorsWrite(&g_denoiser_sh.descriptors);

	vkCmdBindPipeline(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser_sh.pipeline);
	vkCmdBindDescriptorSets(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser_sh.descriptors.pipeline_layout, 0, 1, g_denoiser_sh.descriptors.desc_sets + 0, 0, NULL);
	vkCmdDispatch(args->cmdbuf, (args->width + WG_W - 1) / WG_W, (args->height + WG_H - 1) / WG_H, 1);


	// Compose with blur and relighting by spherical harmonics

	g_denoiser.desc_values[DenoiserBinding_Source_BaseColor].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.base_color_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_DiffuseGI].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.diffuse_gi_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_Specular].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.specular_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_Additive].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.additive_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_Normals].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.normals_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_SH1].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->sh1_blured_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_SH2].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->sh2_blured_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_DestImage].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->dst_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VK_DescriptorsWrite(&g_denoiser.descriptors);

	vkCmdBindPipeline(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.pipeline);
	vkCmdBindDescriptorSets(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.descriptors.pipeline_layout, 0, 1, g_denoiser.descriptors.desc_sets + 0, 0, NULL);
	vkCmdDispatch(args->cmdbuf, (args->width + WG_W - 1) / WG_W, (args->height + WG_H - 1) / WG_H, 1);
}
