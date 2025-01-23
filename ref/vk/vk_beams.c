#include "vk_beams.h"
#include "vk_common.h"
#include "camera.h"
#include "vk_render.h"
#include "vk_geometry.h"
#include "r_textures.h"
#include "vk_sprite.h"
#include "vk_scene.h"
#include "vk_math.h"
#include "vk_triapi.h"
#include "r_speeds.h"

#include "xash3d_types.h"
#include "xash3d_mathlib.h"
#include "customentity.h"
#include "beamdef.h"

#define NOISE_DIVISIONS	64	// don't touch - many tripmines cause the crash when it equal 128
#define MODULE_NAME "beams"

typedef struct
{
	vec3_t	pos;
	float	texcoord;	// Y texture coordinate
	float	width;
} beamseg_t;

static struct {
	struct {
		int beams;
	} stats;
} g_beam;

qboolean R_BeamInit(void) {
	R_SPEEDS_COUNTER(g_beam.stats.beams, "count", kSpeedsMetricCount);
	return true;
}

/*
==============================================================

FRACTAL NOISE

==============================================================
*/
static float	rgNoise[NOISE_DIVISIONS+1];	// global noise array

// freq2 += step * 0.1;
// Fractal noise generator, power of 2 wavelength
static void FracNoise( float *noise, int divs )
{
	int	div2;

	div2 = divs >> 1;
	if( divs < 2 ) return;

	// noise is normalized to +/- scale
	noise[div2] = ( noise[0] + noise[divs] ) * 0.5f + divs * gEngine.COM_RandomFloat( -0.125f, 0.125f );

	if( div2 > 1 )
	{
		FracNoise( &noise[div2], div2 );
		FracNoise( noise, div2 );
	}
}

static void SineNoise( float *noise, int divs )
{
	float	freq = 0;
	float	step = M_PI_F / (float)divs;
	int	i;

	for( i = 0; i < divs; i++ )
	{
		noise[i] = sin( freq );
		freq += step;
	}
}


/*
==============================================================

BEAM MATHLIB

==============================================================
*/
static void R_BeamComputePerpendicular( const vec3_t vecBeamDelta, vec3_t pPerp )
{
	// direction in worldspace of the center of the beam
	vec3_t	vecBeamCenter;

	VectorNormalize2( vecBeamDelta, vecBeamCenter );
	CrossProduct( g_camera.vforward, vecBeamCenter, pPerp );
	VectorNormalize( pPerp );
}

static void R_BeamComputeNormal( const vec3_t vStartPos, const vec3_t vNextPos, vec3_t pNormal )
{
	// vTangentY = line vector for beam
	vec3_t	vTangentY, vDirToBeam;

	VectorSubtract( vStartPos, vNextPos, vTangentY );

	// vDirToBeam = vector from viewer origin to beam
	VectorSubtract( vStartPos, g_camera.vieworg, vDirToBeam );

	// get a vector that is perpendicular to us and perpendicular to the beam.
	// this is used to fatten the beam.
	CrossProduct( vTangentY, vDirToBeam, pNormal );
	VectorNormalizeFast( pNormal );
}


/*
==============
R_BeamCull

Cull the beam by bbox
==============
*/
qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly )
{
	vec3_t	mins, maxs;
	int	i;

	for( i = 0; i < 3; i++ )
	{
		if( start[i] < end[i] )
		{
			mins[i] = start[i];
			maxs[i] = end[i];
		}
		else
		{
			mins[i] = end[i];
			maxs[i] = start[i];
		}

		// don't let it be zero sized
		if( mins[i] == maxs[i] )
			maxs[i] += 1.0f;
	}

	/* FIXME VK
	// check bbox
	if( gEngine.Mod_BoxVisible( mins, maxs, Mod_GetCurrentVis( )))
	{
		if( pvsOnly || !R_CullBox( mins, maxs ))
		{
			// beam is visible
			return false;
		}
	}
	*/
	return false;

	// beam is culled
	return true;
}

/*
static float clampf(float v, float min, float max) {
	if (v < min) return min;
	if (v > max) return max;
	return v;
}

// FIXME unclear how to organize this
static void applyBrightness( float brightness, rgba_t out ) {
	out[0] = out[1] = out[2] = clampf(brightness, 0, 1) * 255.f;
	out[3] = 255;
}
*/

static void TriBrightness( float brightness ) {
	TriColor4f( brightness, brightness, brightness, 1.f );
}

static void R_DrawSegs( vec3_t source, vec3_t delta, float width, float scale, float freq, float speed, int segments, int flags, const vec4_t color )
{
	int	noiseIndex, noiseStep;
	int	i, total_segs, segs_drawn;
	float	div, length, fraction, factor;
	float	flMaxWidth, vLast, vStep, brightness;
	vec3_t	perp1, vLastNormal;
	beamseg_t	curSeg;

	if( segments < 2 ) return;

	length = VectorLength( delta );
	flMaxWidth = width * 0.5f;
	div = 1.0f / ( segments - 1 );

	if( length * div < flMaxWidth * 1.414f )
	{
		// here, we have too many segments; we could get overlap... so lets have less segments
		segments = (int)( length / ( flMaxWidth * 1.414f )) + 1.0f;
		if( segments < 2 ) segments = 2;
	}

	if( segments > NOISE_DIVISIONS )
		segments = NOISE_DIVISIONS;

	div = 1.0f / (segments - 1);
	length *= 0.01f;
	vStep = length * div;	// Texture length texels per space pixel

	// Scroll speed 3.5 -- initial texture position, scrolls 3.5/sec (1.0 is entire texture)
	vLast = fmod( freq * speed, 1 );

	if( flags & FBEAM_SINENOISE )
	{
		if( segments < 16 )
		{
			segments = 16;
			div = 1.0f / ( segments - 1 );
		}
		scale *= 100.0f;
		length = segments * 0.1f;
	}
	else
	{
		scale *= length * 2.0f;
	}

	// Iterator to resample noise waveform (it needs to be generated in powers of 2)
	noiseStep = (int)((float)( NOISE_DIVISIONS - 1 ) * div * 65536.0f );
	brightness = 1.0f;
	noiseIndex = 0;

	if( FBitSet( flags, FBEAM_SHADEIN ))
		brightness = 0;

	// Choose two vectors that are perpendicular to the beam
	R_BeamComputePerpendicular( delta, perp1 );

	total_segs = segments;
	segs_drawn = 0;

	TriBegin( TRI_TRIANGLE_STRIP );

	// specify all the segments.
	for( i = 0; i < segments; i++ )
	{
		beamseg_t	nextSeg;
		vec3_t	vPoint1, vPoint2;

		ASSERT( noiseIndex < ( NOISE_DIVISIONS << 16 ));

		fraction = i * div;

		VectorMA( source, fraction, delta, nextSeg.pos );

		// distort using noise
		if( scale != 0 )
		{
			factor = rgNoise[noiseIndex>>16] * scale;

			if( FBitSet( flags, FBEAM_SINENOISE ))
			{
				float	s, c;

				SinCos( fraction * M_PI_F * length + freq, &s, &c );
				VectorMA( nextSeg.pos, (factor * s), g_camera.vup, nextSeg.pos );

				// rotate the noise along the perpendicluar axis a bit to keep the bolt from looking diagonal
				VectorMA( nextSeg.pos, (factor * c), g_camera.vright, nextSeg.pos );
			}
			else
			{
				VectorMA( nextSeg.pos, factor, perp1, nextSeg.pos );
			}
		}

		// specify the next segment.
		nextSeg.width = width * 2.0f;
		nextSeg.texcoord = vLast;

		if( segs_drawn > 0 )
		{
			// Get a vector that is perpendicular to us and perpendicular to the beam.
			// This is used to fatten the beam.
			vec3_t	vNormal, vAveNormal;

			R_BeamComputeNormal( curSeg.pos, nextSeg.pos, vNormal );

			if( segs_drawn > 1 )
			{
				// Average this with the previous normal
				VectorAdd( vNormal, vLastNormal, vAveNormal );
				VectorScale( vAveNormal, 0.5f, vAveNormal );
				VectorNormalizeFast( vAveNormal );
			}
			else
			{
				VectorCopy( vNormal, vAveNormal );
			}

			VectorCopy( vNormal, vLastNormal );

			// draw regular segment
			VectorMA( curSeg.pos, ( curSeg.width * 0.5f ), vAveNormal, vPoint1 );
			VectorMA( curSeg.pos, (-curSeg.width * 0.5f ), vAveNormal, vPoint2 );

			TriTexCoord2f( 0.0f, curSeg.texcoord );
			TriBrightness( brightness );
			TriNormal3fv( vAveNormal );
			TriVertex3fv( vPoint1 );

			TriTexCoord2f( 1.0f, curSeg.texcoord );
			TriBrightness( brightness );
			TriNormal3fv( vAveNormal );
			TriVertex3fv( vPoint2 );
		}

		curSeg = nextSeg;
		segs_drawn++;

		if( FBitSet( flags, FBEAM_SHADEIN ) && FBitSet( flags, FBEAM_SHADEOUT ))
		{
			if( fraction < 0.5f ) brightness = fraction;
			else brightness = ( 1.0f - fraction );
		}
		else if( FBitSet( flags, FBEAM_SHADEIN ))
		{
			brightness = fraction;
		}
		else if( FBitSet( flags, FBEAM_SHADEOUT ))
		{
			brightness = 1.0f - fraction;
		}

		if( segs_drawn == total_segs )
		{
			// draw the last segment
			VectorMA( curSeg.pos, ( curSeg.width * 0.5f ), vLastNormal, vPoint1 );
			VectorMA( curSeg.pos, (-curSeg.width * 0.5f ), vLastNormal, vPoint2 );

			// specify the points.
			TriTexCoord2f( 0.0f, curSeg.texcoord );
			TriBrightness( brightness );
			TriNormal3fv( vLastNormal );
			TriVertex3fv( vPoint1 );

			TriTexCoord2f( 1.0f, curSeg.texcoord );
			TriBrightness( brightness );
			TriNormal3fv( vLastNormal );
			TriVertex3fv( vPoint2 );
		}

		vLast += vStep; // Advance texture scroll (v axis only)
		noiseIndex += noiseStep;
	}

	TriEndEx(color, "beam segs");
}

static void R_DrawTorus( vec3_t source, vec3_t delta, float width, float scale, float freq, float speed, int segments, const vec4_t color )
{
	int	i, noiseIndex, noiseStep;
	float	div, length, fraction, factor, vLast, vStep;
	vec3_t	last1, last2, point, screen, screenLast, tmp, normal;

	if( segments < 2 )
		return;

	if( segments > NOISE_DIVISIONS )
		segments = NOISE_DIVISIONS;

	length = VectorLength( delta ) * 0.01f;
	if( length < 0.5f ) length = 0.5f; // don't lose all of the noise/texture on short beams

	div = 1.0f / (segments - 1);

	vStep = length * div; // Texture length texels per space pixel

	// Scroll speed 3.5 -- initial texture position, scrolls 3.5/sec (1.0 is entire texture)
	vLast = fmod( freq * speed, 1 );
	scale = scale * length;

	// Iterator to resample noise waveform (it needs to be generated in powers of 2)
	noiseStep = (int)((float)( NOISE_DIVISIONS - 1 ) * div * 65536.0f );
	noiseIndex = 0;

	TriBegin( TRI_TRIANGLE_STRIP );

	for( i = 0; i < segments; i++ )
	{
		float	s, c;

		fraction = i * div;
		SinCos( fraction * M_PI2_F, &s, &c );

		point[0] = s * freq * delta[2] + source[0];
		point[1] = c * freq * delta[2] + source[1];
		point[2] = source[2];

		// distort using noise
		if( scale != 0 )
		{
			if(( noiseIndex >> 16 ) < NOISE_DIVISIONS )
			{
				factor = rgNoise[noiseIndex>>16] * scale;
				VectorMA( point, factor, g_camera.vup, point );

				// rotate the noise along the perpendicluar axis a bit to keep the bolt from looking diagonal
				factor = rgNoise[noiseIndex>>16] * scale * cos( fraction * M_PI_F * 3 + freq );
				VectorMA( point, factor, g_camera.vright, point );
			}
		}

		// Transform point into screen space
		TriWorldToScreen( point, screen );

		if( i != 0 )
		{
			// build world-space normal to screen-space direction vector
			VectorSubtract( screen, screenLast, tmp );

			// we don't need Z, we're in screen space
			tmp[2] = 0;
			VectorNormalize( tmp );
			VectorScale( g_camera.vup, -tmp[0], normal );	// Build point along noraml line (normal is -y, x)
			VectorMA( normal, tmp[1], g_camera.vright, normal );

			// Make a wide line
			VectorMA( point, width, normal, last1 );
			VectorMA( point, -width, normal, last2 );

			vLast += vStep; // advance texture scroll (v axis only)
			TriTexCoord2f( 1, vLast );
			TriVertex3fv( last2 );
			TriTexCoord2f( 0, vLast );
			TriVertex3fv( last1 );
		}

		VectorCopy( screen, screenLast );
		noiseIndex += noiseStep;
	}

	TriEndEx(color, "beam torus");
}

static void R_DrawDisk( vec3_t source, vec3_t delta, float width, float scale, float freq, float speed, int segments, const vec4_t color )
{
	float	div, length, fraction;
	float	w, vLast, vStep;
	vec3_t	point;
	int	i;

	if( segments < 2 ) return;

	if( segments > NOISE_DIVISIONS )		// UNDONE: Allow more segments?
		segments = NOISE_DIVISIONS;

	length = VectorLength( delta ) * 0.01f;
	if( length < 0.5f ) length = 0.5f;	// don't lose all of the noise/texture on short beams

	div = 1.0f / (segments - 1);
	vStep = length * div;		// Texture length texels per space pixel

	// scroll speed 3.5 -- initial texture position, scrolls 3.5/sec (1.0 is entire texture)
	vLast = fmod( freq * speed, 1 );
	scale = scale * length;

	// clamp the beam width
	w = fmod( freq, width * 0.1f ) * delta[2];

	TriBegin( TRI_TRIANGLE_STRIP );

	// NOTE: we must force the degenerate triangles to be on the edge
	for( i = 0; i < segments; i++ )
	{
		float	s, c;

		fraction = i * div;
		VectorCopy( source, point );

		TriBrightness( 1.0f );
		TriTexCoord2f( 1.0f, vLast );
		TriVertex3fv( point );

		SinCos( fraction * M_PI2_F, &s, &c );
		point[0] = s * w + source[0];
		point[1] = c * w + source[1];
		point[2] = source[2];

		TriBrightness( 1.0f );
		TriTexCoord2f( 0.0f, vLast );
		TriVertex3fv( point );

		vLast += vStep;	// advance texture scroll (v axis only)
	}

	TriEndEx(color, "beam disk");
}

static void R_DrawCylinder( vec3_t source, vec3_t delta, float width, float scale, float freq, float speed, int segments, const vec4_t color )
{
	float	div, length, fraction;
	float	vLast, vStep;
	vec3_t	point;
	int	i;

	if( segments < 2 )
		return;

	if( segments > NOISE_DIVISIONS )
		segments = NOISE_DIVISIONS;

	length = VectorLength( delta ) * 0.01f;
	if( length < 0.5f ) length = 0.5f;	// don't lose all of the noise/texture on short beams

	div = 1.0f / (segments - 1);
	vStep = length * div;		// texture length texels per space pixel

	// Scroll speed 3.5 -- initial texture position, scrolls 3.5/sec (1.0 is entire texture)
	vLast = fmod( freq * speed, 1 );
	scale = scale * length;

	TriBegin( TRI_TRIANGLE_STRIP );
	for ( i = 0; i < segments; i++ )
	{
		float	s, c;

		fraction = i * div;
		SinCos( fraction * M_PI2_F, &s, &c );

		point[0] = s * freq * delta[2] + source[0];
		point[1] = c * freq * delta[2] + source[1];
		point[2] = source[2] + width;

		TriBrightness( 0 );
		TriTexCoord2f( 1, vLast );
		TriVertex3fv( point );

		point[0] = s * freq * ( delta[2] + width ) + source[0];
		point[1] = c * freq * ( delta[2] + width ) + source[1];
		point[2] = source[2] - width;

		TriBrightness( 1 );
		TriTexCoord2f( 0, vLast );
		TriVertex3fv( point );

		vLast += vStep;	// Advance texture scroll (v axis only)
	}
	TriEndEx(color, "beam cylinder");
}

static void R_DrawBeamFollow( BEAM *pbeam, float frametime, const vec4_t color )
{
	particle_t	*pnew, *particles;
	float		fraction, div, vLast, vStep;
	vec3_t		last1, last2, tmp, screen;
	vec3_t		delta, screenLast, normal;

	gEngine.R_FreeDeadParticles( &pbeam->particles );

	particles = pbeam->particles;
	pnew = NULL;

	div = 0;
	if( FBitSet( pbeam->flags, FBEAM_STARTENTITY ))
	{
		if( particles )
		{
			VectorSubtract( particles->org, pbeam->source, delta );
			div = VectorLength( delta );

			if( div >= 32 )
			{
				pnew = gEngine.CL_AllocParticleFast();
			}
		}
		else
		{
			pnew = gEngine.CL_AllocParticleFast();
		}
	}

	if( pnew )
	{
		VectorCopy( pbeam->source, pnew->org );
		pnew->die = gpGlobals->time + pbeam->amplitude;
		VectorClear( pnew->vel );

		pnew->next = particles;
		pbeam->particles = pnew;
		particles = pnew;
	}

	// nothing to draw
	if( !particles ) return;

	if( !pnew && div != 0 )
	{
		VectorCopy( pbeam->source, delta );
		TriWorldToScreen( pbeam->source, screenLast );
		TriWorldToScreen( particles->org, screen );
	}
	else if( particles && particles->next )
	{
		VectorCopy( particles->org, delta );
		TriWorldToScreen( particles->org, screenLast );
		TriWorldToScreen( particles->next->org, screen );
		particles = particles->next;
	}
	else
	{
		return;
	}

	// UNDONE: This won't work, screen and screenLast must be extrapolated here to fix the
	// first beam segment for this trail

	// build world-space normal to screen-space direction vector
	VectorSubtract( screen, screenLast, tmp );
	// we don't need Z, we're in screen space
	tmp[2] = 0;
	VectorNormalize( tmp );

	// Build point along noraml line (normal is -y, x)
	VectorScale( g_camera.vup, tmp[0], normal );	// Build point along normal line (normal is -y, x)
	VectorMA( normal, tmp[1], g_camera.vright, normal );

	// Make a wide line
	VectorMA( delta, pbeam->width, normal, last1 );
	VectorMA( delta, -pbeam->width, normal, last2 );

	div = 1.0f / pbeam->amplitude;
	fraction = ( pbeam->die - gpGlobals->time ) * div;

	vLast = 0.0f;
	vStep = 1.0f;

	TriBegin( TRI_QUADS );
	while( particles )
	{
		TriBrightness( fraction );
		TriTexCoord2f( 1, 1 );
		TriVertex3fv( last2 );
		TriBrightness( fraction );
		TriTexCoord2f( 0, 1 );
		TriVertex3fv( last1 );

		// Transform point into screen space
		TriWorldToScreen( particles->org, screen );
		// Build world-space normal to screen-space direction vector
		VectorSubtract( screen, screenLast, tmp );

		// we don't need Z, we're in screen space
		tmp[2] = 0;
		VectorNormalize( tmp );
		VectorScale( g_camera.vup, tmp[0], normal );	// Build point along noraml line (normal is -y, x)
		VectorMA( normal, tmp[1], g_camera.vright, normal );

		// Make a wide line
		VectorMA( particles->org, pbeam->width, normal, last1 );
		VectorMA( particles->org, -pbeam->width, normal, last2 );

		vLast += vStep;	// Advance texture scroll (v axis only)

		if( particles->next != NULL )
		{
			fraction = (particles->die - gpGlobals->time) * div;
		}
		else
		{
			fraction = 0.0;
		}

		TriBrightness( fraction );
		TriTexCoord2f( 0, 0 );
		TriVertex3fv( last1 );
		TriBrightness( fraction );
		TriTexCoord2f( 1, 0 );
		TriVertex3fv( last2 );

		VectorCopy( screen, screenLast );

		particles = particles->next;
	}

	// drift popcorn trail if there is a velocity
	particles = pbeam->particles;

	while( particles )
	{
		VectorMA( particles->org, frametime, particles->vel, particles->org );
		particles = particles->next;
	}

	TriEndEx(color, "beam follow");
}

static void R_DrawRing( vec3_t source, vec3_t delta, float width, float amplitude, float freq, float speed, int segments, const vec4_t color )
{
	int	i, j, noiseIndex, noiseStep;
	float	div, length, fraction, factor, vLast, vStep;
	vec3_t	last1, last2, point, screen, screenLast;
	vec3_t	tmp, normal, center, xaxis, yaxis;
	float	radius, x, y, scale;

	if( segments < 2 )
		return;

	VectorClear( screenLast );
	segments = segments * M_PI_F;

	if( segments > NOISE_DIVISIONS * 8 )
		segments = NOISE_DIVISIONS * 8;

	length = VectorLength( delta ) * 0.01f * M_PI_F;
	if( length < 0.5f ) length = 0.5f;		// Don't lose all of the noise/texture on short beams

	div = 1.0f / ( segments - 1 );

	vStep = length * div / 8.0f;			// texture length texels per space pixel

	// Scroll speed 3.5 -- initial texture position, scrolls 3.5/sec (1.0 is entire texture)
	vLast = fmod( freq * speed, 1.0f );
	scale = amplitude * length / 8.0f;

	// Iterator to resample noise waveform (it needs to be generated in powers of 2)
	noiseStep = (int)((float)( NOISE_DIVISIONS - 1 ) * div * 65536.0f ) * 8;
	noiseIndex = 0;

	VectorScale( delta, 0.5f, delta );
	VectorAdd( source, delta, center );

	VectorCopy( delta, xaxis );
	radius = VectorLength( xaxis );

	// cull beamring
	// --------------------------------
	// Compute box center +/- radius
	VectorSet( last1, radius, radius, scale );
	VectorAdd( center, last1, tmp );		// maxs
	VectorSubtract( center, last1, screen );	// mins

	/* FIXME VK
	if( !WORLDMODEL )
		return;

	// is that box in PVS && frustum?
	if( !gEngine.Mod_BoxVisible( screen, tmp, Mod_GetCurrentVis( )) || R_CullBox( screen, tmp ))
	{
		return;
	}
	*/

	VectorSet( yaxis, xaxis[1], -xaxis[0], 0.0f );
	VectorNormalize( yaxis );
	VectorScale( yaxis, radius, yaxis );

	j = segments / 8;

	TriBegin( TRI_TRIANGLE_STRIP );
	for( i = 0; i < segments + 1; i++ )
	{
		fraction = i * div;
		SinCos( fraction * M_PI2_F, &x, &y );

		VectorMAMAM( x, xaxis, y, yaxis, 1.0f, center, point );

		// distort using noise
		factor = rgNoise[(noiseIndex >> 16) & (NOISE_DIVISIONS - 1)] * scale;
		VectorMA( point, factor, g_camera.vup, point );

		// Rotate the noise along the perpendicluar axis a bit to keep the bolt from looking diagonal
		factor = rgNoise[(noiseIndex >> 16) & (NOISE_DIVISIONS - 1)] * scale;
		factor *= cos( fraction * M_PI_F * 24 + freq );
		VectorMA( point, factor, g_camera.vright, point );

		// Transform point into screen space
		TriWorldToScreen( point, screen );

		if( i != 0 )
		{
			// build world-space normal to screen-space direction vector
			VectorSubtract( screen, screenLast, tmp );

			// we don't need Z, we're in screen space
			tmp[2] = 0;
			VectorNormalize( tmp );

			// Build point along normal line (normal is -y, x)
			VectorScale( g_camera.vup, tmp[0], normal );
			VectorMA( normal, tmp[1], g_camera.vright, normal );

			// Make a wide line
			VectorMA( point, width, normal, last1 );
			VectorMA( point, -width, normal, last2 );

			vLast += vStep;	// Advance texture scroll (v axis only)
			TriTexCoord2f( 1.0f, vLast );
			TriVertex3fv( last2 );
			TriTexCoord2f( 0.0f, vLast );
			TriVertex3fv( last1 );
		}

		VectorCopy( screen, screenLast );
		noiseIndex += noiseStep;
		j--;

		if( j == 0 && amplitude != 0 )
		{
			j = segments / 8;
			FracNoise( rgNoise, NOISE_DIVISIONS );
		}
	}
	TriEndEx( color, "beam ring" );
}

/*
==============
R_BeamComputePoint

compute attachment point for beam
==============
*/
static qboolean R_BeamComputePoint( int beamEnt, vec3_t pt )
{
	cl_entity_t	*ent;
	int		attach;

	ent = gEngine.R_BeamGetEntity( beamEnt );

	if( beamEnt < 0 )
		attach = BEAMENT_ATTACHMENT( -beamEnt );
	else attach = BEAMENT_ATTACHMENT( beamEnt );

	if( !ent )
	{
		gEngine.Con_DPrintf( S_ERROR "R_BeamComputePoint: invalid entity %i\n", BEAMENT_ENTITY( beamEnt ));
		VectorClear( pt );
		return false;
	}

	// get attachment
	if( attach > 0 )
		VectorCopy( ent->attachment[attach - 1], pt );
	else if( ent->index == gEngine.EngineGetParm( PARM_PLAYER_INDEX, 0 ) )
	{
		vec3_t simorg;
		gEngine.GetPredictedOrigin( simorg );
		VectorCopy( simorg, pt );
	}
	else VectorCopy( ent->origin, pt );

	return true;
}

/*
==============
R_BeamRecomputeEndpoints

Recomputes beam endpoints..
==============
*/
static qboolean R_BeamRecomputeEndpoints( BEAM *pbeam )
{
	if( FBitSet( pbeam->flags, FBEAM_STARTENTITY ))
	{
		cl_entity_t *start = gEngine.R_BeamGetEntity( pbeam->startEntity );

		if( R_BeamComputePoint( pbeam->startEntity, pbeam->source ))
		{
			if( !pbeam->pFollowModel )
				pbeam->pFollowModel = start->model;
			SetBits( pbeam->flags, FBEAM_STARTVISIBLE );
		}
		else if( !FBitSet( pbeam->flags, FBEAM_FOREVER ))
		{
			ClearBits( pbeam->flags, FBEAM_STARTENTITY );
		}
	}

	if( FBitSet( pbeam->flags, FBEAM_ENDENTITY ))
	{
		cl_entity_t *end = gEngine.R_BeamGetEntity( pbeam->endEntity );

		if( R_BeamComputePoint( pbeam->endEntity, pbeam->target ))
		{
			if( !pbeam->pFollowModel )
				pbeam->pFollowModel = end->model;
			SetBits( pbeam->flags, FBEAM_ENDVISIBLE );
		}
		else if( !FBitSet( pbeam->flags, FBEAM_FOREVER ))
		{
			ClearBits( pbeam->flags, FBEAM_ENDENTITY );
			pbeam->die = gpGlobals->time;
			return false;
		}
		else
		{
			return false;
		}
	}

	if( FBitSet( pbeam->flags, FBEAM_STARTENTITY ) && !FBitSet( pbeam->flags, FBEAM_STARTVISIBLE ))
		return false;
	return true;
}


/*
==============
R_BeamDraw

Update beam vars and draw it
==============
*/
void R_BeamDraw( BEAM *pbeam, float frametime )
{
	model_t	*model;
	vec3_t	delta;
	vec4_t color;
	int render_mode;
	int	texturenum;

	model = gEngine.pfnGetModelByIndex( pbeam->modelIndex );
	SetBits( pbeam->flags, FBEAM_ISACTIVE );

	if( !model || model->type != mod_sprite )
	{
		pbeam->flags &= ~FBEAM_ISACTIVE; // force to ignore
		pbeam->die = gpGlobals->time;
		return;
	}

	++g_beam.stats.beams;

	// update frequency
	pbeam->freq += frametime;

	// generate fractal noise
	if( frametime != 0.0f )
	{
		rgNoise[0] = 0;
		rgNoise[NOISE_DIVISIONS] = 0;
	}

	if( pbeam->amplitude != 0 && frametime != 0.0f )
	{
		if( FBitSet( pbeam->flags, FBEAM_SINENOISE ))
			SineNoise( rgNoise, NOISE_DIVISIONS );
		else FracNoise( rgNoise, NOISE_DIVISIONS );
	}

	// update end points
	if( FBitSet( pbeam->flags, FBEAM_STARTENTITY|FBEAM_ENDENTITY ))
	{
		// makes sure attachment[0] + attachment[1] are valid
		if( !R_BeamRecomputeEndpoints( pbeam ))
		{
			ClearBits( pbeam->flags, FBEAM_ISACTIVE ); // force to ignore
			return;
		}

		// compute segments from the new endpoints
		VectorSubtract( pbeam->target, pbeam->source, delta );
		VectorClear( pbeam->delta );

		if( VectorLength( delta ) > 0.0000001f )
			VectorCopy( delta, pbeam->delta );

		if( pbeam->amplitude >= 0.50f )
			pbeam->segments = VectorLength( pbeam->delta ) * 0.25f + 3.0f; // one per 4 pixels
		else pbeam->segments = VectorLength( pbeam->delta ) * 0.075f + 3.0f; // one per 16 pixels
	}

	if( pbeam->type == TE_BEAMPOINTS && R_BeamCull( pbeam->source, pbeam->target, 0 ))
	{
		ClearBits( pbeam->flags, FBEAM_ISACTIVE );
		return;
	}

	// don't draw really short or inactive beams
	if( !FBitSet( pbeam->flags, FBEAM_ISACTIVE ) || VectorLength( pbeam->delta ) < 0.1f )
	{
		return;
	}

	if( pbeam->flags & ( FBEAM_FADEIN|FBEAM_FADEOUT ))
	{
		// update life cycle
		pbeam->t = pbeam->freq + ( pbeam->die - gpGlobals->time );
		if( pbeam->t != 0.0f ) pbeam->t = 1.0f - pbeam->freq / pbeam->t;
	}

	if( pbeam->type == TE_BEAMHOSE )
	{
		float	flDot;

		VectorSubtract( pbeam->target, pbeam->source, delta );
		VectorNormalize( delta );

		flDot = DotProduct( delta, g_camera.vforward );

		// abort if the player's looking along it away from the source
		if( flDot > 0 )
		{
			return;
		}
		else
		{
			float	flFade = pow( flDot, 10 );
			vec3_t	localDir, vecProjection, tmp;
			float	flDistance;

			// fade the beam if the player's not looking at the source
			VectorSubtract( g_camera.vieworg, pbeam->source, localDir );
			flDot = DotProduct( delta, localDir );
			VectorScale( delta, flDot, vecProjection );
			VectorSubtract( localDir, vecProjection, tmp );
			flDistance = VectorLength( tmp );

			if( flDistance > 30 )
			{
				flDistance = 1.0f - (( flDistance - 30.0f ) / 64.0f );
				if( flDistance <= 0 ) flFade = 0;
				else flFade *= pow( flDistance, 3 );
			}

			if( flFade < ( 1.0f / 255.0f ))
				return;

			// FIXME: needs to be testing
			pbeam->brightness *= flFade;
		}
	}

	render_mode = FBitSet( pbeam->flags, FBEAM_SOLID ) ? kRenderNormal : kRenderTransAdd;

	texturenum = R_GetSpriteTexture( model, (int)(pbeam->frame + pbeam->frameRate * gpGlobals->time) % pbeam->frameCount);
	if( texturenum <= 0 ) // FIXME VK || texturenum > MAX_TEXTURES )
	{
		ClearBits( pbeam->flags, FBEAM_ISACTIVE );
		return;
	}

	if( pbeam->type == TE_BEAMFOLLOW )
	{
		cl_entity_t	*pStart;

		// XASH SPECIFIC: get brightness from head entity
		pStart = gEngine.R_BeamGetEntity( pbeam->startEntity );
		if( pStart && pStart->curstate.rendermode != kRenderNormal )
			pbeam->brightness = CL_FxBlend( pStart ) / 255.0f;
	}

	color[0] = pbeam->r;
	color[1] = pbeam->g;
	color[2] = pbeam->b;
	if( FBitSet( pbeam->flags, FBEAM_FADEIN ))
		color[3] = pbeam->t * pbeam->brightness;
	else if( FBitSet( pbeam->flags, FBEAM_FADEOUT ))
		color[3] = (1.f - pbeam->t) * pbeam->brightness;
	else
		color[3] = pbeam->brightness;

	// TODO gl renderer has per-vertex color that is updated using brightness and whatever
	VK_RenderDebugLabelBegin( "beam" );

	TriSetTexture( texturenum );
	TriRenderMode( render_mode );
	TriColor4f( color[0], color[1], color[2], color[3] );

	switch( pbeam->type )
	{
	case TE_BEAMTORUS:
		// FIXME VK GL_Cull( GL_NONE );
		R_DrawTorus( pbeam->source, pbeam->delta, pbeam->width, pbeam->amplitude, pbeam->freq, pbeam->speed, pbeam->segments, color );
		break;
	case TE_BEAMDISK:
		// FIXME VK GL_Cull( GL_NONE );
		R_DrawDisk( pbeam->source, pbeam->delta, pbeam->width, pbeam->amplitude, pbeam->freq, pbeam->speed, pbeam->segments, color );
		break;
	case TE_BEAMCYLINDER:
		// FIXME VK GL_Cull( GL_NONE );
		R_DrawCylinder( pbeam->source, pbeam->delta, pbeam->width, pbeam->amplitude, pbeam->freq, pbeam->speed, pbeam->segments, color );
		break;
	case TE_BEAMPOINTS:
	case TE_BEAMHOSE:
		R_DrawSegs( pbeam->source, pbeam->delta, pbeam->width, pbeam->amplitude, pbeam->freq, pbeam->speed, pbeam->segments, pbeam->flags, color );
		break;
	case TE_BEAMFOLLOW:
		R_DrawBeamFollow( pbeam, frametime, color );
		break;
	case TE_BEAMRING:
		// FIXME VK GL_Cull( GL_NONE );
		R_DrawRing( pbeam->source, pbeam->delta, pbeam->width, pbeam->amplitude, pbeam->freq, pbeam->speed, pbeam->segments, color );
		break;
	}

	VK_RenderDebugLabelEnd();

	// FIXME VK r_stats.c_view_beams_count++;
}

/*
==============
R_BeamSetAttributes

set beam attributes
==============
*/
static void R_BeamSetAttributes( BEAM *pbeam, float r, float g, float b, float framerate, int startFrame )
{
	pbeam->frame = (float)startFrame;
	pbeam->frameRate = framerate;
	pbeam->r = r;
	pbeam->g = g;
	pbeam->b = b;
}

/*
==============
R_BeamSetup

generic function. all beams must be
passed through this
==============
*/
static void R_BeamSetup( BEAM *pbeam, vec3_t start, vec3_t end, int modelIndex, float life, float width, float amplitude, float brightness, float speed )
{
	model_t	*sprite = gEngine.pfnGetModelByIndex( modelIndex );

	if( !sprite ) return;

	pbeam->type = BEAM_POINTS;
	pbeam->modelIndex = modelIndex;
	pbeam->frame = 0;
	pbeam->frameRate = 0;
	pbeam->frameCount = sprite->numframes;

	VectorCopy( start, pbeam->source );
	VectorCopy( end, pbeam->target );
	VectorSubtract( end, start, pbeam->delta );

	pbeam->freq = speed * gpGlobals->time;
	pbeam->die = life + gpGlobals->time;
	pbeam->amplitude = amplitude;
	pbeam->brightness = brightness;
	pbeam->width = width;
	pbeam->speed = speed;

	if( amplitude >= 0.50f )
		pbeam->segments = VectorLength( pbeam->delta ) * 0.25f + 3.0f;	// one per 4 pixels
	else pbeam->segments = VectorLength( pbeam->delta ) * 0.075f + 3.0f;		// one per 16 pixels

	pbeam->pFollowModel = NULL;
	pbeam->flags = 0;
}

/*
==============
R_BeamDrawCustomEntity

initialize beam from server entity
==============
*/
void R_BeamDrawCustomEntity( cl_entity_t *ent, float frametime )
{
	BEAM	beam;
	float	amp = ent->curstate.body / 100.0f;
	float	blend = CL_FxBlend( ent ) / 255.0f;
	float	r, g, b;
	int	beamFlags;

	r = ent->curstate.rendercolor.r / 255.0f;
	g = ent->curstate.rendercolor.g / 255.0f;
	b = ent->curstate.rendercolor.b / 255.0f;

	R_BeamSetup( &beam, ent->origin, ent->angles, ent->curstate.modelindex, 0, ent->curstate.scale, amp, blend, ent->curstate.animtime );
	R_BeamSetAttributes( &beam, r, g, b, ent->curstate.framerate, ent->curstate.frame );
	beam.pFollowModel = NULL;

	switch( ent->curstate.rendermode & 0x0F )
	{
	case BEAM_ENTPOINT:
		beam.type	= TE_BEAMPOINTS;
		if( ent->curstate.sequence )
		{
			SetBits( beam.flags, FBEAM_STARTENTITY );
			beam.startEntity = ent->curstate.sequence;
		}
		if( ent->curstate.skin )
		{
			SetBits( beam.flags, FBEAM_ENDENTITY );
			beam.endEntity = ent->curstate.skin;
		}
		break;
	case BEAM_ENTS:
		beam.type	= TE_BEAMPOINTS;
		SetBits( beam.flags, FBEAM_STARTENTITY | FBEAM_ENDENTITY );
		beam.startEntity = ent->curstate.sequence;
		beam.endEntity = ent->curstate.skin;
		break;
	case BEAM_HOSE:
		beam.type	= TE_BEAMHOSE;
		break;
	case BEAM_POINTS:
		// already set up
		break;
	}

	beamFlags = ( ent->curstate.rendermode & 0xF0 );

	if( FBitSet( beamFlags, BEAM_FSINE ))
		SetBits( beam.flags, FBEAM_SINENOISE );

	if( FBitSet( beamFlags, BEAM_FSOLID ))
		SetBits( beam.flags, FBEAM_SOLID );

	if( FBitSet( beamFlags, BEAM_FSHADEIN ))
		SetBits( beam.flags, FBEAM_SHADEIN );

	if( FBitSet( beamFlags, BEAM_FSHADEOUT ))
		SetBits( beam.flags, FBEAM_SHADEOUT );

	// draw it
	R_BeamDraw( &beam, frametime );
}

