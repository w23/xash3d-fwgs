#pragma once

typedef struct {
	const char *s;
	int len;
} const_string_view_t;

const_string_view_t svFromNullTerminated( const char *s );

int svCmp(const_string_view_t sv, const char* s);
void svStrncpy(const_string_view_t sv, char *dest, int size);

const_string_view_t svStripExtension(const_string_view_t sv);
