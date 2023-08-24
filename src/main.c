#include <stdio.h>

#include "memscrub.h"

static size_t cacheline_width(const CCacheDesc *me) {
	return sizeof(Cacheline);
}

static size_t cacheline_size(const CCacheDesc *me) {
	return 1 << me->c_cacheline_width(me);
}

static size_t cache_index_width(const CCacheDesc *me) {
	return CACHE_INDEX_WIDTH;
}

static void read_cacheline(CCacheDesc *me, Cacheline *cacheline) {
	((volatile Cacheline *)cacheline)[0];
}

static size_t size_in_cachelines(const CCacheDesc *me,
	const ScrubArea *scrub_area) {
	size_t start = ((size_t)scrub_area->start) >> me->c_cacheline_width(me);
	size_t end = ((size_t)scrub_area->end) >> me->c_cacheline_width(me);
	return start - end + 1;
}

static size_t cache_index(const CCacheDesc *me, const uint8_t *p) {
	return ((size_t)p >> me->c_cacheline_width(me)) &
		((1 << me->c_cache_index_width(me)) - 1);
}

static CCacheDesc cache_desc = {
	.c_cacheline_width = cacheline_width,
	.c_cacheline_size = cacheline_size,
	.c_cache_index_width = cache_index_width,
	.c_read_cacheline = read_cacheline,
	.c_size_in_cachelines = size_in_cachelines,
	.c_cache_index = cache_index,
};

static size_t next(CAutoScrubDesc *me) {
	return 0;
};

static CAutoScrubDesc auto_scrub_desc = {
	.next = next,
};

int main(int argc, char *argv[]) {
	int rc;
	
	rc = autoscrub(&cache_desc, NULL, 0, &auto_scrub_desc);
	printf("rc %d\n", rc);
}

/*

#[repr(C)]
pub struct CAutoScrubDesc {
    me: *mut CAutoScrubDesc,
    c_next: extern "C" fn(me: *mut CAutoScrubDesc) -> usize,
}

impl BaseAutoScrubDesc for CAutoScrubDesc {
    fn next(&mut self) -> usize {
        (self.c_next)(self.me)
    }
}

#[no_mangle]
pub extern "C" fn autoscrub(c_cache_desc: &mut CCacheDesc,
    scrub_areas_ptr: *const ScrubArea, n_scrub_areas: usize,
    c_auto_scrub_desc: &mut CAutoScrubDesc) -> Result<usize, Error> {
    let scrub_areas = unsafe {
        slice::from_raw_parts(scrub_areas_ptr, n_scrub_areas)
    };
    BaseAutoScrub::autoscrub(c_cache_desc, &scrub_areas, c_auto_scrub_desc)
}
*/
