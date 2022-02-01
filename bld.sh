#!/bin/sh
g++ example_routing.cc -std=c++17 -Wall -pedantic  -Os -march=native -mtune=native -g1 -c -DNDEBUG
g++ example_routing.o -oapp -losmpbf -lz -lprotobuf-lite

