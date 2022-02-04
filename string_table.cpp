#include <vector>
#include <map>
#include <utility>
#include "stdlib.h"
#include "crc32.c"
#include "serializer.cpp"

#if (__cplusplus <= 201500)
#    include "llvm_string_view.hpp"
     using string_view = StringView;
#else
#  include <string_view>
#endif

#define SORT_VEC(VEC) \
    VEC.data(), VEC.size(), sizeof(decltype(VEC)::value_type)

using namespace std;

struct StringEntry
{
    uint32_t crc32;
    uint32_t length;
    uint32_t offset;
};

struct StringTable
{
    StringTable() = default;
    StringTable(vector<const char*> primer);
    // private:

    std::vector<char> string_data;
    std::vector<StringEntry> strings;
    map<uint32_t, uint32_t> crc32_to_indecies;
    std::vector<std::pair<uint32_t, uint32_t> > usage_counts;
    // public :

    ~StringTable() = default;

    uint32_t AddString(const string_view & str);

    /// Returns 0 if not found or the index of the string_entry + 1 if found
    uint32_t LookupCString(const char* str);

    /// Returns 0 if not found or the index of the string_entry + 1 if found
    uint32_t LookupString(const string_view& str);

    /// Returns 0 if not found or the index fo the string_entry + 1 if found
    uint32_t LookupString (const char* str_data, uint32_t str_size, uint32_t input_crc);

    string_view LookupId(uint32_t idx);

    string_view operator[] (uint32_t idx);

    /// Returns 0 if not found or the index of the string_entry + 1 if found
    uint32_t operator[] (const string_view& str);

    void Serialize (Serializer& serializer);

    void DeSerialize (Serializer& serializer);

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
};


StringTable::StringTable (vector<const char*> primer) : string_data(), strings(), crc32_to_indecies() {
    for(auto &e : primer)
    {
        AddString(string_view {e, strlen(e)} );
    }
}

uint32_t StringTable::AddString (const string_view & str) {
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


uint32_t StringTable::LookupCString (const char* str) {
    return LookupString(string_view {str, strlen(str)});
}

uint32_t StringTable::LookupString (const string_view& str) {
    const char* str_data = str.data();
    const auto str_size = str.size();

    const uint32_t crc_input =
        FINALIZE_CRC32C(crc32c(INITIAL_CRC32C, str_data, str_size));

    return LookupString(str_data, str_size, crc_input);
}

uint32_t StringTable::LookupString (const char* str_data, uint32_t str_size, uint32_t crc_input) {
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

string_view StringTable::LookupId (uint32_t idx) {
    if (!idx || idx > strings.size())
    {
        return {(const char*)0, (size_t)0};
    }
    else
    {
        const auto entry = strings[idx - 1];
        return string_view {&string_data[entry.offset], entry.length};
    }
}

string_view StringTable::operator[] (uint32_t idx) {
    return LookupId(idx);
}

uint32_t StringTable::operator[] (const string_view& str) {
    return LookupString(str);
}

void StringTable::Serialize (Serializer& serializer) {
    // serialize string data.
    // serializer.BeginField("vector<char>", "string_data");
    {
        const uint32_t n_chars = string_data.size();

        serializer.WriteU32(n_chars);
        auto ptr = (const char*)string_data.data();
        WRITE_ARRAY_DATA_SIZE(serializer, ptr, n_chars);
    }

    // serializer.EndField();

    // we only need to store the number of strings as we can
    // reproduce the string entires by reading in the null
    // terminated strings in order.
    serializer.WriteU32(strings.size());
}

void StringTable::DeSerialize (Serializer& serializer) {
    // serialize string data.

    // serializer.BeginField("vector<char>", "string_data");
    {
        uint32_t n_chars = serializer.ReadU32();
        string_data.resize(n_chars);
        auto ptr = string_data.data();

        READ_ARRAY_DATA_SIZE(serializer, ptr, n_chars);
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

#undef SORT_VEC
