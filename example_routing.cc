/*
Copyright (c) 2012, Canal TP
This is an example file, do whatever you want with it! (for example if you are in Paris, invite us for a beer)
A slightly more advanced example : we want to use OSM data to extract a graph that represents the road network
Build the example with (we're to lazy to make it compatible with older standards):
g++ example_routing.cc -O2 -losmpbf -lprotobuf -std=c++0x -o routing
To run it:
./routing path_to_your_data.osm.pbf
*/

#include <unordered_map>
#include <map>
#include <set>
#include "osmpbfreader.h"
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "endian.h"
#include "int_to_str.c"

#if (__cplusplus <= 201500)
#    include "llvm_string_view.hpp"
     using string_view = StringView;
#else
#  include <string_view>
#endif

#ifdef TEST_MAIN
#  define HAD_TEST_MAIN_ROUTING
#  undef TEST_MAIN
#endif

#include "crc32.c"
#include "serializer.cpp"

#ifdef HAD_TEST_MAIN_ROUTING
#  define TEST_MAIN
#endif

#define cast(T) (T)
#define MAYBE_UNUSED(expr) \
    do { (void)(expr); } while (0)

using namespace CanalTP;
using namespace std;

struct Offsets
{
   const uint32_t NameTable = 0;
   const uint32_t ValueTable = 4;
//   const uint32_t

} ;


template <typename map_t>
bool CanFind(const map_t& map, const typename map_t::value_type && key_value) {
    bool result = false;
    for(auto it = map.find(key_value.first);
        it != map.end();
        it++)
    {
        if (it->second == key_value.second)
        {
            result = true;
            break;
        }
    }
    return result;
}

string_view findName(const Tags tags)
{
    string_view name = {};
    auto entry = tags.find("name");
    if (entry != tags.end()) {
        name = entry->second;
    }
    return name;
}


#include "ways.h"


#include "deserialize.cpp"

struct SerializeWays
{
    // the following fields get serialized.
    StringTable tag_names {
#        include "prime_names.h"
    };
    StringTable tag_values {};
    set<uint32_t> street_name_indicies {};
    std::unordered_map<uint64_t, Node> nodes;
    vector<Way> ways;
    Pool* pool;

    // everthing below is just serialisation state

    uint64_t currentBaseNode = 0;
    uint8_t dependent_nodes[255];
    uint8_t n_dependent_nodes = 0;

    vector<pair<uint64_t, uint8_t> > baseNodes = {};
    vector<vector<uint8_t> > childNodes {};


    short_tags_t ShortenTags(const Tags& tags) {
        short_tags_t result = {};
        {
            uint32_t idx = 0;
            result.AllocFromPool(tags.size(), pool);

            for(auto it = tags.begin();
                it != tags.end();
                it++)
            {
                auto name_id = tag_names.AddString(it->first);
                auto value_id = tag_values.AddString(it->second);
                result[idx++] = {name_id, value_id};
            }
        }

        return result;
    }

    void WriteTags(Serializer& serializer, const short_tags_t& tags)
    {
        serializer.WriteShortUint(tags.size());
        for(const auto & p : tags)
        {
            serializer.WriteShortUint(p.first);
            serializer.WriteShortUint(p.second);
        }
    }

    void node_callback(uint64_t osmid, double lon, double lat, const Tags &tags) {
        auto nDiff = ((int64_t)(osmid - currentBaseNode));
        // printf("node_id: %lu .. currentBaseNode: %lu - nDiff: %lu\n", osmid, currentBaseNode, nDiff)
        if (nDiff > 255)
        {
            baseNodes.push_back({currentBaseNode, n_dependent_nodes});
            vector<uint8_t> dependent_nodes_v {};
            for(int i = 0; i < n_dependent_nodes; i++)
            {
                dependent_nodes_v.push_back(dependent_nodes[i]);
            }
            childNodes.push_back(dependent_nodes_v);
            currentBaseNode = osmid;
            n_dependent_nodes = 0;
        }
        else
        {
            assert(nDiff > 0 && nDiff <= 255);
            dependent_nodes[n_dependent_nodes++] = (uint8_t)nDiff;
        }

        const auto street = tags.find("addr:street");
        if (street != tags.end())
        {
            const auto street_name_index = tag_values.LookupString(street->second);
            street_name_indicies.emplace(street_name_index);
        }
        this->nodes[osmid] = Node(osmid, lon, lat, ShortenTags(tags));
    }

    // This method is called every time a Way is read
    void way_callback(uint64_t osmid, const Tags &tags, const std::vector<uint64_t> &refs){
        // If the way is part of the road network we keep it
        // There are other tags that correspond to the street network, however for simplicity, we don't manage them
        // Homework: read more properties like oneways, bicycle lanes…

        if(tags.find("highway") != tags.end()) {
            const auto& name = findName(tags);

            uint32_t name_index = 0;
            if (name.size())
            {
                name_index = tag_values.LookupString(name);
                street_name_indicies.emplace(name_index);
            }
        }
        ways.push_back({osmid, refs, ShortenTags(tags)});
    }

    // We don't care about relations
    void relation_callback(uint64_t /*osmid*/, const Tags &/*tags*/, const References & /*refs*/){}

    void Serialize (Serializer& serializer)
    {
        // First we reserve space for the index
        const auto index_p = serializer.CurrentPosition();
        serializer.WriteU32(index_p + 20); // beginning tag names
        serializer.WriteU32(0); // beginning tag values
        serializer.WriteU32(0); // beginning street_names
        serializer.WriteU32(0); // beginning nodes
        serializer.WriteU32(0); // beginning ways

        {
            // then we serialze the tags
            clock_t serialize_tags_begin = clock();
            {
                tag_names.Serialize(serializer);
                const auto tag_value_pos = serializer.CurrentPosition();
                tag_values.Serialize(serializer);
                // after serializng we go back and poke the value in
                const auto after_tag_values = serializer.SetPosition(index_p + 4);
                serializer.WriteU32(tag_value_pos);
                serializer.SetPosition(after_tag_values);
            }
            clock_t serialize_tags_end = clock();

            printf("serialisation of tags took %f milliseconds\n",
                ((serialize_tags_end - serialize_tags_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);
        }

        // Now we serialize the street_name indecies
        {
            clock_t serialize_street_names_begin = clock();
            {
                const auto street_name_position = serializer.CurrentPosition();
                const auto oldP = serializer.SetPosition(index_p + 8);
                {
                    serializer.WriteU32(street_name_position);
                }
                serializer.SetPosition(oldP);

                serializer.WriteU32(street_name_indicies.size());
                for(auto& e : street_name_indicies)
                    serializer.WriteShortUint(e);
            }
            clock_t serialize_street_names_end = clock();
            printf("serialisation of street names took %f milliseconds\n",
                ((serialize_street_names_end - serialize_street_names_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);
        }

        // now we serialze the nodes.
        // this will use base_nodes and delta coding
        printf("number of base_nodes %u\n", (uint32_t) baseNodes.size());
        printf("number of all nodes %u\n", (uint32_t) nodes.size());
        {
            const auto nodes_start = serializer.CurrentPosition();
            const auto oldP = serializer.SetPosition(index_p + 12);
            serializer.WriteU32(nodes_start);
            serializer.SetPosition(oldP);
        }


        // serializer.BeginField("vector<pair<uint64, relativeNodes>>"
        clock_t serialize_bNodes_begin = clock();
        {
            serializer.WriteU32(nodes.size());

            int idx = 0;

            serializer.WriteU32(baseNodes.size());

            for(auto b : baseNodes)
            {
                const auto base_id = b.first;
                const auto & base_node = nodes[base_id];
                serializer.WriteU64(base_id);
                // writing out the number of relative nod
                serializer.WriteF64(base_node.lat_m);
                serializer.WriteF64(base_node.lon_m);

                WriteTags(serializer, base_node.tags);
                // number of children
                serializer.WriteU8(b.second);
                // child list
                auto child_list = childNodes[idx];
                for(int i = 0;
                    i < b.second;
                    i++)
                {
                    serializer.WriteU8(child_list[i]);
                    // id offset from base no
                    auto & child = nodes[base_id + child_list[i]];

                    serializer.WriteF64(child.lat_m);
                    serializer.WriteF64(child.lon_m);

                    WriteTags(serializer, child.tags);
                }
                idx++;
            }
        }
        clock_t serialize_bNodes_end = clock();
        printf("serialisation of baseNodes took %f milliseconds\n",
            ((serialize_bNodes_end - serialize_bNodes_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);

        {
            const auto ways_start = serializer.CurrentPosition();
            const auto oldP = serializer.SetPosition(index_p + 16);
            {
                serializer.WriteU32(ways_start);
            }
            serializer.SetPosition(oldP);
        }

        clock_t serialize_ways_begin = clock();
        {
            const auto n_ways = ways.size();
            serializer.WriteU32(n_ways);
            {
                uint64_t lastOsmId = 0;
                for(uint32_t widx = 0;
                    widx < n_ways;
                    widx++)
                {
                    const auto& w = ways[widx];

                    serializer.WriteShortInt(w.osmid - lastOsmId);
                    lastOsmId = w.osmid;
                }
            }

            for(uint32_t widx = 0;
                widx < n_ways;
                widx++)
            {
                const auto& w = ways[widx];

                WriteTags(serializer, w.tags);

                uint64_t base_ref;
                const auto n_refs = w.refs.size();
                serializer.WriteShortUint(n_refs);
                if (n_refs)
                {
                    base_ref = w.refs[0];
                    serializer.WriteU64(base_ref);

                    for(uint32_t i = 1;
                        i < n_refs;
                        i++)
                    {
                        int64_t diff = w.refs[i] - base_ref;
                        if (diff != 0 && FitsInShortInt(diff))
                        {
                            // if there is a dupllicate node we have to use the long encoding
                            // as a difference of 0 i reserved for the out of range
                            // to switch to the long encoding
                            serializer.WriteShortInt(w.refs[i] - base_ref);
                        }
                        else
                        {
                            serializer.WriteU8(0);
                            // a 0 can't be a vaild ShortInt in this case.
                            // therefore the deserializer knows that a raw U64 comes in
                            serializer.WriteU64(w.refs[i]);
                        }
                    }
                }
            }
        }
        clock_t serialize_ways_end = clock();

        printf("serialisation of ways took %f milliseconds\n",
            ((serialize_ways_end - serialize_ways_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);
    }
};

struct Routing {
    // Map that stores all the nodes read
    std::unordered_map<uint64_t, Node> nodes;

    // Stores all the nodes of all the ways that are part of the road network
    // ulong[][] ways;
    std::vector<Way> ways;

    // This method is called every time a Node is read
    void node_callback(uint64_t osmid, double lon, double lat, const Tags &tags) {
        this->nodes[osmid] = Node(osmid, lon, lat, {});
    }

    // This method is called every time a Way is read
    void way_callback(uint64_t osmid, const Tags &tags, const std::vector<uint64_t> &refs){
        // If the way is part of the road network we keep it
        // There are other tags that correspond to the street network, however for simplicity, we don't manage them
        // Homework: read more properties like oneways, bicycle lanes…
        if(tags.find("highway") != tags.end()) {
            ways.push_back({osmid, refs, {}});
        }
    }

    // Once all the ways and nodes are read, we count how many times a node is used to detect intersections
    void count_nodes_uses() {
        for(auto way : ways){
            auto refs = way.refs;
            for(uint64_t ref : refs) {
                nodes.at(ref).uses++;
            }
            // make sure that the last node is considered as an extremity
            nodes.at(refs.back()).uses++;
        }
    }

    // Returns the source and target node of the edges
    std::vector< std::pair<uint64_t, uint64_t> > edges() {
        std::vector< std::pair<uint64_t, uint64_t> > result;

        for(auto way : ways) {
            auto refs = way.refs;
            if(refs.size() > 0) {
                uint64_t source = refs[0];
                for(size_t i = 1; i < refs.size(); ++i) {
                    uint64_t current_ref = refs[i];
                    // If a node is used more than once, it is an intersection, hence it's a node of the road network graph
                    if(nodes.at(current_ref).uses > 1) {
                        // Homework: measure the length of the edge
                        uint64_t target = current_ref;
                        //auto src_node = nodes[source];
                        //auto tgt_node = nodes[target];
                        result.push_back(std::make_pair(source, target));
                        source = target;
                    }
                }
            }
        }
        return result;
    }

    // We don't care about relations
    void relation_callback(uint64_t /*osmid*/, const Tags &/*tags*/, const References & /*refs*/){}
};

int main(int argc, char** argv) {
     if(argc != 2 && argc != 3) {
        std::cout << "Usage: " << argv[0] << " file_to_read.osm.pbf" << std::endl;
        return 1;
    }

    // Let's read that file !
//    Routing routing;
//    read_osm_pbf(argv[1], routing);
//    std::cout << "We read " << routing.nodes.size() << " nodes and " << routing.ways.size() << " ways" << std::endl;
//    routing.count_nodes_uses();
//    std::cout << "The routing graph has " << routing.edges().size() << " edges" << std::endl;

    SerializeWays serializeWays;
    Pool pool {};
    serializeWays.pool = &pool;
    read_osm_pbf(argv[1], serializeWays);

    serializeWays.tag_values.SortUsageCounts();
    serializeWays.tag_names.SortUsageCounts();

//    serializeWays.tag_values.ReorderByUsage();
//    serializeWays.tag_names.ReorderByUsage();

/*
    {
        #define A(IDX) \
            (float)array[IDX].second / (float)array[0].second, strings[array[IDX].first].c_str()
        {
            printf("relative usage of the first 127 tag_names: (relative to the most used tag) \n");
            auto array = serializeWays.tag_names.usage_counts.data();
            auto strings = serializeWays.tag_names;

            for(int i = 0; i < 127; i += 10) {
                printf("%d: %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s') || %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s')\n",
                i,
                A(i + 0), A(i + 1), A(i + 2), A(i + 3), A(i + 4),
                A(i + 5), A(i + 6), A(i + 7), A(i + 8), A(i + 9));
            }
        }

        {
            printf("relative usage of the first 180 tag_values: (relative to the most used tag) \n");
            auto array = serializeWays.tag_values.usage_counts.data();
            auto strings = serializeWays.tag_values;
            for(int i = 0;  i < 180; i += 10)
            {
                printf("%d: %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s') || %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s'), %f ('%s')\n",
                i,
                A(i + 0), A(i + 1), A(i + 2), A(i + 3), A(i + 4),
                A(i + 5), A(i + 6), A(i + 7), A(i + 8), A(i + 9));
            }
        }
        #undef A
   }
*/
    {
        Serializer s {"tags.dat", Serializer::serialize_mode_t::Writing};

        serializeWays.Serialize(s);
    }
    {
        Serializer d {"tags.dat", Serializer::serialize_mode_t::Reading};

        DeSerializeWays ws;
        Pool pool {};
        ws.DeSerialize(d, &pool);
    }
    return 0;
}

/*
 * building | yes • house • residential • garage
   source   | BAG • Bing • cadastre-dgi-fr␣source␣:␣Direction␣Générale␣des␣Impôts␣-␣Cadastre.␣Mise␣à␣jour␣:␣2010 • cadastre-dgi-fr␣source␣:␣Direction␣Générale␣des␣Impôts␣-␣Cadastre.␣Mise␣à␣jour␣:␣2011 • cadastre-dgi-fr␣source␣:␣Direction␣Générale␣des␣Impôts␣-␣Cadastre.␣Mise␣à␣jour␣:␣2012 • bing • NRCan-CanVec-10.0 • microsoft/BuildingFootprints • digitalglobe • YahooJapan/ALPSMAP
   highway  | residential • service • track • footway • unclassified • path • tertiary • crossing • secondary • primary
   addr:housenumber |  1 • 2 • 3 • 4 • 5 • 6 • 7 • 8 • 10 • 9
   addr:street      |
   addr:city        |
   name             |
   addr:postcode    |
   natural          | tree • water • wood • scrub • wetland • grassland • coastline • tree_row • peak • bare_rock
   surface          | asphalt • unpaved • paved • ground • concrete • paving_stones • gravel • dirt • grass • compacted
   landuse          | residential • farmland • grass • forest • meadow • orchard • farmyard • industrial • vineyard • cemetery
   addr:country     | DE • DK • EG • AT • US • CZ • RU • SK • IT • EE
   source:date      | 2014-03-24 • 2014-02-11 • 2014-05-07 • 2013-11-26 • 2018-12-14 • 201011 • 2014-05-20 • 1989-07-01
   power            | tower • pole • generator • line • minor_line • substation
   waterway         | stream • ditch • river • drain • canal
   building:levels  |  1 • 2 • 3 • 4 • 5 • 6
   amenity          | parking • bench • place_of_worship • restaurant • school • parking_space • waste_basket • fuel • cafe • fast_food
   service          | driveway • parking_aisle • alley • yard • spur • siding
   oneway           | yes • no
   barrier          | fence • gate • wall • hedge • kerb • bollard • lift_gate • retaining_wall
   access           | private • yes • customers • no • permissive • destination
   height           | 3 • 6 • 5 • 4 • 2 • 7 • 10
   start_date       | 1970 • 1950 • 1972 • 1975 • 1960 • 1980 • 1973 • 1965 • 1974 • 1968
   ref
   addr:state       | NY • FL • CT • CA • MD • ME • KY • AZ • NC • CO
   maxspeed         | 50 • 30 • 40 • 60 • 80 • 30␣mph • 70 • 100 • 25␣mph • 20
 */
