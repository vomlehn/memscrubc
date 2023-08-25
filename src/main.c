#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "memscrub.h"

#define ARRAY_SIZE(_a)	(sizeof(_a) / sizeof((_a)[0]))

static size_t cacheline_width(const CCacheDesc *me) {
	return me->cl_width;
}

static size_t cacheline_size(const CCacheDesc *me) {
	return 1 << me->c_cacheline_width(me);
}

static size_t cache_index_width(const CCacheDesc *me) {
	return CACHE_INDEX_WIDTH;
}

static void read_cacheline(CCacheDesc *me, Cacheline *cacheline) {
	volatile ECCData *p = &cacheline->data[0];
printf("probing %p\n", p);
	*(volatile char *)p = 0;
printf(" probe done\n");
	(void)*p;
}

static size_t size_in_cachelines(const CCacheDesc *me,
	const ScrubArea *scrub_area) {
	uintptr_t start = ((uintptr_t)scrub_area->start) >>
		me->c_cacheline_width(me);
	uintptr_t end = ((uintptr_t)scrub_area->end) >>
		me->c_cacheline_width(me);
	return start - end + 1;
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
	const size_t decrement = 2 * sizeof(Cacheline);
	size_t offset = offsetof(TestAutoScrubDesc, auto_scrub_desc);
	TestAutoScrubDesc *test_auto_scrub_desc;
	char *test_auto_scrub_desc_p = ((char *)me - offset);
       	test_auto_scrub_desc = (TestAutoScrubDesc *)test_auto_scrub_desc_p;

	cur_count = test_auto_scrub_desc->count;
	delta = cur_count > decrement ? decrement : cur_count;
	test_auto_scrub_desc->count -= delta;
printf("next: me %p cur_count %zu delta %zu\n", me, test_auto_scrub_desc->count, delta);

	return delta;
};

static TestAutoScrubDesc test_auto_scrub_desc = {
	.count = 5 * sizeof(Cacheline),
	.auto_scrub_desc = {
		.next = next,
		.me = &test_auto_scrub_desc.auto_scrub_desc,
	},
};

static ScrubArea alloc_mem(size_t size) {
	ScrubArea scrub_area;
	void *p;

	p = malloc(size + sizeof(Cacheline));
	if (p == NULL) {
		fprintf(stderr, "Cant' allocate %zu bytes\n", size);
		exit(EXIT_FAILURE);
	}

printf("alloc_mem at %p\n", p);
	p = (void *)(((uintptr_t)p + sizeof(Cacheline) - 1) &
		~(sizeof(Cacheline) - 1));
printf("alloc_mem at %p\n", p);
	scrub_area.start = p;
	scrub_area.end = p + (size - 1);
printf("probing %p\n", p);
	*(volatile char *)p = 0;
printf(" probe done\n");
	return scrub_area;
}

int main(int argc, char *argv[]) {
	AutoScrubResult result;
	CAutoScrubDesc *auto_scrub_desc;
	ScrubArea scrub_areas[1];

	size_t size = 2000 * sizeof(Cacheline);
	scrub_areas[0] = alloc_mem(size);
	
	auto_scrub_desc = &test_auto_scrub_desc.auto_scrub_desc;
	printf("cache_desc %p\n", &cache_desc);
	printf("cache_desc.cl_width %zu\n", cache_desc.cl_width);
	printf("scrub_areas [");
	size_t i;
	const char *sep = "";
	for (i = 0; i < ARRAY_SIZE(scrub_areas); i++) {
		printf("%s%p-%p", sep, scrub_areas[i].start, scrub_areas[i].end);
		sep = ", ";
	}
	printf("]\n");

	printf("auto_scrub_desc %p\n", auto_scrub_desc);
	result = autoscrub(&cache_desc, scrub_areas, ARRAY_SIZE(scrub_areas),
		auto_scrub_desc);
	if (result.is_err) {
		fprintf(stderr, "%s failed: error %u\n", argv[0],
			result.error);
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
}
