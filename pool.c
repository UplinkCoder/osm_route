#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>

#include "pool.h"

#ifdef __TINYC__
#  define CONSTEXPR
#  define static_assert(...)
#elif __cplusplus
#  define CONSTEXPR constexpr
#else
#  define CONSTEXPR
#endif

static CONSTEXPR size_t Align16(size_t n) {
    return (n + 15) & ~15;
}

#ifdef __cplusplus
static_assert(Align16(32) == 32);
static_assert(Align16(30) == 32);
static_assert(Align16(7) == 16);
static_assert(Align16(15) == 16);
static_assert(Align16(0) == 0);
#endif

#ifndef __cplusplus
#  define true 1
#  define nullptr ((void*)0)
#endif

static uint32_t page_size = 0;

#define MMAP_SIZE(SIZE) \
    ((uint8_t*)mmap(NULL, \
    (SIZE), \
    PROT_READ | PROT_WRITE, \
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, \
    -1, 0))

uint8_t* Pool_AllocateNewPages(Pool* thisP, uint32_t nPages) {
    thisP->poolInfo->n_allocated_extra_pages += nPages;
    return MMAP_SIZE(page_size * nPages);
}

void Pool_Init(Pool* thisP)
{
    if (!page_size)
        *((uint32_t*)&page_size) = sysconf(_SC_PAGE_SIZE);


    const uint8_t* first_page = MMAP_SIZE(page_size);
    if (!first_page) perror("mmap");

    PoolInfo* poolInfo = (PoolInfo*) first_page;

    //TODO consider padding the space to the allocation
    //start area out more so we can store more meta-data
    const uint32_t reserved_memory = Align16(sizeof(PoolInfo));

    poolInfo->allocationAreaStart =
        (uint8_t*)Align16(((size_t)first_page) + reserved_memory);

    poolInfo->sizeLeftOnCurrentPage =
        page_size - reserved_memory;

    thisP->poolInfo = poolInfo;
}

PoolAllocationRecord* Pool_Allocate(Pool* thisP, uint32_t requested_size)
{
    PoolInfo* pi = thisP->poolInfo;
    uint8_t* memory = nullptr;
    size_t aligned_size = Align16(requested_size);

    // const auto opr = pi.n_allocation_records;
    // const auto ope = pi.n_allocated_extra_pages;

    PoolAllocationRecord* result =
        ((pi->n_allocation_records
            < LOCAL_RECORDS) ?
            &pi->localRecords[pi->n_allocation_records++] :
            ((PoolAllocationRecord*)pi->recordPage) + (pi->n_allocation_records++ - LOCAL_RECORDS));

    if (pi->n_allocation_records == LOCAL_RECORDS)
    {
        pi->recordPage = (PoolAllocationRecord*)
            Pool_AllocateNewPages(thisP, 4096);
    }

    if (pi->sizeLeftOnCurrentPage >= aligned_size)
    {
        pi->sizeLeftOnCurrentPage -= aligned_size;
        memory = pi->allocationAreaStart;
        (pi->allocationAreaStart += aligned_size);
    }
    else
    {
        if (requested_size >= page_size)
        {
            const size_t n_pages_required =
                ((requested_size + page_size) / page_size);

            aligned_size = (n_pages_required * page_size);
            {
                uint8_t* p = 0;
                if (Pool_AllocateNewPages(thisP, n_pages_required))
                {
                    memory = p;
                }
            }
        }
        else
        {
            // time to use a new page for small allocations
            uint8_t* pageMem = Pool_AllocateNewPages(thisP, 2);
            pi->sizeLeftOnCurrentPage = (page_size * 2) - aligned_size;
            pi->allocationAreaStart = pageMem + aligned_size;
            //TODO keep a page header and such
            memory = pageMem;
        }
    }


    if (result)
    {
        thisP->wasted_bytes += (aligned_size - requested_size);
        thisP->total_allocated += aligned_size;
        assert(memory);
        result->startMemory   = memory;
        result->sizeRequested = requested_size;
        result->sizeAllocated = aligned_size;
        result->used          = true;
        assert(result->sizeAllocated >= result->sizeRequested);
    }

    return result;
}
#undef LOCAL_RECORDS

#ifdef __cplusplus
Pool::Pool()
{
    Pool_Init(this);
}

PoolAllocationRecord* Pool::Allocate(size_t requested_size)
{
    return Pool_Allocate(this, requested_size);
}
#endif
