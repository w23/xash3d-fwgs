#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

typedef struct {
	int w, h;
	int comp;
	unsigned char* data;
} image_t;

static int imageLoad(image_t* img, const char *f) {
	img->data = stbi_load(f, &img->w, &img->h, &img->comp, 0);
	if (!img->data)
		fprintf(stderr, "Unable to load image \"%s\"\n", f);
	return !!(img->data);
}

static int imageSave(image_t* img, const char *f) {
	return stbi_write_png(f, img->w, img->h, img->comp, img->data, 0);
}

/* LOL no need to clear anything
static void imageFree(image_t *img) {
	if (img->data)
		stbi_image_free(img->data);
}
*/

int main(int argc, char *argv[]) {
	if (argc < 4) {
		fprintf(stderr, "Usage: %s infile1 infile2 out_diff.png\n", argv[0]);
		return 1;
	}

	image_t a, b;

	if (!imageLoad(&a, argv[1]))
		return 1;

	if (!imageLoad(&b, argv[2]))
		return 1;

	if (a.w != b.w || a.h != b.h || a.comp != b.comp) {
		fprintf(stderr, "Images metadata doesn't match: %s:%dx%d(%d) %s:%dx%d(%d)\n",
			argv[1], a.w, a.h, a.comp,
			argv[2], b.w, b.h, b.comp);
		return 1;
	}

	image_t diff = {
		.w = a.w,
		.h = a.h,
		.comp = a.comp,
		.data = malloc(a.w * a.h * a.comp),
	};

	const unsigned char* ap = a.data, *bp = b.data;
	unsigned char* dp = diff.data;

	uint32_t diff_sum = 0;
	for (int y = 0; y < a.h; ++y) {
		for (int x = 0; x < a.w; ++x) {
			const uint8_t ar = *(ap++);
			const uint8_t ag = *(ap++);
			const uint8_t ab = *(ap++);

			const uint8_t br = *(bp++);
			const uint8_t bg = *(bp++);
			const uint8_t bb = *(bp++);

			const uint8_t dr = abs((int)ar-br);
			const uint8_t dg = abs((int)ag-bg);
			const uint8_t db = abs((int)ab-bb);

			*(dp++) = dr;
			*(dp++) = dg;
			*(dp++) = db;

			const int diff = dr + dg + db;
			diff_sum += diff;
		}
	}

	const uint32_t total = a.w * a.h * a.comp * 256;

	const float diff_pct_threshold = 1.f;
	const float diff_pct = diff_sum * 100.f / total;
	const int over = diff_pct_threshold < diff_pct;

	fprintf(stderr, "%sTotal difference \"%s\" vs \"%s\": %d (%.03f%%)%s\n",
		over ? "\033[31mFAIL " : "",
		argv[1], argv[2], diff_sum, diff_pct,
		over ? "\033[0m" : ""
		);

	if (!imageSave(&diff, argv[3]))
		return 1;

	return over;
}
