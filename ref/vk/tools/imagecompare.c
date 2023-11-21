#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <time.h>

uint64_t now( void ) {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_nsec + tp.tv_sec * 1000000000ull;
}

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
	const char *const ext = strrchr(f, '.');
	if (ext != NULL) {
		if (strcmp(ext, ".png") == 0) {
			return stbi_write_png(f, img->w, img->h, img->comp, img->data, 0);
		} else if (strcmp(ext, ".bmp") == 0) {
			return stbi_write_bmp(f, img->w, img->h, img->comp, img->data);
		} else if (strcmp(ext, ".tga") == 0) {
			return stbi_write_tga(f, img->w, img->h, img->comp, img->data);
		} else if (strcmp(ext, ".jpg") ==0 || strcmp(ext, ".jpeg") == 0) {
			return stbi_write_jpg(f, img->w, img->h, img->comp, img->data, 90);
		}
	}

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

	const uint64_t start_ns = now();

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

	const uint64_t loaded_ns = now();

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

	const uint64_t diffd_ns = now();

	const uint32_t total = a.w * a.h * a.comp * 256;

	const float diff_pct_threshold = 1.f;
	const float diff_pct = diff_sum * 100.f / total;
	const int over = diff_pct_threshold < diff_pct;

	fprintf(stderr, "%s\"%s\" vs \"%s\": %d (%.03f%%)\033[0m\n",
		over ? "\033[31mFAIL" : (diff_sum == 0 ? "\033[32m" : ""),
		argv[1], argv[2], diff_sum, diff_pct
		);

	if (!imageSave(&diff, argv[3]))
		return 1;

	const uint64_t end_ns = now();

#define MS(t) ((t)/1e6)
	fprintf(stderr, "loading: %.03fms, diffing: %.03fms, saving: %.03fms; total: %.03fms\n",
		MS(loaded_ns - start_ns), MS(diffd_ns - loaded_ns), MS(end_ns - diffd_ns), MS(end_ns - start_ns));

	return over;
}
