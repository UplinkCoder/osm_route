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
qSpan<string_view> street_name_trie[27] {};
qSpan<pair<string_view, uint32_t> > street_names;

static const auto SPECIAL_CHAR_IDX = 26;

#define LOWERCASE(C) \
        (((C) >= 'A' && (C) <= 'Z') ? ((C) + ('a' - 'A')) : (C))


void complete (const char * line, linenoiseCompletions * completions)
{
    const auto len = strlen(line);

    if (len)
    {
        auto c = LOWERCASE(line[0]);
        if (c >= 'a' &&  c <= 'z')
        {
            const auto c_idx = c - 'a';
            uint n_completions = 0;
            for(uint32_t idx = 0;
                idx < street_name_trie[c_idx].size();
                idx++)
            {
                const auto & str = street_name_trie[c_idx][idx];
                if (str.size() < len)
                    continue;
                if (0 == strncasecmp(str.data(), line, len))
                {
                    linenoiseAddCompletion(completions, str.data());
                    if (n_completions++ == 30)
                        break;
                }
            }
        }
    }
}


MAIN
{
    if (argc != 2)
    {
        fprintf(stderr, "exactly one argument expected");
        abort();
    }

    Serializer dser (argv[1], Serializer::serialize_mode_t::Reading);

    DeSerializeWays ws = {};
    Pool pool = {};

    ws.DeSerialize(dser, &pool);

    const uint32_t n_street_names =
        ws.street_name_indicies.size();

    printf("found %u street names\n", n_street_names);
    {
        street_names = decltype(street_names) {n_street_names, &pool};
    }
    {
        uint32_t idx = 0;
        for(const auto &s_idx : ws.street_name_indicies)
        {
            const auto & str = ws.tag_values[s_idx];
            street_names[idx++] = (make_pair(str, s_idx));
        }
    }

    printf("total_allocated: %u\n", pool.total_allocated);
    printf("wasted: %u\n", pool.wasted_bytes);

    {
        // count frequency of starting letters so we know our qSpans
        uint counts[27] = {};
        for(auto &str : street_names)
        {
            if (!str.first.size())
                continue;
            const char c = LOWERCASE(str.first[0]);
            if (c >= 'a' && c <= 'z')
            {
                counts[c - 'a']++;
            }
            else
            {
                counts[SPECIAL_CHAR_IDX]++;
            }
        }
        // preallocate our spans
        {
            uint32_t idx = 0;
            for(auto &c : counts)
            {
                street_name_trie[idx++].AllocFromPool(c, &pool);
            }
        }
        // now fill it
        for(auto &str : street_names)
        {
            if (!str.first.size())
                continue;
            const char c = LOWERCASE(str.first[0]);
            if (c >= 'a' && c <= 'z')
            {
                const uint8_t c_idx = c - 'a';
                street_name_trie[c_idx][--counts[c_idx]] = str.first;
            }
            else
            {
                const uint8_t c_idx = SPECIAL_CHAR_IDX;
                street_name_trie[c_idx][--counts[c_idx]] = str.first;
            }
        }
    }

    {
        char* input;
        linenoiseSetCompletionCallback(&complete);
        while( (input = linenoise("> ")) != NULL )
        {
            if (0 == strcmp(input, "q"))
                return 0;
            linenoiseFree(input); /* Or free(line) for libc malloc. */
        }
    }

    return 0;
}


/*
typedef struct linenoiseCompletions {
  size_t len;
  char **cvec;
} linenoiseCompletions;
*/
typedef void(linenoiseCompletionCallback)(const char *, linenoiseCompletions *);
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *);
void linenoiseAddCompletion(linenoiseCompletions *, const char *);
char *linenoise(const char *prompt);
void linenoiseFree(void *ptr);
