#include <stdint.h>
#include <vector>
#include <utility>
#include <unordered_map>
#include "ways.h"
#include "deserialize.cpp"
#include <thread>
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

#undef MAYBE_UNUSED

#define MAYBE_UNUSED(expr) \
    do { (void)(expr); } while (0)

using namespace std;

static const string_view commands[] {
    ":tag_name",
    ":tag_value",
    ":is_street",
    ":dump_names",
    ":dump_values"
};

// lets do a crappy trie
qSpan<string_view> street_name_trie[27] {};
qSpan<pair<string_view, uint32_t> > street_names;

static const auto SPECIAL_CHAR_IDX = 26;

#define LOWERCASE(C) \
        (((C) >= 'A' && (C) <= 'Z') ? ((C) + ('a' - 'A')) : (C))

void complete (const char * line, linenoiseCompletions * completions)
{
    const auto len = strlen(line);
    if (line[0] == ':')
    {
        char completion_buffer[128];
        for(const auto& c : commands)
        {
            if (strcasecmp(line, c.data()))
            {
                int i = 0;
                for(const char *cc = c.data(); *cc; i++, cc++)
                {
                    completion_buffer[i] = *cc;
                }
                completion_buffer[c.size()] = ' ';
                completion_buffer[c.size() + 1] = '\0';
                if (len < c.size())
                {
                    // only add the command completion if we haven't seen the whole command
                    // sprintf(completion_buffer + i + 1, " length: %d", len);
                    linenoiseAddCompletion(completions, completion_buffer);
                }
                else
                {
                    
                }
            }
        }
    }

    else if (len)
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
                    if (n_completions++ == 100)
                        break;
                }
            }
        }
    }
}


void BuildStreetNameTrie(Pool* pool)
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
        //TODO alloc mutex.
        for(auto &c : counts)
        {
            street_name_trie[idx++].AllocFromPool(c, pool);
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

MAIN
{
    if (argc != 2)
    {
        fprintf(stderr, "exactly one argument expected");
        return 1;
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

    BuildStreetNameTrie(&pool);

    printf("total_allocated: %10u\n", pool.total_allocated);
    printf("wasted:          %10u\n", pool.wasted_bytes);

    {
        char* input;
        linenoiseSetCompletionCallback(&complete);
        while( (input = linenoise("> ")) != NULL )
        {
            const auto is_cmd = ((input[0] == ':') ? 1 : 0); 
            const auto input_length = strlen(input + is_cmd);
            const auto input_crc = 
                FINALIZE_CRC32C (crc32c(INITIAL_CRC32C, input + is_cmd, input_length));
 
            if (0 == strcmp(input + is_cmd, "q"))
                return 0;

            if (is_cmd)
            {
                #define CMD(S, ... ) \
                    if(0 == strncmp(#S, input + 1, sizeof(#S) - 1)) \
                    { \
                        const int32_t arg_len = input_length - sizeof(#S); \
                        const char* arg = ((arg_len > 0) ? input + 1 + sizeof(#S) : 0); \
                        const auto crc_arg = ((arg_len > 0) ? (~crc32c(INITIAL_CRC32C, arg, arg_len)) : 0) ; \
                        MAYBE_UNUSED(crc_arg); \
                        MAYBE_UNUSED(arg); \
                        __VA_ARGS__ \
                    }
                    
                // this is a command
                CMD(tag_name, {
                    const auto tag_idx = ws.tag_names.LookupString(arg, arg_len, crc_arg);
                    if (tag_idx)
                    {
                        printf("tag name index for '%s' is: '%u'\n", arg, tag_idx);
                    }
                    else
                    {
                        printf("No such tag name found\n");
                    }
                })

                CMD(tag_value, {
                    const auto tag_idx = ws.tag_values.LookupString(arg, arg_len, crc_arg);
                    if (tag_idx)
                    {
                        printf("tag value index for '%s' is: '%u'\n", arg, tag_idx);
                    }
                    else
                    {
                        printf("No such tag name found\n");
                    }
                })

                CMD(dump_names, {
                    for(auto& e : ws.tag_names.strings)
                    {
                        printf("%s\n", &ws.tag_names.string_data[e.offset]);
                    }
                })

                CMD(dump_values, {
                    for(auto& e : ws.tag_values.strings)
                    {
                        printf("%s\n", &ws.tag_values.string_data[e.offset]);
                    }
                })
            }
            #undef CMD
            else
            {
                printf("crc32c of '%s' is: %x\n", input, input_crc);
            }
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

#undef MAYBE_UNUSED

typedef void(linenoiseCompletionCallback)(const char *, linenoiseCompletions *);
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *);
void linenoiseAddCompletion(linenoiseCompletions *, const char *);
char *linenoise(const char *prompt);
void linenoiseFree(void *ptr);
