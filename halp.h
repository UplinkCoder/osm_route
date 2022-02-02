#include <stdio.h>
#define MAIN int main (int argc, char* argv[])
#define RANGE(FROM, TO) for (auto it = (FROM); it < (TO); it++)  
#define FOR(E, RANGE_) \
    for(auto& E = (RANGE_).begin(), const auto& __end = (RANGE).end(); \
        E != __end(); \
        E++)
