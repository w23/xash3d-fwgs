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

#define PREAMBLE(N, type_) \
	item_t items[N]; \
	const urmom_desc_t desc = { \
		.array = items, \
		.count = COUNTOF(items), \
		.item_size = sizeof(item_t), \
		.type = type_, \
	}; \
	urmomInit(&desc)

static int test_insert_find_remove( void ) {
	PREAMBLE(4, kUrmomString);

	const urmom_insert_t i = urmomInsert(&desc, "bidonchik");
	CHECK_NOT_EQUAL_I(i.index, -1);
	CHECK_EQUAL_I(i.created, 1);
	CHECK_EQUAL_S(items[i.index].hdr_.key, "bidonchik");

	const int found = urmomFind(&desc, "bidonchik");
	CHECK_EQUAL_I(found, i.index);

	const urmom_insert_t i2 = urmomInsert(&desc, "bidonchik");
	CHECK_EQUAL_I(i2.index, i.index);
	CHECK_EQUAL_I(i2.created, 0);
	CHECK_EQUAL_S(items[i.index].hdr_.key, "bidonchik");

	const int removed = urmomRemove(&desc, "bidonchik");
	CHECK_EQUAL_I(removed, i.index);
	CHECK_EQUAL_I(items[i.index].hdr_.key[0], '\0');

	const int not_found = urmomFind(&desc, "bidonchik");
	CHECK_EQUAL_I(not_found, -1);

	return 1;
}

static int test_find_nonexistent( void ) {
	PREAMBLE(4, kUrmomString);

	const int found = urmomFind(&desc, "kishochki");
	CHECK_EQUAL_I(found, -1);
	return 1;
}

static int test_insert_find_many( void ) {
	PREAMBLE(4, kUrmomString);

	const urmom_insert_t a = urmomInsert(&desc, "smetanka");
	CHECK_NOT_EQUAL_I(a.index, -1);
	CHECK_EQUAL_I(a.created, 1);
	CHECK_EQUAL_S(items[a.index].hdr_.key, "smetanka");

	const urmom_insert_t b = urmomInsert(&desc, "tworog");
	CHECK_NOT_EQUAL_I(b.index, -1);
	CHECK_EQUAL_I(b.created, 1);
	CHECK_NOT_EQUAL_I(a.index, b.index);
	CHECK_EQUAL_S(items[b.index].hdr_.key, "tworog");

	const int a_found = urmomFind(&desc, "smetanka");
	const int b_found = urmomFind(&desc, "tworog");

	CHECK_EQUAL_I(a_found, a.index);
	CHECK_EQUAL_I(b_found, b.index);

	return 1;
}

static int test_overflow( void ) {
	PREAMBLE(4, kUrmomString);

	const urmom_insert_t a = urmomInsert(&desc, "smetanka");
	CHECK_NOT_EQUAL_I(a.index, -1);
	CHECK_EQUAL_I(a.created, 1);
	CHECK_EQUAL_S(items[a.index].hdr_.key, "smetanka");

	const urmom_insert_t b = urmomInsert(&desc, "tworog");
	CHECK_NOT_EQUAL_I(b.index, -1);
	CHECK_EQUAL_I(b.created, 1);
	CHECK_NOT_EQUAL_I(a.index, b.index);
	CHECK_EQUAL_S(items[b.index].hdr_.key, "tworog");


	const urmom_insert_t c = urmomInsert(&desc, "kefirushka");
	CHECK_NOT_EQUAL_I(c.index, -1);
	CHECK_EQUAL_I(c.created, 1);
	CHECK_NOT_EQUAL_I(a.index, c.index);
	CHECK_NOT_EQUAL_I(b.index, c.index);
	CHECK_EQUAL_S(items[c.index].hdr_.key, "kefirushka");

	const urmom_insert_t d = urmomInsert(&desc, "ryazhenka");
	CHECK_NOT_EQUAL_I(d.index, -1);
	CHECK_EQUAL_I(d.created, 1);
	CHECK_NOT_EQUAL_I(a.index, d.index);
	CHECK_NOT_EQUAL_I(b.index, d.index);
	CHECK_NOT_EQUAL_I(c.index, d.index);
	CHECK_EQUAL_S(items[d.index].hdr_.key, "ryazhenka");

	{
		const urmom_insert_t e = urmomInsert(&desc, "riajenka");
		CHECK_EQUAL_I(e.index, -1);
		CHECK_EQUAL_I(e.created, 0);
	}

	const int d_remove = urmomRemove(&desc, "ryazhenka");
	CHECK_EQUAL_I(d_remove, d.index);
	CHECK_EQUAL_I(items[d_remove].hdr_.state, 0);
	CHECK_NOT_EQUAL_I(items[d_remove].hdr_.hash, 0);
	CHECK_EQUAL_I(items[d_remove].hdr_.key[0], '\0');

	const urmom_insert_t e = urmomInsert(&desc, "riajenka");
	CHECK_NOT_EQUAL_I(e.index, -1);
	CHECK_EQUAL_I(e.created, 1);
	CHECK_NOT_EQUAL_I(a.index, e.index);
	CHECK_NOT_EQUAL_I(b.index, e.index);
	CHECK_NOT_EQUAL_I(c.index, e.index);
	CHECK_EQUAL_S(items[e.index].hdr_.key, "riajenka");

	return 1;
}

// Assumes FNV-1a
static int test_hash_collision( void ) {
	PREAMBLE(4, kUrmomString);

	const urmom_insert_t a = urmomInsert(&desc, "costarring");
	CHECK_NOT_EQUAL_I(a.index, -1);
	CHECK_EQUAL_I(a.created, 1);

	const urmom_insert_t b = urmomInsert(&desc, "liquid");
	CHECK_NOT_EQUAL_I(b.index, -1);
	CHECK_EQUAL_I(b.created, 1);
	CHECK_NOT_EQUAL_I(b.index, a.index);

	CHECK_EQUAL_I(items[a.index].hdr_.hash, items[b.index].hdr_.hash);

	const int a_found = urmomFind(&desc, "costarring");
	CHECK_EQUAL_I(a_found, a.index);

	const int b_found = urmomFind(&desc, "liquid");
	CHECK_EQUAL_I(b_found, b.index);

	return 1;
}

static int test_insert_find_remove_insensitive( void ) {
	PREAMBLE(4, kUrmomStringInsensitive);

	const urmom_insert_t i = urmomInsert(&desc, "bidonchik");
	CHECK_NOT_EQUAL_I(i.index, -1);
	CHECK_EQUAL_I(i.created, 1);
	CHECK_EQUAL_S(items[i.index].hdr_.key, "bidonchik");

	const int found = urmomFind(&desc, "BIDONCHIk");
	CHECK_EQUAL_I(found, i.index);

	const urmom_insert_t i2 = urmomInsert(&desc, "biDONChik");
	CHECK_EQUAL_I(i2.index, i.index);
	CHECK_EQUAL_I(i2.created, 0);
	CHECK_EQUAL_S(items[i.index].hdr_.key, "bidonchik");

	const int removed = urmomRemove(&desc, "bidonCHIK");
	CHECK_EQUAL_I(removed, i.index);
	CHECK_EQUAL_I(items[i.index].hdr_.key[0], '\0');

	const int not_found = urmomFind(&desc, "bidonchik");
	CHECK_EQUAL_I(not_found, -1);

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
	X(test_insert_find_remove_insensitive) \
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
