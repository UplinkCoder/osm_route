#!/bin/sh
g++ example_routing.cc -std=c++17 -Wall -pedantic -lz -lprotobuf-lite -losmpbf -oapp -Os -g3 -march=native -mtune=native
