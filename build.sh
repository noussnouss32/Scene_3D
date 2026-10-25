#!/usr/bin/env bash
# Compile le projet (Linux / macOS)
set -e
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
echo
echo "OK -> lancez :  ./build/enchanted_forest"
