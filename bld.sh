#!/bin/sh
#g++ example_routing.cc -std=c++17 -Wall -pedantic  -Os -march=native -mtune=native -g3 -c -DNDEBUG
#g++ example_routing.o -oapp -losmpbf -lz -lprotobuf-lite

g++ list_streets.cpp -std=c++17 -Wall -pedantic -ffast-math -Ofast -march=native -mtune=native -g1 -c -DNDEBUG #-DNO_CRC32
g++ list_streets.o -olist_streets
