// Common definitions for both shaders and native code
#ifndef RAY_INTEROP_H_INCLUDED
#define RAY_INTEROP_H_INCLUDED

#define LIST_SPECIALIZATION_CONSTANTS(X) \
	X(0, uint, MAX_POINT_LIGHTS, 256) \
	X(1, uint, MAX_EMISSIVE_KUSOCHKI, 256) \
	X(2, uint, MAX_VISIBLE_POINT_LIGHTS, 63) \
	X(3, uint, MAX_VISIBLE_SURFACE_LIGHTS, 255) \
	X(4, float, LIGHT_GRID_CELL_SIZE, 128.) \
	X(5, uint, MAX_LIGHT_CLUSTERS, 262144) \
	X(6, uint, MAX_TEXTURES, 4096) \
	X(7, uint, SBT_RECORD_SIZE, 32) \

#ifndef GLSL
#include "xash3d_types.h"
#include "vk_const.h"
#define MAX_EMISSIVE_KUSOCHKI 256
#define uint uint32_t
#define vec2 vec2_t
#define vec3 vec3_t
#define vec4 vec4_t
#define mat4 matrix4x4
typedef int ivec3[3];
typedef int ivec2[2];
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define PAD(x) float TOKENPASTE2(pad_, __LINE__)[x];
#define STRUCT struct

enum {
#define DECLARE_SPECIALIZATION_CONSTANT(index, type, name, default_value) \
	SPEC_##name##_INDEX = index,
LIST_SPECIALIZATION_CONSTANTS(DECLARE_SPECIALIZATION_CONSTANT)
#undef DECLARE_SPECIALIZATION_CONSTANT
};

#else // if GLSL else
#extension GL_EXT_shader_8bit_storage : require

#define PAD(x)
#define STRUCT

#define DECLARE_SPECIALIZATION_CONSTANT(index, type, name, default_value) \
	layout (constant_id = index) const type name = default_value;
LIST_SPECIALIZATION_CONSTANTS(DECLARE_SPECIALIZATION_CONSTANT)
#undef DECLARE_SPECIALIZATION_CONSTANT

#endif // not GLSL

struct Vertex {
	vec3 pos;
	vec3 prev_pos;
	vec3 normal;
	vec3 tangent;
	vec2 gl_tc;
	vec2 _unused_lm_tc;
	uint color;
};

#define GEOMETRY_BIT_OPAQUE 0x01
#define GEOMETRY_BIT_ALPHA_TEST 0x02
#define GEOMETRY_BIT_BLEND 0x04
#define GEOMETRY_BIT_REFRACTIVE 0x08
#define GEOMETRY_BIT_CASTS_SHADOW 0x10

#define SHADER_OFFSET_MISS_REGULAR 0
#define SHADER_OFFSET_MISS_SHADOW 1
#define SHADER_OFFSET_MISS_EMPTY 2

#define SHADER_OFFSET_HIT_REGULAR 0
#define SHADER_OFFSET_HIT_ALPHA_TEST 1
#define SHADER_OFFSET_HIT_ADDITIVE 2

#define SHADER_OFFSET_HIT_REGULAR_BASE 0
#define SHADER_OFFSET_HIT_SHADOW_BASE 3

#define MATERIAL_MODE_OPAQUE 0
#define MATERIAL_MODE_OPAQUE_ALPHA_TEST 1
#define MATERIAL_MODE_TRANSLUCENT 2
#define MATERIAL_MODE_BLEND_ADD 3
#define MATERIAL_MODE_BLEND_MIX 4
#define MATERIAL_MODE_BLEND_GLOW 5
#define MATERIAL_MODE_DECAL 6
#define MATERIAL_MODE_COUNT 7

#define TEX_BASE_SKYBOX 0x0f000000u

struct Material {
	uint tex_base_color;

	// TODO can be combined into a single texture
	uint tex_roughness;
	uint tex_metalness;
	uint tex_normalmap;

	// TODO:
	// uint tex_emissive;
	// uint tex_detail;

	float roughness;
	float metalness;
	float normal_scale;
	PAD(1)

	vec4 base_color;
};

struct ModelHeader {
	mat4 prev_transform;
	vec4 color;
	uint mode;
	PAD(3)
};

struct Kusok {
	// Geometry data, static
	uint index_offset;
	uint vertex_offset;

	// material below consists of scalar fields only, so it's not aligned to vec4.
	// Alignt it here to vec4 explicitly, so that later vector fields are properly aligned (for simplicity).
	uint _padding0[2];

	// Per-kusok because individual surfaces can be patched
	// TODO? still move to material, or its own table? As this can be dynamic
	vec3 emissive;
	PAD(1)

	// TODO reference into material table
	STRUCT Material material;
};

struct PointLight {
	vec4 origin_r2; // vec4(center.xyz, radius²)
	vec4 color_stopdot;
	vec4 dir_stopdot2;

	// TODO move to either dedicated array, or section of array (by-index type delimiter)
	uint environment; // Is directional-only environment light
	uint flashlight; // Is flashlight spotlight
	PAD(2)
};

struct PolygonLight {
	vec4 plane;

	vec3 center;
	float area;

	vec3 emissive;
	uint vertices_count_offset;
};

struct LightsMetadata {
	uint num_polygons;
	uint num_point_lights;
	PAD(2)
	ivec3 grid_min_cell;
	PAD(1)
	ivec3 grid_size;
	PAD(1)
	STRUCT PointLight point_lights[MAX_POINT_LIGHTS];
	STRUCT PolygonLight polygons[MAX_EMISSIVE_KUSOCHKI];
	vec4 polygon_vertices[MAX_EMISSIVE_KUSOCHKI * 7]; // vec3 but aligned
};

struct LightCluster {
	uint8_t num_point_lights;
	uint8_t num_polygons;
	uint8_t point_lights[MAX_VISIBLE_POINT_LIGHTS];
	uint8_t polygons[MAX_VISIBLE_SURFACE_LIGHTS];
};

#define PUSH_FLAG_LIGHTMAP_ONLY 0x01

#define DEBUG_DISPLAY_DISABLED 0
#define DEBUG_DISPLAY_BASECOLOR 1
#define DEBUG_DISPLAY_BASEALPHA 2
#define DEBUG_DISPLAY_EMISSIVE 3
#define DEBUG_DISPLAY_NSHADE 4
#define DEBUG_DISPLAY_NGEOM 5
#define DEBUG_DISPLAY_LIGHTING 6
#define DEBUG_DISPLAY_SURFHASH 7
#define DEBUG_DISPLAY_DIRECT 8
#define DEBUG_DISPLAY_DIRECT_DIFF 9
#define DEBUG_DISPLAY_DIRECT_SPEC 10
#define DEBUG_DISPLAY_INDIRECT 11
#define DEBUG_DISPLAY_INDIRECT_DIFF 12
#define DEBUG_DISPLAY_INDIRECT_SPEC 13
#define DEBUG_DISPLAY_TRIHASH 14
#define DEBUG_DISPLAY_MATERIAL 15
#define DEBUG_DISPLAY_DIFFUSE 16
#define DEBUG_DISPLAY_SPECULAR 17
// add more when needed

#define DEBUG_FLAG_WHITE_FURNACE (1<<0)

#define RENDERER_FLAG_ONLY_DIFFUSE_GI (1<<0)
#define RENDERER_FLAG_SEPARATED_REFLECTION (1<<1)
#define RENDERER_FLAG_DENOISE_GI_BY_SH (1<<2)
#define RENDERER_FLAG_DISABLE_GI (1<<3)
#define RENDERER_FLAG_SPATIAL_RECONSTRUCTION (1<<4)
#define RENDERER_FLAG_DISABLE_REPROJECTION (1<<6)

#define ASVGF_COMPATIBILITY_STATS 0u
#define ASVGF_COMPATIBILITY_LUMA_DELTA 1u

#define ASVGF_HISTORY_FILTER_NONE 0u
#define ASVGF_HISTORY_FILTER_LUMA_OUTLIER_CLAMP 1u

#define ASVGF_DEPENDENCY_NONE 0u
#define ASVGF_DEPENDENCY_COMBINE_MIN 1u
#define ASVGF_DEPENDENCY_RELIGHT_CARRY 2u
#define ASVGF_DEPENDENCY_VARIANCE_CARRY 3u

struct AsvgfReprojectionParams {
	float history_samples_max;
	float history_current_weight_min;
	float reprojection_depth_threshold_scale;
	float parallax_depth_threshold_scale;

	float variance_compatibility_luma_scale;
	float variance_compatibility_delta_floor;
	float variance_compatibility_signal_floor;
	float variance_compatibility_smooth_min;
	float variance_compatibility_smooth_max;
	float variance_compatibility_floor;

	float variance_gate_min;
	float variance_gate_max;
	float variance_gate_floor;
	float variance_soften_threshold;
	float variance_soften_mix;
	float variance_stage_mix;

	float reset_min_scale;
	float reset_hard_variance_gate;
	float reset_hard_compatibility;
	float reset_hard_factor;
	float variance_reset_boost_smooth_min;
	float variance_reset_boost_smooth_max;

	float analytical_variance_signal_floor;
	float analytical_variance_floor_base;
	float analytical_variance_floor_signal_scale;
	float analytical_variance_ratio_min;
	float analytical_variance_ratio_max;
	float analytical_variance_mean_min;
	float analytical_variance_mean_max;

	float luma_delta_gate_signal_floor;
	float luma_delta_gate_denominator_offset;
	float luma_delta_gate_smooth_min;
	float luma_delta_gate_smooth_max;
	float luma_delta_gate_mix;

	float external_gate_floor;
	float external_gate_mix;
	float external_gate_hard_threshold;

	float dependency_gate_floor;
	float dependency_gate_soft_min;
	float dependency_gate_soft_max;
	float dependency_gate_mix;

	float dependency_luma_signal_floor;
	float dependency_luma_denominator_offset;
	float dependency_luma_smooth_min;
	float dependency_luma_smooth_max;
	float dependency_luma_mix;

	float dependency_off_history_min;
	float dependency_off_current_max;
	float dependency_off_ratio_min;

	float history_luma_clamp;
	float history_outlier_noise_mean_floor;
	float history_outlier_noise_gate_min;
	float history_outlier_noise_gate_max;
	float history_outlier_envelope_sigma_min;
	float history_outlier_envelope_sigma_max;
	float history_outlier_envelope_mean_min;
	float history_outlier_envelope_mean_max;
	float history_outlier_envelope_min;
	float history_outlier_threshold_low_min;
	float history_outlier_threshold_low_max;
	float history_outlier_threshold_high_min;
	float history_outlier_threshold_high_max;
	float history_outlier_luma_cap_min;
	float history_outlier_luma_cap_scale_min;
	float history_outlier_luma_cap_scale_max;
	float history_outlier_external_gate_attenuation_min;
	float history_outlier_external_gate_attenuation_max;
	float history_outlier_accumulated_luma_scale_min;
	float history_outlier_accumulated_luma_scale_max;

	float parallax_roughness_threshold;
	float parallax_shading_normal_threshold;

	uint variance_compatibility_strategy;
	uint history_filter_strategy;
	uint dependency_strategy;
	uint use_dependency_reset_as_gate;
	PAD(1)
};

struct AsvgfParams {
	STRUCT AsvgfReprojectionParams direct_diffuse;
	STRUCT AsvgfReprojectionParams direct_specular;
	STRUCT AsvgfReprojectionParams indirect_diffuse;
	STRUCT AsvgfReprojectionParams indirect_specular;
};

struct UniformBuffer {
	mat4 inv_proj, inv_view;
	mat4 prev_inv_proj, prev_inv_view;
	ivec2 res;
	float ray_cone_width;
	uint random_seed;
	uint frame_counter;
	float skybox_exposure;

	uint debug_display_only;
	uint debug_flags;

	uint renderer_flags;
	PAD(3)

	STRUCT AsvgfParams asvgf;
};

#undef PAD
#undef STRUCT

#ifndef GLSL
#undef uint
#undef vec3
#undef vec4
#undef TOKENPASTE
#undef TOKENPASTE2
#endif

#endif // RAY_INTEROP_H_INCLUDED
