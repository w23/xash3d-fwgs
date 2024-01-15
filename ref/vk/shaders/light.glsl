#extension GL_EXT_control_flow_attributes : require
#include "debug.glsl"

const float color_culling_threshold = 0;//600./color_factor;
const float shadow_offset_fudge = .1;

#include "brdf.glsl"
#include "light_common.glsl"

#if LIGHT_POLYGON
#include "light_polygon.glsl"
#endif

#if LIGHT_POINT
// TODO Consider splitting into different arrays:
// 1. Spherical lights
// 2. Spotlights
// 3. Env|dir lights
void computePointLights(vec3 P, vec3 N, uint cluster_index, vec3 view_dir, MaterialProperties material, out vec3 diffuse, out vec3 specular) {
	diffuse = specular = vec3(0.);

	//diffuse = vec3(1.);//float(lights.m.num_point_lights) / 64.);
#define USE_CLUSTERS
#ifdef USE_CLUSTERS
	const uint num_point_lights = uint(light_grid.clusters_[cluster_index].num_point_lights);
	for (uint j = 0; j < num_point_lights; ++j) {
		const uint i = uint(light_grid.clusters_[cluster_index].point_lights[j]);

		// HACK: work around corrupted/stale cluster indexes
		// See https://github.com/w23/xash3d-fwgs/issues/730
		if (i >= lights.m.num_point_lights)
			continue;
#else
	for (uint i = 0; i < lights.m.num_point_lights; ++i) {
#endif

		const vec3 spotlight_dir = lights.m.point_lights[i].dir_stopdot2.xyz;
		const bool is_environment = (lights.m.point_lights[i].environment != 0);

		// TODO blue noise
		const vec2 rnd = vec2(rand01(), rand01());

		vec3 light_dir;
		vec3 color = lights.m.point_lights[i].color_stopdot.rgb;
		float light_dist = 0.;
		if (is_environment) {
			// Environment/directional light
			// FIXME extract, it is very different from other point/sphere/spotlights

			// Empirical? TODO WHY?
			// TODO move to native code
			color *= 2.;

			// TODO parametrize externally, via entity patches, etc
			const float sun_solid_angle = 6.794e-5; // Wikipedia
			const float cos_theta_max = 1. - sun_solid_angle / (2 * kPi);

			const vec3 dir_sample_z = sampleConeZ(rnd, cos_theta_max);
			light_dir = normalize(orthonormalBasisZ(-spotlight_dir) * dir_sample_z);

			// If light sample is below horizon, skip
			const float light_dot = dot(light_dir, N);
			if (light_dot < 1e-5)
				continue;
		} else {
			// Spherical lights
			const vec3 light_pos = lights.m.point_lights[i].origin_r2.xyz;
			const float light_r2 = lights.m.point_lights[i].origin_r2.w;

#ifdef DEBUG_VALIDATE_EXTRA
			if (IS_INVALID(light_r2) || light_r2 <= 0.) {
				debugPrintfEXT("light %d INVALID light_r2 = %f", i, light_r2);
			}
#endif

			//const vec3 ld = light_pos - P;
			light_dir = light_pos - P;
			const float light_dist2 = dot(light_dir, light_dir);

			// Light is too close, skip
			const float d2_minus_r2 = light_dist2 - light_r2;
			if (d2_minus_r2 <= 0.)
				continue;

			light_dist = sqrt(light_dist2);

			// Sample on the visible disc
			const float cos_theta_max = sqrt(d2_minus_r2) / light_dist;

#ifdef DEBUG_VALIDATE_EXTRA
		if (IS_INVALID(cos_theta_max) || cos_theta_max < 0. || cos_theta_max > 1.) {
			debugPrintfEXT("light.glsl:%d P=(%f,%f,%f) light_pos=(%f,%f,%f) light_dist2=%f light_r2=%f INVALID cos_theta_max=%f",
				__LINE__, PRIVEC3(P), PRIVEC3(light_pos), light_dist2, light_r2, cos_theta_max);
			continue;
		}
#endif

			const vec3 dir_sample_z = sampleConeZ(rnd, cos_theta_max);
			const mat3 basis = orthonormalBasisZ(light_dir / light_dist);
			light_dir = normalize(basis * dir_sample_z);
			//light_dir = normalize(light_dir);

#ifdef DEBUG_VALIDATE_EXTRA
			//DEBUG_VALIDATE_RANGE_VEC3("light.glsl", light_dir, -1., 1.);
			if (IS_INVALID3(light_dir) || any(lessThan(light_dir,vec3(-1.))) || any(greaterThan(light_dir,vec3(1.)))) { \
				/*debugPrintfEXT("ld=(%f,%f,%f), ldn=(%f,%f,%f); basis=((%f,%f,%f), (%f,%f,%f), (%f,%f,%f))",
					PRIVEC3(ld),
					PRIVEC3(normalize(ld)),
					PRIVEC3(basis[0]),
					PRIVEC3(basis[1]),
					PRIVEC3(basis[2]));*/
				debugPrintfEXT("light.glsl:%d cos_theta_max=%f light_dist=%f dir_sample_z=(%f,%f,%f) INVALID light_dir=(%f, %f, %f)",
					__LINE__, cos_theta_max, light_dist, PRIVEC3(dir_sample_z), PRIVEC3(light_dir));
			}
#endif

			// If light sample is below horizon, skip
			const float light_dot = dot(light_dir, N);
			if (light_dot < 1e-5)
				continue;

			float spot_attenuation = 1.;
			// Spotlights
			// Check for angles early
			// TODO split into separate spotlights and point lights arrays
			const float spot_dot = -dot(light_dir, spotlight_dir);
			const float stopdot2 = lights.m.point_lights[i].dir_stopdot2.a;
			if (spot_dot < stopdot2)
				continue;

			const float stopdot = lights.m.point_lights[i].color_stopdot.a;

			// For non-spotlighths stopdot will be -1.. spot_dot can never be less than that
			if (spot_dot < stopdot) {
				spot_attenuation = (spot_dot - stopdot2) / (stopdot - stopdot2);
#ifdef DEBUG_VALIDATE_EXTRA
				if (IS_INVALID(spot_attenuation)) {
					debugPrintfEXT("light.glsl:%d spot_dot=%f stopdot=%f stopdot2=%f INVALID spot_attenuation=%f",
						__LINE__, spot_dot, stopdot, stopdot2, spot_attenuation);
				}
#endif
				// Skip the rest of the computation for points completely outside of the light cone
				if (spot_attenuation <= 0.)
					continue;
			} // Spotlight

			// d2=4489108.500000 r2=1.000000 dist=2118.751465 spot_attenuation=0.092902 INVALID pdf=-316492608.000000
			// Therefore, need to clamp denom with max
			// TODO need better sampling right nao
			const float one_over_pdf = 2. * kPi * max(0., 1. - cos_theta_max) * spot_attenuation;
#ifdef DEBUG_VALIDATE_EXTRA
			if (IS_INVALID(one_over_pdf) || one_over_pdf < 0.) {
				debugPrintfEXT("light.glsl:%d light_dist2=%f light_r2=%f light_dist=%f spot_attenuation=%f INVALID one_over_pdf=%f",
					__LINE__, light_dist2, light_r2, light_dist, spot_attenuation, one_over_pdf);
			}
#endif

			color *= one_over_pdf;
		} // Sphere/spot lights

		vec3 ldiffuse, lspecular;
		evalSplitBRDF(N, light_dir, view_dir, material, ldiffuse, lspecular);
		ldiffuse *= color;
		lspecular *= color;

		// TODO does this make sense for diffuse-vs-specular bounce modes?
		const vec3 combined = ldiffuse + lspecular;
		if (dot(combined,combined) < color_culling_threshold)
			continue;

		if (is_environment) {
			if (shadowedSky(P, light_dir))
				continue;
		} else {
			if (shadowed(P, light_dir, light_dist + shadow_offset_fudge))
				continue;
		}

		diffuse += ldiffuse;
		specular += lspecular;
	} // for all lights
}
#endif

void computeLighting(vec3 P, vec3 N, vec3 view_dir, MaterialProperties material, out vec3 diffuse, out vec3 specular) {
	diffuse = specular = vec3(0.);

	const ivec3 light_cell = ivec3(floor(P / LIGHT_GRID_CELL_SIZE)) - lights.m.grid_min_cell;
	const uint cluster_index = uint(dot(light_cell, ivec3(1, lights.m.grid_size.x, lights.m.grid_size.x * lights.m.grid_size.y)));

#ifdef USE_CLUSTERS
	if (any(greaterThanEqual(light_cell, lights.m.grid_size)) || cluster_index >= MAX_LIGHT_CLUSTERS) {
#ifdef DEBUG_VALIDATE_EXTRA
		debugPrintfEXT("light_cell=(%d,%d,%d) OOB size=(%d, %d, %d)", PRIVEC3(light_cell), PRIVEC3(lights.m.grid_size));
#endif
		return; // vec3(1., 0., 0.);
	}
#endif

	// const uint cluster_offset = cluster_index * LIGHT_CLUSTER_SIZE + HACK_OFFSET;
	// const int num_dlights = int(light_grid.clusters_data[cluster_offset + LIGHT_CLUSTER_NUM_DLIGHTS_OFFSET]);
	// const int num_emissive_surfaces = int(light_grid.clusters_data[cluster_offset + LIGHT_CLUSTER_NUM_EMISSIVE_SURFACES_OFFSET]);
	// const uint emissive_surfaces_offset = cluster_offset + LIGHT_CLUSTER_EMISSIVE_SURFACES_DATA_OFFSET;
	//C = vec3(float(num_emissive_surfaces));

	//C = vec3(float(int(light_grid.clusters[cluster_index].num_emissive_surfaces)));
	//C += .3 * fract(vec3(light_cell) / 4.);

#if LIGHT_POLYGON
	sampleEmissiveSurfaces(P, N, view_dir, material, cluster_index, diffuse, specular);
#endif

#if LIGHT_POINT
	vec3 ldiffuse = vec3(0.), lspecular = vec3(0.);
	computePointLights(P, N, cluster_index, view_dir, material, ldiffuse, lspecular);
	diffuse += ldiffuse;
	specular += lspecular;
#endif

#ifdef DEBUG_VALIDATE_EXTRA
	if (IS_INVALID3(diffuse) || any(lessThan(diffuse,vec3(0.)))) {
		debugPrintfEXT("P=(%f,%f,%f) N=(%f,%f,%f) INVALID diffuse=(%f,%f,%f)",
			PRIVEC3(P), PRIVEC3(N), PRIVEC3(diffuse));
		diffuse = vec3(0.);
	}

	if (IS_INVALID3(specular) || any(lessThan(specular,vec3(0.)))) {
		debugPrintfEXT("P=(%f,%f,%f) N=(%f,%f,%f) INVALID specular=(%f,%f,%f)",
			PRIVEC3(P), PRIVEC3(N), PRIVEC3(specular));
		specular = vec3(0.);
	}
#endif
}
