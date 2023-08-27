#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "memscrublib.h"

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
#else
#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>

class ProgramOutput {
public:
    explicit ProgramOutput(const std::string& command) {
        pipe_ = popen(command.c_str(), "r");
        if (!pipe_) {
            throw std::runtime_error("Failed to run the command.");
        }
    }

    ~ProgramOutput() {
        if (pipe_) {
            pclose(pipe_);
        }
    }

    std::istream& getStream() {
        if (!stream_) {
            stream_ = std::make_unique<std::stringstream>();
            readOutputIntoStream(*stream_);
        }
        return *stream_;
    }

private:
    void readOutputIntoStream(std::ostream& stream) {
        std::array<char, 128> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe_) != nullptr) {
            stream << buffer.data();
        }
    }

    FILE* pipe_ = nullptr;
    std::unique_ptr<std::stringstream> stream_;
};

static std::vector<ScrubArea> read_scrub_areas(std::string command) {
	std::vector<ScrubArea> scrub_areas;

	try {
		ProgramOutput pout = ProgramOutput(command);
		std::istream& outputStream = pout.getStream();

		std::string line;
		while (std::getline(outputStream, line)) {
			std::istringstream iss(line);
			std::string hexValue1, hexValue2;

			if (!(iss >> hexValue1 >> hexValue2)) {
				exit(EXIT_FAILURE);
			}

			if (hexValue1.substr(0, 2) != "0x" ||
				hexValue2.substr(0, 2) != "0x") {
				std::cerr << "Values must start with '0x': "
					<< line << std::endl;
				exit(EXIT_FAILURE);
			}

			intptr_t start = std::strtol(hexValue1.c_str(),
				nullptr, 16);
			intptr_t end = std::strtol(hexValue2.c_str(),
				nullptr, 16);

			ScrubArea scrub_area;
			scrub_area.start = (uint8_t *)start;
			scrub_area.end = (uint8_t *)end;
			scrub_areas.push_back(scrub_area);
		}
	} catch (const std::exception& e) {
		std::cerr << "Unable to read memory configuration: " <<
			e.what() << std::endl;
		exit(EXIT_FAILURE);
	}
	
	return scrub_areas;
}

static void scrub_dev_mem(std::string dev_mem,
	std::vector<ScrubArea> phys_scrub_areas) {

	// Display the values stored in the vector
	size_t total_size = 0;
	std::cout << "Physical addresses:" << std::endl;
	for (ScrubArea value : phys_scrub_areas) {
		size_t size = (char *)value.end - (char *)value.start + 1;
		std::cout << std::hex << (void *)value.start << "-"
			<< (void *)value.end << ": " << std::dec <<
			size << std::endl;
		total_size += size;
	}
	std::cout << "total size " << std::dec << total_size << std::endl;

	int fd = open(dev_mem.c_str(), O_RDONLY);
	if (fd == -1) {
		perror("/dev/mem");
		exit(EXIT_FAILURE);
	}
	std::vector<ScrubArea> virt_scrub_areas;

	for (ScrubArea scrub_area: phys_scrub_areas) {
		intptr_t start_offset = (intptr_t)scrub_area.start;
		intptr_t end_offset = (intptr_t)scrub_area.end;
		size_t length = end_offset - start_offset + 1;
		int rc = lseek(fd, start_offset, SEEK_SET);
		if (rc == -1) {
			perror("seek");
			exit(EXIT_FAILURE);
		}

		void *data = mmap(NULL, length, PROT_READ, MAP_SHARED, fd,
			start_offset);
		if (data == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}

		intptr_t end = (intptr_t)data + length - 1;
		ScrubArea virt_scrub_area;
		virt_scrub_area.start = (uint8_t *)data;
		virt_scrub_area.end = (uint8_t *)end;
		virt_scrub_areas.push_back(virt_scrub_area);
	}

	if (close(fd) == -1) {
		perror("close");
		exit(EXIT_FAILURE);
	}
}
#endif

int main(int argc, char *argv[]) {
#ifdef TEST
	test_autoscrub(argv[0]);
	return EXIT_SUCCESS;
#else
	std::string dev_mem = "/dev/mem";
	std::string command = "../memscrub/extract-memconfig";

	std::vector<ScrubArea> phys_scrub_areas = read_scrub_areas(command);
	scrub_dev_mem(dev_mem, phys_scrub_areas);
	exit(EXIT_SUCCESS);
#endif
}
