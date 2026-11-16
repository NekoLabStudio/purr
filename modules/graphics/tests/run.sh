#!/bin/sh
# Builds and runs the adapter tier tests (plain C++, no Godot needed).
set -eu
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/graphics_tests"
c++ -std=c++17 -O1 -I. -o "$out" tests/adapter_tier_test.cpp adapter_tier.cpp
"$out"
