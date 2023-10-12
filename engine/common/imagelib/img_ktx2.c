/*
img_dds.c - dds format load
Copyright (C) 2015 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "ktx2.h"

qboolean Image_LoadKTX2( const char *name, const byte *buffer, fs_offset_t filesize ) {
	ktx2_header_t header;

	if( filesize < KTX2_MINIMAL_HEADER_SIZE )
		return false;

	if ( memcmp(buffer, KTX2_IDENTIFIER, KTX2_IDENTIFIER_SIZE) != 0 ) {
		Con_DPrintf( S_ERROR "%s: (%s) has invalid identifier\n", __FUNCTION__, name );
		return false;
	}

	memcpy(&header, buffer + KTX2_IDENTIFIER_SIZE, sizeof header);

	/* ktx2_index_t index; */
	/* memcpy(&header, buffer + KTX2_IDENTIFIER_SIZE + sizeof header, sizeof index); */

	image.width = header.pixelWidth;
	image.height = header.pixelHeight;
	image.depth = Q_max(1, header.pixelDepth);

	// Just pass file contents as rgba data directly

	// TODO support various formats individually, for other renders to be able to consume them too
	// This is a catch-all for ref_vk, which can do this format directly and natively
	image.type = PF_KTX2_RAW;

	image.size = filesize;
	//image.encode = TODO custom encode type?

	// FIXME format-dependent
	image.flags = IMAGE_HAS_COLOR; // | IMAGE_HAS_ALPHA

	image.rgba = Mem_Malloc( host.imagepool, image.size );
	memcpy(image.rgba, buffer, image.size);

	return true;
}
