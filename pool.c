#include <stdint.h>
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

#ifdef __linux__
#define _GNU_SOURCE 1
#define __USE_GNU 1
#include <unistd.h>
#include <sys/mman.h>

#define MMAP_SIZE(SIZE) \
    ((uint8_t*)mmap(NULL, \
    (SIZE), \
    PROT_READ | PROT_WRITE, \
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, \
    -1, 0))

static uint8_t* Pool_AllocateNewPages(Pool* thisP, uint32_t nPages) {
    thisP->n_allocated_extra_pages += nPages;
    return MMAP_SIZE(page_size * nPages);
}

void Pool_Init(Pool* thisP) {
    if (!page_size)
        *((uint32_t*)&page_size) = sysconf(_SC_PAGE_SIZE);

    const uint8_t* first_page = MMAP_SIZE(page_size);
    if (!first_page) perror("mmap");

#define N_INITIAL_RECORD_PAGES (4096)
    thisP->recordPage = (PoolAllocationRecord*)
        Pool_AllocateNewPages(thisP, N_INITIAL_RECORD_PAGES);

    thisP->recordPage->startMemory = (uint8_t*)thisP->recordPage;
    thisP->recordPage->sizeAllocated = N_INITIAL_RECORD_PAGES * page_size;
    thisP->recordPage->sizeRequested = N_INITIAL_RECORD_PAGES * page_size;
    thisP->recordPage->used = 1;
    thisP->recordPage->pageRangeStart = 1;

    thisP->n_allocation_records = 1;
    thisP->recordsLeft = ((N_INITIAL_RECORD_PAGES * page_size) / sizeof(PoolAllocationRecord)) - 1;
    thisP->allocatedRecordPages = N_INITIAL_RECORD_PAGES;

    thisP->allocationAreaStart =
        (uint8_t*)Align16((size_t)first_page);

    thisP->sizeLeftOnCurrentPage = page_size;
#undef N_INITIAL_RECORD_PAGES
}

PoolAllocationRecordIndex Pool_Allocate(Pool* thisP, uint32_t requested_size)
{
    uint8_t* memory = nullptr;
    uint8_t pageRangeStart = 0;
    size_t aligned_size = Align16(requested_size);

    // const auto opr = pi.n_allocation_records;
    // const auto ope = pi.n_allocated_extra_pages;

    PoolAllocationRecordIndex result =
        {thisP->n_allocation_records++};

    if (thisP->recordsLeft-- == 0)
    {
#define GROWTH_FACTOR 1.6
        uint32_t oldSize = (thisP->allocatedRecordPages * page_size);

        PoolAllocationRecord par = *thisP->recordPage;
        uint32_t newSize = ((uint32_t)(thisP->allocatedRecordPages * GROWTH_FACTOR) * page_size);
        PoolAllocationRecord* newPar = Pool_Reallocate(thisP, thisP->recordPage, newSize);

        thisP->recordsLeft = (newSize - oldSize)  / sizeof(PoolAllocationRecord);
        thisP->allocatedRecordPages = newSize / page_size;

        thisP->recordPage = (PoolAllocationRecord*) newPar->startMemory;
        *thisP->recordPage = *newPar;
#undef GROWTH_FACTOR
    }

    if (thisP->sizeLeftOnCurrentPage >= aligned_size)
    {
        thisP->sizeLeftOnCurrentPage -= aligned_size;
        memory = thisP->allocationAreaStart;
        (thisP->allocationAreaStart += aligned_size);
    }
    else
    {
        if (requested_size >= page_size)
        {
            const size_t n_pages_required =
                ((requested_size + page_size) / page_size);

            aligned_size = (n_pages_required * page_size);
            {
                uint8_t* p = Pool_AllocateNewPages(thisP, n_pages_required);
                if (p)
                {
                    memory = p;
                    pageRangeStart = true;
                }
            }
        }
        else
        {
            // time to use a new page for small allocations
            uint8_t* pageMem = Pool_AllocateNewPages(thisP, 2);
            thisP->sizeLeftOnCurrentPage = (page_size * 2) - aligned_size;
            thisP->allocationAreaStart = pageMem + aligned_size;
            //TODO keep a page header and such
            memory = pageMem;
        }
    }

    if (result.value)
    {
        PoolAllocationRecord* parp = thisP->recordPage + result.value;

        thisP->wasted_bytes += (aligned_size - requested_size);
        thisP->total_allocated += aligned_size;
        assert(memory);
        parp->startMemory      = memory;
        parp->sizeRequested    = requested_size;
        parp->sizeAllocated    = aligned_size;
        parp->used             = true;
        parp->pageRangeStart   = pageRangeStart;
        assert(parp->sizeAllocated >= parp->sizeRequested);
    }

    return result;
}

PoolAllocationRecord* Pool_Reallocate(Pool* thisP, PoolAllocationRecord* parp,
                                      uint32_t requested_new_size)
{
    PoolAllocationRecord* result = nullptr;

    if (parp->sizeAllocated >= requested_new_size)
    {
        thisP->wasted_bytes -=
            (requested_new_size - parp->sizeRequested);
        parp->sizeRequested = requested_new_size;
        result = parp;
    }
    else if(parp->pageRangeStart)
    {
        size_t new_size = ((requested_new_size + page_size) / page_size) * page_size;
        void* new_mem = mremap((void*)parp->startMemory, parp->sizeAllocated,
            new_size, MREMAP_MAYMOVE);
        if (new_mem == MAP_FAILED)
        {
            perror("mremap failed");
        }
        else
        {
            if (parp == thisP->recordPage)
            {
                parp = (PoolAllocationRecord*) new_mem;
            }

            parp->sizeRequested = requested_new_size;
            parp->sizeAllocated = new_size;
            parp->startMemory = (uint8_t*)new_mem;
            result = parp;
        }
    }

    return result;
}
#else
#error("Platform not supported")
#endif

#ifdef __cplusplus
Pool::Pool()
{
    Pool_Init(this);
}

PoolAllocationRecordIndex Pool::Allocate(size_t requested_size)
{
    return Pool_Allocate(this, requested_size);
}

PoolAllocationRecord* Pool::Reallocate(PoolAllocationRecord* par, size_t requested_new_size)
{
    return Pool_Reallocate(this, par, requested_new_size);
}
#endif
