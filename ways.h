#pragma once

#include <stdint.h>
#include <vector>
#include <stdio.h>
#include <assert.h>

#include "pool.c"

using namespace std;

struct short_tag
{
    uint32_t first;
    uint32_t second;
};

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
    PoolAllocationRecordIndex parIdx;

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
        parIdx = pool->Allocate(requested_size);
        if(!parIdx.value)
        {
            assert(!"Allocation failed");
        }
        // else
        {
            begin_ = (T*)((pool->recordPage + parIdx.value)->startMemory);
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
