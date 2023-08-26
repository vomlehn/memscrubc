#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "my-memscrub.h"

#define ARRAY_SIZE(_a)	(sizeof(_a) / sizeof((_a)[0]))

#ifdef TEST
// Specify the parameters of the scrub
// BYTES_TO_SCRUB	Total number of bytes being scrubbed
// SCRUB_INCREMENT	Number of bytes scrubbed at a time
// scrub_sizes		Array of sizes, in bytes, of memory areas to scrub
#if 1		// FIXME: revert this
#define BYTES_TO_SCRUB		(30ull * 1024 * 1024 * 1024)
#define SCRUB_INCREMENT		(1ull * 1024 * 1024)

static size_t scrub_sizes[] = {512 * 1024 * 1024, 3 * 512 * 1024 * 1024};
#else
#define BYTES_TO_SCRUB		(101 * sizeof(Cacheline))
#define SCRUB_INCREMENT		(3 * sizeof(Cacheline))

static size_t scrub_sizes[] = {
	2 *  sizeof(Cacheline),
	3 *  sizeof(Cacheline),
	1 * sizeof(Cacheline),
	31 * sizeof(Cacheline),
};
#endif

static void test_autoscrub(const char *name);

static size_t read_count;
static size_t scrub_count;

static size_t cacheline_width(const CCacheDesc *me) {
	return me->cl_width;
}

static size_t cacheline_size(const CCacheDesc *me) {
	return 1 << me->c_cacheline_width(me);
}

static size_t cache_index_width(const CCacheDesc *me) {
	return CACHE_INDEX_WIDTH;
}

static void read_cacheline(CCacheDesc *me, const Cacheline *cacheline) {
	volatile const ECCData *p = &cacheline->data[0];
	(void)*p;
	read_count++;
}

static size_t size_in_cachelines(const CCacheDesc *me,
	const ScrubArea *scrub_area) {
	uintptr_t start = ((uintptr_t)scrub_area->start) >>
		me->c_cacheline_width(me);
	uintptr_t end = ((uintptr_t)scrub_area->end) >>
		me->c_cacheline_width(me);
	return end - start + 1;
}

static size_t cache_index(const CCacheDesc *me, const uint8_t *p) {
	return ((uintptr_t)p >> me->c_cacheline_width(me)) &
		((1 << me->c_cache_index_width(me)) - 1);
}

static CCacheDesc cache_desc = {
	.me = &cache_desc,
	.cl_width = sizeof(int) * CHAR_BIT - 1 -
		__builtin_clz(sizeof(Cacheline)),
	.c_cacheline_width = cacheline_width,
	.c_cacheline_size = cacheline_size,
	.c_cache_index_width = cache_index_width,
	.c_read_cacheline = read_cacheline,
	.c_size_in_cachelines = size_in_cachelines,
	.c_cache_index = cache_index,
};

typedef struct {
	size_t	count;
	CAutoScrubDesc	auto_scrub_desc;
} TestAutoScrubDesc;

static size_t next(CAutoScrubDesc *me) {
	size_t cur_count;
	size_t delta;
	const size_t increment = SCRUB_INCREMENT;
	size_t offset = offsetof(TestAutoScrubDesc, auto_scrub_desc);
	TestAutoScrubDesc *test_auto_scrub_desc;
	char *test_auto_scrub_desc_p = ((char *)me - offset);
       	test_auto_scrub_desc = (TestAutoScrubDesc *)test_auto_scrub_desc_p;

	cur_count = test_auto_scrub_desc->count;
	delta = cur_count > increment ? increment : cur_count;
	test_auto_scrub_desc->count -= delta;

	size_t cacheline_width = cache_desc.c_cacheline_width(&cache_desc);
	scrub_count += delta >> cacheline_width;

	return delta;
};

static TestAutoScrubDesc test_auto_scrub_desc = {
	.count = BYTES_TO_SCRUB,
	.auto_scrub_desc = {
		.me = &test_auto_scrub_desc.auto_scrub_desc,
		.c_next = next,
	},
};

// Allocate a memory, aligning it on a cache line size boundary
//
// Returns: A ScrubArea
static ScrubArea alloc_mem(size_t size) {
	ScrubArea scrub_area;
	void *p;

	p = malloc(size + sizeof(Cacheline));
	if (p == NULL) {
		fprintf(stderr, "Cant' allocate %zu bytes\n", size);
		exit(EXIT_FAILURE);
	}

	p = (void *)(((uintptr_t)p + sizeof(Cacheline) - 1) &
		~(sizeof(Cacheline) - 1));
	scrub_area.start = p;
	scrub_area.end = (char *)p + (size - 1);
	*(volatile char *)p = 0;
	return scrub_area;
}

static void test_autoscrub(const char *name) {
	AutoScrubResult result;
	CAutoScrubDesc *auto_scrub_desc;
	ScrubArea scrub_areas[ARRAY_SIZE(scrub_sizes)];
	size_t cacheline_width;
	size_t i;

	cacheline_width = cache_desc.c_cacheline_width(&cache_desc);

	// Allocate memory
	for (i = 0; i < ARRAY_SIZE(scrub_areas); i++) {
		scrub_areas[i] = alloc_mem(scrub_sizes[i]);
	}
	
	auto_scrub_desc = &test_auto_scrub_desc.auto_scrub_desc;
	printf("scrub_areas [\n"); 

	// Print the areas to be scrubbed
	for (i = 0; i < ARRAY_SIZE(scrub_areas); i++) {
		ScrubArea *scrub_area;
		scrub_area = &scrub_areas[i];
		printf("\t%p-%p: %zu\n", scrub_area->start,
			scrub_area->end,
			cache_desc.c_size_in_cachelines(&cache_desc,
				scrub_area) << cacheline_width);
	}
	printf("]\n");
	fflush(stdout);

	result = autoscrub(&cache_desc, scrub_areas, ARRAY_SIZE(scrub_areas),
		auto_scrub_desc);
	if (result.is_err) {
		fprintf(stderr, "%s failed: error %u\n", name,
			result.error);
		exit(EXIT_FAILURE);
	}

	printf("scrub_count %zu read_count %zu\n", scrub_count, read_count);
	if (scrub_count != read_count) {
		fprintf(stderr, "scrub_count != read_count\n");
		exit(EXIT_FAILURE);
	}
	double gigabyte = 1024 * 1024 * 1024;
	double count = scrub_count * (double)(1 << cacheline_width);
	double gigabytes_touched = count / gigabyte;
	printf("Touched %f GB\n", gigabytes_touched);
}
#endif

int main(int argc, char *argv[]) {
#ifdef TEST
	test_autoscrub(argv[0]);
#else
	fprintf(stderr, "%s: TEST is not #defined\n", argv[0]);
	exit(EXIT_FAILURE);
#endif

	return EXIT_SUCCESS;
}
