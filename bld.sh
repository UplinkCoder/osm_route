#!/bin/sh
g++ example_routing.cc -std=c++17 -Wall -pedantic  -O0 -march=native -mtune=native -g3 -c
g++ example_routing.o -oapp -losmpbf -lz -lprotobuf-lite
