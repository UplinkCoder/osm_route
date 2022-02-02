#pragma once

#include <stdint.h>
#include <vector>
#include <stdio.h>
#include <assert.h>

using namespace std;

struct short_tag
{
    uint32_t first;
    uint32_t second;
};

template <typename T>
struct qSpan
{
    using value_type = T;

    const T* begin_;
    const T* end_;
    bool managed = false;

    constexpr const size_t size() const {
        return end_ - begin_;
    }

    qSpan() = default;

    constexpr qSpan(const T* begin, const T* end) :
        begin_(begin), end_(end) {}

    constexpr qSpan(const vector<T>& vec) :
        begin_(vec.data()), end_(begin_ + vec.size()) {}

    constexpr qSpan(const T* begin, const size_t size) :
        begin_(begin), end_(begin + size) {}

    ~qSpan() {
        if (managed && begin_) free((void*)begin_);
    }

    T *begin() const {
        return (T*)begin_;
    }

    T *end() const {
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
        begin_ = (T*)realloc((T*)begin_, n * sizeof(T));
        end_ = begin_ + n;
        managed = true;
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
