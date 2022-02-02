#include <stdint.h>
#include <vector>
#include <utility>
#include <unordered_map>
#include "ways.h"
#include "deserialize.cpp"
#include "3rd_party/linenoise/linenoise.h"
#include "3rd_party/linenoise/linenoise.c"

#if (__cplusplus <= 201500)
#    include "llvm_string_view.hpp"
     using string_view = StringView;
#else
#  include <string_view>
#endif

#include <iostream>
#include "halp.h"

using namespace std;

// lets to a crappy trie
vector<string_view> street_name_trie[27] {};
qSpan<pair<string_view, uint32_t>> street_names;

static const auto SPECIAL_CHAR_IDX = 26;

MAIN
{
    if (argc != 2)
    {
        fprintf(stderr, "exactly one argument expected");
        abort();
    }
    
    Serializer dser (argv[1], Serializer::serialize_mode_t::Reading);
    
    DeSerializeWays ws;
    
    ws.DeSerialize(dser);
    
    const uint32_t n_street_names = 
        ws.street_name_indicies.size();
    
    printf("found %u street names\n", n_street_names);
    {
        street_names.resize(n_street_names);
    }
    {
        uint32_t idx = 0;
        for(const auto &s_idx : ws.street_name_indicies)
        {
            const auto & str = ws.tag_values[s_idx];
            street_names[idx++] = (make_pair(str, s_idx));
        }
        
        
    }
    
    return 0;
}
