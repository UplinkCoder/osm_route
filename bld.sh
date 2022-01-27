#!/bin/sh
g++ example_routing.cc -std=c++17 -Wall -pedantic  -oapp -O0 -march=native -mtune=native -g3 \
    -losmpbf -lz -lprotobuf-lite
