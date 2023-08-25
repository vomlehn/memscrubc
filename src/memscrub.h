// For instantiating architecture-specfic objects for the memory scrubbing
// code

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CACHE_INDEX_WIDTH	10
#define CACHELINE_ITEMS		8

typedef uint64_t ECCData;
typedef struct {
	ECCData data[CACHELINE_ITEMS];
} Cacheline;

typedef struct {
	const void *start;
	const void *end;
} ScrubArea;

typedef struct CCacheDesc_s CCacheDesc;
struct CCacheDesc_s {
	CCacheDesc *me;
	size_t cl_width;
	size_t (*c_cacheline_width)(const CCacheDesc *me);
	size_t (*c_cacheline_size)(const CCacheDesc *me);
	size_t (*c_cache_index_width)(const CCacheDesc *me);
	void (*c_read_cacheline)(CCacheDesc *me, Cacheline *cacheline);
	size_t (*c_size_in_cachelines)(const CCacheDesc *me,
		const ScrubArea *scrub_area);
	size_t (*c_cache_index)(const CCacheDesc *me, const uint8_t *p);
};

typedef struct CAutoScrubDesc_s CAutoScrubDesc;
struct CAutoScrubDesc_s {
	CAutoScrubDesc *me;
	size_t (*next)(CAutoScrubDesc *me);
};

enum AutoScrubError {
    UnalignedStart,
    UnalignedEnd,
    UnalignedSize,
    NoScrubAreas,
    EmptyScrubArea,
    IteratorFailed,
};

typedef struct {
	bool	is_err;
	int	error;
} AutoScrubResult;


AutoScrubResult autoscrub(CCacheDesc *cache_desc, ScrubArea scrub_areas[],
	size_t n_scrub_areas, CAutoScrubDesc *auto_scrub_desc); 
