// For instantiating architecture-specfic objects for the memory
// scrubbing code

#include <stdint.h>

// This are things that I expected cbindgen would generate, but it didn't
typedef uint64_t ECCData;

typedef struct {
	void *start;
	void *end;
} ScrubArea;

enum Error {
    UnalignedStart,
    UnalignedEnd,
    UnalignedSize,
    NoScrubAreas,
    EmptyScrubArea,
    IteratorFailed,
};

#include <memscrublib_base.h>
#include <memscrublib_arch.h>
#include "memscrub.h"
