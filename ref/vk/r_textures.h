#pragma once

#include "xash3d_types.h"
#include "const.h" // required for ref_api.h
#include "cvardef.h"
#include "com_model.h"
#include "ref_api.h" // needed for render_api.h
#include "render_api.h" // texFlags_t
#include "com_image.h"

qboolean R_TexturesInit( void );
void R_TexturesShutdown( void );

// Ref interface functions, exported
int R_TextureFindByName( const char *name );
const char* R_TextureGetNameByIndex( unsigned int texnum );

void R_TextureSetupSky( const char *skyboxname );

int R_TextureUploadFromFile( const char *name, const byte *buf, size_t size, int flags );
int R_TextureUploadFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update_only );
void R_TextureFree( unsigned int texnum );

#include "vk_textures.h"
