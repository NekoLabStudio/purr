#!/bin/sh
# Builds and runs the tests of the rules for lobbies without the online services (plain C++, no
# Godot needed).
set -eu
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/purr_lobby_rules_tests"
c++ -std=c++17 -O1 -Wall -I. -o "$out" tests/lobby_rules_test.cpp lobby_rules.cpp
"$out"
