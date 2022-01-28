#!/bin/sh
g++ example_routing.cc -std=c++17 -Wall -pedantic  -oapp -O3 -march=native -mtune=native -g0 \
    -losmpbf -lz -lprotobuf-lite
