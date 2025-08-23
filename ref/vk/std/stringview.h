#pragma once

typedef struct {
	const char *s;
	int len;
} const_string_view_t;

const_string_view_t svFromNullTerminated( const char *s );

int svCmp(const_string_view_t sv, const char* s);
void svStrncpy(const_string_view_t sv, char *dest, int size);

const_string_view_t svStripExtension(const_string_view_t sv);

const_string_view_t svSkipWhitespace(const_string_view_t sv);

typedef struct {
	long value;
	int chars_converted;
} SVParseLongResult;

// Parse string view into long
// Decimal-only for now
// Does not skip whitespace, do it explicitly
// Very simple and not very robust -- cannot read LONG_MIN
// Does not accept '+' as prefix
SVParseLongResult svParseLong(const_string_view_t sv);
