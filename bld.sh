#!/bin/sh
g++ example_routing.cc -std=c++17 -Wall -pedantic -lz -lprotobuf-lite -losmpbf -oapp -O2 -g -march=core2 -mtune=core2
