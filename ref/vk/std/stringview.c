#include "stringview.h"

#include <string.h>

const_string_view_t svFromNullTerminated( const char *s ) {
	return (const_string_view_t){.len = s?strlen(s):0, .s = s};
}

int svCmp(const_string_view_t sv, const char* s) {
	for (int i = 0; i < sv.len; ++i) {
		const int d = sv.s[i] - s[i];
		if (d != 0)
			return d;
		if (s[i] == '\0')
			return 1;
	}

	// Check that both strings end the same
	return '\0' - s[sv.len];
}

const_string_view_t svStripExtension(const_string_view_t sv) {
	for (int i = sv.len - 1; i >= 0; --i) {
		const char c = sv.s[i];
		if (c == '.')
			return (const_string_view_t){ .len = i, .s = sv.s };

		if (c == '/' || c == '\\' || c == ':')
			break;
	}

	return sv;
}

#define MIN(a,b) ((a)<(b)?(a):(b))

void svStrncpy(const_string_view_t sv, char *dest, int size) {
	const int to_copy = MIN(sv.len, size - 1);
	memcpy(dest, sv.s, to_copy);
	dest[to_copy] = '\0';
}
