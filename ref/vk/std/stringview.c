#include "stringview.h"

#include <string.h>
#include <ctype.h> // isspace

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

const_string_view_t svSkipWhitespace(const_string_view_t sv) {
	while (sv.len > 0 && isspace(sv.s[0])) {
		sv.len--; sv.s++;
	}
	return sv;
}

SVParseLongResult svParseLong(const_string_view_t sv) {
	int i = 0;
	long value = 0;
	long sign = 1;

	if (i < sv.len && sv.s[0] == '-') {
		sign = -1;
		++i;
	}

	while (i < sv.len) {
		const char c = sv.s[i++];
		if (c < '0' || c > '9')
			break;

		value = value * 10 + (c - '0');
	}

	return (SVParseLongResult){
		.value = value * sign,
		// If only sign was read, then it's not a number
		.chars_converted = (sign < 0 && i == 1) ? 0 : i,
	};
}
