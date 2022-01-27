#!/bin/sh
g++ example_routing.cc -std=c++17 -Wall -pedantic  -oapp -O2 -march=native -mtune=native  \
    -losmpbf -lz -lprotobuf-lite
