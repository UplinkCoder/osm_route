#include <unordered_map>
#include <utility>
#include "string_table.cpp"
#include "ways.h"

#undef MAYBE_UNUSED

#define MAYBE_UNUSED(expr) \
    do { (void)(expr); } while (0)

struct DeSerializeWays
{
    // the following fields get serialized.
    StringTable tag_names {
#        include "prime_names.h"
    };

    StringTable tag_values {};
    qSpan<uint32_t> street_name_indicies {};
    qSpan<Node> nodes;
    qSpan<Way> ways;

    void ReadTags(Serializer& serializer, short_tags_t* tags)
    {
        uint32_t n_tags;
        serializer.ReadShortUint(&n_tags);
        tags->resize(n_tags);
        for(uint32_t itag = 0; itag < n_tags; itag++)
        {
            uint32_t name_index, value_index;
            serializer.ReadShortUint(&name_index);
            serializer.ReadShortUint(&value_index);
            (*tags)[itag] = {name_index, value_index};
        }
    }

    void DeSerialize (Serializer& serializer)
    {
        const auto tag_names_off = serializer.ReadU32(); // beginning tag names
        MAYBE_UNUSED(tag_names_off);
        const auto tag_values_off = serializer.ReadU32(); // beginning tag values
        MAYBE_UNUSED(tag_values_off);
        const auto street_names_off = serializer.ReadU32(); // beginning street_names
        MAYBE_UNUSED(street_names_off);
        const auto nodes_off = serializer.ReadU32(); // beginning nodes
        MAYBE_UNUSED(nodes_off);
        const auto ways_off = serializer.ReadU32(); // beginning ways
        MAYBE_UNUSED(ways_off);

        {
            clock_t deserialize_tags_begin = clock();
            {
                tag_names.DeSerialize(serializer);
                tag_values.DeSerialize(serializer);
            }
            clock_t deserialize_tags_end = clock();
            printf("deserialisation of tags took %f milliseconds\n",
                ((deserialize_tags_end - deserialize_tags_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);
        }


        {
            clock_t deserialize_street_names_begin = clock();
            {
                uint32_t n_street_names = serializer.ReadU32();
                street_name_indicies.resize(n_street_names);

                for (uint32_t i = 0;
                    i < n_street_names;
                    i++)
                {
                    serializer.ReadShortUint(&street_name_indicies[i]);
                    // printf("street_name: %s\n", tag_values[value].data());
                }
                // printf("Read %d street_name_indicies\n", street_name_indicies.size());
             }
            clock_t deserialize_street_names_end = clock();
            printf("deserialisation of street names took %f milliseconds\n",
                ((deserialize_street_names_end - deserialize_street_names_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);
        }

        {
            assert(serializer.CurrentPosition() == nodes_off);

            const auto n_nodes = serializer.ReadU32();
            nodes.resize(n_nodes);

            clock_t deserialize_nodes_begin = clock();
            {
                const auto n_baseNodes = serializer.ReadU32();
                printf("n_nodes: %d .. n_baseNodes: %d\n", n_nodes, n_baseNodes);
                {
                    uint32_t idx = 0;

                    for(uint32_t i = 0;
                        i < n_baseNodes;
                        i++)
                    {
                        const auto base_id = serializer.ReadU64();
                        auto& n = nodes[idx++];

                        n.osmid = base_id;
                        // writing out the number of relative nod
                        n.lat_m = serializer.ReadF64();
                        n.lon_m = serializer.ReadF64();
                        ReadTags(serializer, &n.tags);
                        // number of children
                        uint32_t n_children = serializer.ReadU8();

                        for(uint32_t i = 0;
                            i < n_children;
                            i++)
                        {
                            auto & child = nodes[idx++];

                            child.osmid = base_id + serializer.ReadU8();
                            child.lat_m = serializer.ReadF64();
                            child.lon_m = serializer.ReadF64();
                            ReadTags(serializer, &child.tags);
                        }
                    }
                }
            }
            clock_t deserialize_nodes_end = clock();

            printf("deserialisation of nodes took %f milliseconds\n",
                ((deserialize_nodes_end - deserialize_nodes_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);
        }
        assert(ways_off == serializer.CurrentPosition());

        const auto n_ways = serializer.ReadU32();
        ways.resize(n_ways);

        clock_t deserialize_ways_begin = clock();
        {
            uint64_t base_way_osmid = 0;

            for(auto& w : ways)
            {
                int32_t osmid_delta;
                serializer.ReadShortInt(&osmid_delta);

                w.osmid = base_way_osmid + osmid_delta;
                base_way_osmid = w.osmid;
            }

            for(uint32_t i = 0;
                i < ways.size();
                i++)
            {
                auto &w = ways[i];
                ReadTags(serializer, &w.tags);

                uint32_t n_refs;
                serializer.ReadShortUint(&n_refs);

                if (n_refs)
                {
                    w.refs.resize(n_refs);
                    const auto base_ref = serializer.ReadU64();
                    w.refs[0] = base_ref;

                    for(uint32_t i = 1;
                        i < n_refs;
                        i++)
                    {
                        int32_t delta;

                        auto bytes_read =
                            serializer.ReadShortInt(&delta);
                        MAYBE_UNUSED(bytes_read);
                        w.refs[i] = base_ref + delta;
                        if (delta == 0)
                        {
                            assert(bytes_read == 1);
                            w.refs[i] = serializer.ReadU64();
                        }
                    }
                }
            }
        }
        clock_t deserialize_ways_end = clock();
        printf("deserialisation of ways took %f milliseconds\n",
            ((deserialize_ways_end - deserialize_ways_begin) / (double)CLOCKS_PER_SEC) * 1000.0f);

    }
} ;

#undef MAYBE_UNUSED
