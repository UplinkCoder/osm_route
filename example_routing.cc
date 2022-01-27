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

#if (__cplusplus <= 201500)
#  include "string_view.hpp"
   using string_view = bpstd::string_view;
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

using namespace CanalTP;
using namespace std;
using short_tags_t = unordered_map<uint32_t, uint32_t>;

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

struct Way
{
    Way(uint64_t osmid_, std::vector<uint64_t> refs_, short_tags_t tags_) :
        osmid(osmid_), refs(refs_), tags(tags_) {}

    uint64_t osmid;
    std::vector<uint64_t> refs;
    short_tags_t tags;
};

string_view findName(const Tags tags)
{
    string_view name = {};
    auto entry = tags.find("name");
    if (entry != tags.end()) {
        name = entry->second;
    }
    return name;
}

struct StringEntry
{
    uint32_t crc32;
    uint32_t length;
    uint32_t offset;
};

#define SORT_VEC(VEC) \
    VEC.data(), VEC.size(), sizeof(decltype(VEC)::value_type)

struct StringTable
{
    uint32_t AddString(const string_view & str) {
        // cerr << "called " << __FUNCTION__ << " (" << str << ")" << endl;

        const auto crc =
            FINALIZE_CRC32C(crc32c(INITIAL_CRC32C, str.data(), str.size()));
        uint32_t idx = 0;

        for (auto it = crc32_to_indecies.find(crc);
            it != crc32_to_indecies.end();
            it++)
        {
            const auto entry = strings[it->second - 1];
            const auto canidate = string_view {&string_data[entry.offset], entry.length};
            if (canidate == str)
            {
                idx = it->second;
                usage_counts[idx - 1].second++;
                break;
            }
        }

        if (!idx)
        // couldn't find the string insert it
        {
            uint32_t offset = (uint32_t) string_data.size();
            StringEntry entry { crc, (uint32_t)str.size(), offset };
            string_data.insert(string_data.end(), str.begin(), str.end());
            string_data.push_back('\0');
            strings.push_back(entry);
            idx = strings.size();
            usage_counts.push_back({idx, 1});
            crc32_to_indecies.emplace(crc, idx);
        }
        return idx;
    }

    ~StringTable() = default;

    StringTable(vector<const char*> primer = {}) : string_data(), strings(), crc32_to_indecies() {
        for(auto &e : primer)
        {
            AddString(string_view {e, strlen(e)} );
        }
    }

    // private:
    std::vector<char> string_data;
    std::vector<StringEntry> strings;
    map<uint32_t, uint32_t> crc32_to_indecies;
    std::vector<std::pair<uint32_t, uint32_t> > usage_counts;

    // public :

    void assert_sorted(void)
    {
        uint32_t lastCrc = 0;
        for(auto s : strings)
        {
            uint32_t s_crc32 = s.crc32;
            assert(s_crc32 >= lastCrc);
            lastCrc = s.crc32;
        }
        return ;
    }

    void SortUsageCounts(void) {
        qsort(SORT_VEC(usage_counts),
        [] (const void* ap, const void* bp) -> int {
            int result = 0;
            auto a = (decltype(usage_counts)::value_type*) ap;
            auto b = (decltype(usage_counts)::value_type*) bp;

            result = (b->second - a->second);
            return result;
        });
    }


    void operator+= (const string_view & str) {
        (void)AddString(str);
    }

    /// Returns 0 if not found or the index of the string_entry + 1
    uint32_t LookupCString(const char* str) {
        return LookupString(string_view {str, strlen(str)});
    }

    uint32_t LookupString(const string_view& str) {
        const char* str_data = str.data();
        const auto str_size = str.size();
        const uint32_t crc_input =
            FINALIZE_CRC32C(crc32c(INITIAL_CRC32C, str.data(), str.size()));

        uint idx = 0;
        // using entry_t = decltype(strings)::value_type;
        for (auto it = crc32_to_indecies.find(crc_input);
             it != crc32_to_indecies.end();
             it++
        ) {
            const auto s = strings[it->second - 1];
            if (str_size == s.length
                && 0 == strncmp(&string_data[s.offset], str_data, str_size))
            {
                idx = (it->second);
                break;
            }
        }

        return idx;
    }

    string_view LookupId(uint32_t idx) {
        if (!idx || idx > strings.size())
        {
            return {(const char*)0, 0};
        }
        else
        {
            const auto entry = strings[idx - 1];
            return string_view {&string_data[entry.offset], entry.length};
        }
    }

    string_view operator[] (uint32_t idx) {
        return LookupId(idx);
    }

    uint32_t operator[] (const string_view& str) {
        return LookupString(str);
    }

    void Serialize (Serializer& serializer) {
        // serialize string data.
        // serializer.BeginField("vector<char>", "string_data");
        {
            serializer.WriteU32(string_data.size());
            const auto begin = (const char*)string_data.data();
            auto ptr = (const char*)string_data.data();
            uint32_t size_left = string_data.size();
            uint32_t bytes_written = 0;

            do {
                bytes_written = serializer.WriteRawData(ptr, size_left);
                ptr += bytes_written;
                size_left -= bytes_written;
            } while(bytes_written);
            assert((uint32_t)(ptr - begin) == string_data.size());
        }
        // serializer.EndField();

        // we only need to store the number of strings as we can
        // reproduce the string entires by reading in the null
        // terminated strings in order.
        serializer.WriteU32(strings.size());
    }

    void DeSerialize (Serializer& serializer) {
        // serialize string data.

        // serializer.BeginField("vector<char>", "string_data");
        {
            uint32_t n_chars = serializer.ReadU32();
            string_data.resize(n_chars);

            const auto begin = (const char*)string_data.data();
            auto ptr = string_data.data();
            uint32_t bytes_read = 0;
            uint32_t size_left = n_chars;
            do {
                bytes_read = serializer.ReadRawData(ptr, size_left);
                ptr += bytes_read;
                size_left -= bytes_read;
            } while(bytes_read);
            assert((uint32_t)(ptr - begin) == string_data.size());
        }
        // serializer.EndField();

        // let's recreate the strings vector
        {
            auto n_strings = serializer.ReadU32();

            const char * const string_data_begin = string_data.data();
            const char* string_ptr = string_data.data();

            strings.resize(n_strings);


            for(StringEntry& e : strings)
            {
                const char* const word_begin = string_ptr;
                e.offset = (uint32_t)(word_begin - string_data_begin);

                uint32_t length = strlen(word_begin);
                assert(length);

                e.length = length;
                e.crc32 = FINALIZE_CRC32C(
                    crc32c(INITIAL_CRC32C, string_ptr, length)
                );
                string_ptr += length;
                string_ptr++;
            }
            assert((uint32_t)(string_ptr - string_data_begin) == string_data.size());
        }

        {
            // now recreate the map
            // crc32_to_indecies
            int idx = 1;
            for(auto& e : strings)
            {
                crc32_to_indecies.emplace(e.crc32, idx++);
            }
        }
    }
};

#undef SORT_VEC


struct SerializeWays
{
    std::unordered_map<uint64_t, Node> nodes;

    uint64_t max_node_id = 0;
    uint64_t min_node_id = ~0UL;

    uint64_t currentBaseNode;
    uint8_t dependent_nodes[255];
    uint8_t n_dependent_nodes;

    vector<pair<uint64_t, uint8_t> > baseNodes = {};
    vector<vector<uint8_t> > childNodes {};

    set<uint32_t> street_names_indicies {};

    StringTable tag_names {
#        include "prime_names.h"
    };
    StringTable tag_values {};
    vector<Way> ways;

    short_tags_t ShortenTags(const Tags& tags) {
        short_tags_t result = {};

        for(auto it = tags.begin();
            it != tags.end();
            it++)
        {
            auto name_id = tag_names.AddString(it->first);
            auto value_id = tag_values.AddString(it->second);
            result.emplace(name_id, value_id);
        }

        return result;
    }

    void node_callback(uint64_t osmid, double lon, double lat, const Tags &tags) {
        if (osmid > max_node_id)
            this->max_node_id = osmid;
        if (osmid < min_node_id)
            this->min_node_id = osmid;

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
                street_names_indicies.emplace(name_index);
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
        serializer.WriteU32(index_p + 16); // beginning tag names
        serializer.WriteU32(0); // beginning tag values
        serializer.WriteU32(0); // beginning street_names
        serializer.WriteU32(0); // beginning nodes


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
        // now we serialze the nodes.
        // this will use base_nodes and delta coding
        printf("number of base_nodes %u\n", (uint32_t) baseNodes.size());
        printf("number of all nodes %u\n", (uint32_t) nodes.size());
        // serializer.BeginField("vector<pair<uint64, relativeNodes>>"
        clock_t serialize_bNodes_begin = clock();
        int idx = 0;

        for(auto b : baseNodes)
        {
/*
            if (idx == 2)
            {
                printf("base: ");
                const auto node_id = b.first;
                auto base_node = nodes[node_id];
                base_node.print_node();
                printf ("# children: %d\n", b.second);
                auto child_list = childNodes[idx];
                for(int i = 0;
                    i < b.second;
                    i++)
                {
                    printf("  ");
                    auto & child = nodes[node_id + child_list[i]];
                    child.print_node();
                }
            }
*/
            // high bytes
            const auto base_id = b.first;
            const auto & base_node = nodes[base_id];
            serializer.WriteU32((uint32_t)(base_id >> 32));
            // low bytes
            serializer.WriteU32((uint32_t)base_id);
            // writing out the number of relative nod
            serializer.WriteF64(base_node.lat_m);
            serializer.WriteF64(base_node.lon_m);
            serializer.WriteShortUint(base_node.tags.size());
            for(const auto & p : base_node.tags)
            {
                serializer.WriteShortUint(p.first);
                serializer.WriteShortUint(p.second);
            }

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
                serializer.WriteShortUint(child.tags.size());

                for(const auto & p : child.tags)
                {
                    serializer.WriteShortUint(p.first);
                    serializer.WriteShortUint(p.second);
                }
            }
            idx++;
        }
        clock_t serialize_bNodes_end = clock();
        printf("serialisation of baseNodes took %f milliseconds\n",
            ((serialize_bNodes_end - serialize_bNodes_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);

        // Now we serialize the street_name indecies
        {
            clock_t serialize_street_names_begin = clock();
            {
                const auto street_name_position = serializer.CurrentPosition();

            }
            clock_t serialize_street_names_end = clock();
        printf("serialisation of street names took %f milliseconds\n",
            ((serialize_street_names_end - serialize_street_names_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);
        }
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
                        auto src_node = nodes[source];
                        auto tgt_node = nodes[target];
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
    read_osm_pbf(argv[1], serializeWays);


    uint32_t maxTags = 0;
/*
    for (auto& nt : serializeWays.nodes)
    {
        auto &node = nt.second;
        if (node.tags.size() > maxTags)
            maxTags = node.tags.size();

        for(auto it = node.tags.begin();
            it != node.tags.end();
            it++)
        {
            printf("  %s : %s\n", serializeWays.tag_names[it->first].data(),
                                  serializeWays.tag_values[it->second].data());
        }
        printf("\n");
    }
*/
    printf("max number of tags: %d\n", maxTags);
    printf("max number of tag-keys: %d\n", (int)serializeWays.tag_names.strings.size());
    printf("max number of tag-values: %d\n", (int)serializeWays.tag_values.strings.size());
    printf("max id: %lu ... min id: %lu\n", serializeWays.max_node_id, serializeWays.min_node_id);

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

        StringTable d_tag_names {};
        StringTable d_tag_values {};

        d_tag_names.DeSerialize(d);
        d_tag_values.DeSerialize(d);

        printf("deserilzed tag-keys: %d\n", (int)d_tag_names.strings.size());
        printf("deserilzed tag-values: %d\n", (int)d_tag_values.strings.size());
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
