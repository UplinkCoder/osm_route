#pragma once

#include <unordered_map>
#include <vector>
#include <stdio.h>

using namespace std;

using short_tags_t = unordered_map<uint32_t, uint32_t>;

struct Way
{
    Way(uint64_t osmid_ = {}, std::vector<uint64_t> refs_ = {}, short_tags_t tags_ = {}) :
        osmid(osmid_), refs(refs_), tags(tags_) {}

    uint64_t osmid;
    std::vector<uint64_t> refs;
    short_tags_t tags;
};

// We keep every node and the how many times it is used in order to detect crossings
struct Node {
    Node() : osmid(0), uses(0), lon_m(0), lat_m(0), tags({}) {}

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
