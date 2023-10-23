#include "../unordered_roadmap.h"

#define URMOM_TEST
#include "../unordered_roadmap.c"

#define LOG(msg, ...) \
	fprintf(stderr, "%s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define CHECK_EQUAL_I(a, b) \
	do { \
		const int ar = (a), br = (b); \
		if (ar != br) { \
			LOG("CHECK_EQUAL_I("#a", "#b") failed: %d != %d", ar, br); \
			return 0; \
		} \
	} while (0)

#define CHECK_EQUAL_S(a, b) \
	do { \
		const char *ar = (a), *br = (b); \
		if (strcmp(ar, br) != 0) { \
			LOG("CHECK_EQUAL_S("#a", "#b") failed: \"%s\" != \"%s\"", ar, br); \
			return 0; \
		} \
	} while (0)

#define CHECK_NOT_EQUAL_I(a, b) \
	do { \
		const int ar = (a), br = (b); \
		if (ar == br) { \
			LOG("CHECK_NOT_EQUAL_I("#a", "#b") failed: %d == %d", ar, br); \
			return 0; \
		} \
	} while (0)

typedef struct {
	urmom_header_t hdr_;
	int i;
	float f;
} item_t;

#define PREAMBLE(N) \
	item_t items[N]; \
	const urmom_desc_t desc = { \
		.array = items, \
		.count = COUNTOF(items), \
		.item_size = sizeof(item_t), \
	}; \
	urmomInit(&desc)

static int test_insert_find_remove( void ) {
	PREAMBLE(4);

	const int i = urmomInsert(&desc, "bidonchik");
	CHECK_NOT_EQUAL_I(i, -1);
	CHECK_EQUAL_S(items[i].hdr_.key, "bidonchik");

	const int found = urmomFind(&desc, "bidonchik");
	CHECK_EQUAL_I(found, i);

	const int removed = urmomRemove(&desc, "bidonchik");
	CHECK_EQUAL_I(removed, i);
	CHECK_EQUAL_I(items[i].hdr_.key[0], '\0');

	const int not_found = urmomFind(&desc, "bidonchik");
	CHECK_EQUAL_I(not_found, -1);

	return 1;
}

static int test_find_nonexistent( void ) {
	PREAMBLE(4);

	const int found = urmomFind(&desc, "kishochki");
	CHECK_EQUAL_I(found, -1);
	return 1;
}

static int test_insert_find_many( void ) {
	PREAMBLE(4);

	const int a = urmomInsert(&desc, "smetanka");
	CHECK_NOT_EQUAL_I(a, -1);
	CHECK_EQUAL_S(items[a].hdr_.key, "smetanka");

	const int b = urmomInsert(&desc, "tworog");
	CHECK_NOT_EQUAL_I(b, -1);
	CHECK_NOT_EQUAL_I(a, b);
	CHECK_EQUAL_S(items[b].hdr_.key, "tworog");

	const int a_found = urmomFind(&desc, "smetanka");
	const int b_found = urmomFind(&desc, "tworog");

	CHECK_EQUAL_I(a_found, a);
	CHECK_EQUAL_I(b_found, b);

	return 1;
}

static int test_overflow( void ) {
	PREAMBLE(4);

	const int a = urmomInsert(&desc, "smetanka");
	CHECK_NOT_EQUAL_I(a, -1);
	CHECK_EQUAL_S(items[a].hdr_.key, "smetanka");

	const int b = urmomInsert(&desc, "tworog");
	CHECK_NOT_EQUAL_I(b, -1);
	CHECK_NOT_EQUAL_I(a, b);
	CHECK_EQUAL_S(items[b].hdr_.key, "tworog");


	const int c = urmomInsert(&desc, "kefirushka");
	CHECK_NOT_EQUAL_I(c, -1);
	CHECK_NOT_EQUAL_I(a, c);
	CHECK_NOT_EQUAL_I(b, c);
	CHECK_EQUAL_S(items[c].hdr_.key, "kefirushka");

	const int d = urmomInsert(&desc, "ryazhenka");
	CHECK_NOT_EQUAL_I(d, -1);
	CHECK_NOT_EQUAL_I(a, d);
	CHECK_NOT_EQUAL_I(b, d);
	CHECK_NOT_EQUAL_I(c, d);
	CHECK_EQUAL_S(items[d].hdr_.key, "ryazhenka");

	{
		const int e = urmomInsert(&desc, "riajenka");
		CHECK_EQUAL_I(e, -1);
	}

	const int d_remove = urmomRemove(&desc, "ryazhenka");
	CHECK_EQUAL_I(d_remove, d);
	CHECK_EQUAL_I(items[d_remove].hdr_.state, 0);
	CHECK_NOT_EQUAL_I(items[d_remove].hdr_.hash, 0);
	CHECK_EQUAL_I(items[d_remove].hdr_.key[0], '\0');

	const int e = urmomInsert(&desc, "riajenka");
	CHECK_NOT_EQUAL_I(e, -1);
	CHECK_NOT_EQUAL_I(a, e);
	CHECK_NOT_EQUAL_I(b, e);
	CHECK_NOT_EQUAL_I(c, e);
	CHECK_EQUAL_S(items[e].hdr_.key, "riajenka");

	return 1;
}

// Assumes FNV-1a
static int test_hash_collision( void ) {
	PREAMBLE(4);

	const int a = urmomInsert(&desc, "costarring");
	CHECK_NOT_EQUAL_I(a, -1);

	const int b = urmomInsert(&desc, "liquid");
	CHECK_NOT_EQUAL_I(b, -1);
	CHECK_NOT_EQUAL_I(b, a);

	CHECK_EQUAL_I(items[a].hdr_.hash, items[b].hdr_.hash);

	const int a_found = urmomFind(&desc, "costarring");
	CHECK_EQUAL_I(a_found, a);

	const int b_found = urmomFind(&desc, "liquid");
	CHECK_EQUAL_I(b_found, b);

	return 1;
}

static int test_fail( void ) {
	//CHECK_EQUAL_S("sapogi", "tapki");
	return 1;
}

#define LIST_TESTS(X) \
	X(test_insert_find_remove) \
	X(test_find_nonexistent) \
	X(test_insert_find_many) \
	X(test_hash_collision) \
	X(test_fail) \

int main( void ) {
	int retval = 0;
#define X(f) \
	do { \
		fprintf(stderr, "Running " #f "...\n"); \
		const int result = f(); \
		fprintf(stderr, #f " => %s\n", result == 0 ? "FAIL" : "OK" ); \
		if (!result) \
			++retval;\
	} while (0);

LIST_TESTS(X)
#undef X

	return retval;
}
