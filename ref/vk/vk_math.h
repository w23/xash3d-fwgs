#pragma once

#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include <string.h>
#include "xash3d_mathlib.h"

void Matrix4x4_ToArrayFloatGL( const matrix4x4 in, float out[16] );
void Matrix4x4_FromArrayFloatGL( matrix4x4 out, const float in[16] );
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_ConcatScale( matrix4x4 out, float x );
void Matrix4x4_ConcatScale3( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateScale( matrix4x4 out, float x );
void Matrix4x4_CreateScale3( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateProjection(matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar);
void Matrix4x4_CreateOrtho(matrix4x4 m, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar);
void Matrix4x4_CreateModelview( matrix4x4 out );

void computeTangentE(vec3_t out_tangent, const vec3_t e1, const vec3_t e2, const vec2_t uv0, const vec2_t uv1, const vec2_t uv2);
void computeTangent(vec3_t out_tangent, const vec3_t v0, const vec3_t v1, const vec3_t v2, const vec2_t uv0, const vec2_t uv1, const vec2_t uv2);

void Matrix4x4_CreateFromVectors(matrix4x4 out, const vec3_t right, const vec3_t up, const vec3_t z, const vec3_t translate);

void computeNormal(vec3_t p0, vec3_t p1, vec3_t p2, vec3_t out_normal);
