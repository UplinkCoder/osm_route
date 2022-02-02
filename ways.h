#pragma once

#include <stdint.h>
#include <vector>
#include <stdio.h>
#include <assert.h>

#include <sys/mman.h>
#include <unistd.h>

using namespace std;

struct short_tag
{
    uint32_t first;
    uint32_t second;
};

struct PoolAllocationRecord
{
   void* startMemory;       // 8

   uint32_t sizeRequested;  // 12
   uint32_t sizeAllocated;  // 16
   uint8_t used;            // 17
   uint8_t _pad[7];         // 32
} ;

static constexpr size_t Align16(size_t n) {
    return (n + 15) & ~15;
}

static_assert(Align16(32) == 32);
static_assert(Align16(30) == 32);
static_assert(Align16(7) == 16);
static_assert(Align16(15) == 16);
static_assert(Align16(0) == 0);

static uint32_t page_size = 0;


struct Pool
{
#   define LOCAL_RECORDS 16
    uint32_t wasted_bytes = 0;
    uint32_t total_allocated = 0;
    /// information private to the memory manager.
    struct PoolInfo
    {
        uint32_t n_allocation_records = {};
        uint32_t n_allocated_extra_pages = {};
        uint32_t sizeLeftOnCurrentPage;

        uint8_t* allocationAreaStart = {};

        PoolAllocationRecord localRecords[32] = {};
        PoolAllocationRecord* recordPage = {};
    };

    PoolInfo* poolInfo;
    uint8_t* last_page;

    Pool()
    {
        if (!page_size)
            *((uint32_t*)&page_size) = sysconf(_SC_PAGE_SIZE);
#       define MMAP_SIZE(SIZE) \
            ((uint8_t*)mmap(NULL, \
            (SIZE), \
            PROT_READ | PROT_WRITE, \
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, \
            -1, 0))

        const uint8_t* first_page = MMAP_SIZE(page_size);
        perror("mmap");

        poolInfo = (PoolInfo*) first_page;
        *poolInfo = {};
        //TODO consider padding the space to the allocation
        //start area out more so we can store more meta-data
        const uint32_t reserved_memory = Align16(sizeof(PoolInfo));

        poolInfo->allocationAreaStart =
            (uint8_t*)Align16(((size_t)first_page) + reserved_memory);

        poolInfo->sizeLeftOnCurrentPage =
            page_size - reserved_memory;

        last_page = (uint8_t*)first_page;
    }

    uint8_t* AllocateNewPages(uint32_t nPages) {
        poolInfo->n_allocated_extra_pages += nPages;
        return MMAP_SIZE(page_size * nPages);
    }

    PoolAllocationRecord* Allocate(size_t requested_size)
    {
        auto & pi = *poolInfo;
        void * memory = nullptr;
        auto aligned_size = Align16(requested_size);


        const auto opr = pi.n_allocation_records;
        const auto ope = pi.n_allocated_extra_pages;

        if (pi.n_allocation_records == 734
            && pi.n_allocated_extra_pages == 24060)
        {
            int k = 22;
        }

        PoolAllocationRecord* result =
            ((pi.n_allocation_records
                < LOCAL_RECORDS) ?
                &pi.localRecords[pi.n_allocation_records++] :
                ((PoolAllocationRecord*)pi.recordPage) + (pi.n_allocation_records++ - LOCAL_RECORDS));

        if (pi.n_allocation_records == LOCAL_RECORDS)
        {
            pi.recordPage = (PoolAllocationRecord*)
                AllocateNewPages(128);
        }

        if (pi.sizeLeftOnCurrentPage >= aligned_size)
        {
            pi.sizeLeftOnCurrentPage -= aligned_size;
            memory = pi.allocationAreaStart;
            (pi.allocationAreaStart += aligned_size);
        }
        else
        {
            if (requested_size >= page_size)
            {
                const auto n_pages_required =
                    ((requested_size + page_size) / page_size);

                aligned_size = (n_pages_required * page_size);

                if (auto p = AllocateNewPages(n_pages_required))
                {
                    memory = p;
                }
            }
            else
            {
                // time to use a new page for small allocations
                auto pageMem = AllocateNewPages(2);
                poolInfo->sizeLeftOnCurrentPage = (page_size * 2) - aligned_size;
                poolInfo->allocationAreaStart = pageMem + aligned_size;
                //TODO keep a page header and such
                memory = pageMem;
            }
        }


        if (result)
        {
            wasted_bytes += (aligned_size - requested_size);
            total_allocated += aligned_size;
            assert(memory);
            result->startMemory   = memory;
            result->sizeRequested = requested_size;
            result->sizeAllocated = aligned_size;
            result->used          = true;
            assert(result->sizeAllocated >= result->sizeRequested);
        }

        return result;
    }

#   undef LOCAL_RECORDS
} ;

enum MemoryFlags
{
    Unknwon,

    Reserved1 = (1 << 0),

    External = (1 << 1),
    PoolManaged = (1 << 2),

} ;

template <typename T>
struct qSpan
{
    using value_type = T;

    const T* begin_;
    const T* end_;

    MemoryFlags memoryFlags = MemoryFlags::PoolManaged;
    PoolAllocationRecord* par;

    constexpr const size_t size() const {
        return end_ - begin_;
    }

    qSpan() = default;

    qSpan(size_t n, Pool* pool) {
        AllocFromPool(n, pool);
    }

    constexpr qSpan(const T* begin, const T* end) :
        begin_(begin), end_(end),
            memoryFlags(MemoryFlags::External) {}

    constexpr qSpan(const vector<T>& vec) :
        begin_(vec.data()), end_(begin_ + vec.size()),
                memoryFlags(MemoryFlags::External) {}

    constexpr qSpan(const T* begin, const size_t size) :
        begin_(begin), end_(begin + size),
            memoryFlags(MemoryFlags::External) {}


    void FreeMemory(void) {
        if (memoryFlags & MemoryFlags::External)
            assert(!"External memory must not be freed");

        assert(!"Not implemented");
    }

    T* begin(void) const {
        return (T*)begin_;
    }

    T* end(void) const {
        return (T*)end_;
    }

    T& operator[] (uint32_t index) {
        assert(index < (uint32_t)(end_ - begin_));
        return ((T*)begin_)[index];
    }

    constexpr const T& operator[] (uint32_t index) const {
        return begin_[index];
    }

    constexpr const T& back(void) {
        return end_[-1];
    }

    void resize(size_t n)
    {
        assert(!(memoryFlags & MemoryFlags::External));

        if (memoryFlags & MemoryFlags::PoolManaged)
        {
            assert(!"Not Implemented");
            // begin_ = (T*)realloc((T*)begin_, n * sizeof(T));
            // begin_ = Realloc()
            end_ = begin_ + n;
        }
        else
        {
            assert(0);
        }
    }

    void AllocFromPool(size_t n, Pool* pool)
    {
        if (!n)
            return ;
        size_t requested_size = n * sizeof(T);
        par = pool->Allocate(requested_size);
        if(!par)
        {
            assert(!"Allocation failed");
        }
        // else
        {
            begin_ = (T*)par->startMemory;
            end_ = begin_ + n;
        }
    }
};

using short_tags_t = qSpan<short_tag>;

struct Way
{
    Way(uint64_t osmid_ = {}, qSpan<uint64_t> refs_ = {}, short_tags_t tags_ = {}) :
        osmid(osmid_), refs(refs_), tags(tags_) {}

    uint64_t osmid;

    qSpan<uint64_t> refs;

    short_tags_t tags;
};

// We keep every node and the how many times it is used in order to detect crossings
struct Node {
    Node() = default;

    Node(uint64_t osmid_, double lon, double lat, short_tags_t tags_) :
        osmid(osmid_), uses(0), lon_m(lon), lat_m(lat), tags(tags_) {}

    uint64_t osmid;
    int32_t uses;
    double lon_m;
    double lat_m;
    //Tags tags;
    short_tags_t tags;

    void print_node()
    {
        printf("id: %lu, lon: %f, lat: %f, {#tags %d}\n"
            , osmid
            , lon_m
            , lat_m
            , (int)tags.size()
        );
    }
};
