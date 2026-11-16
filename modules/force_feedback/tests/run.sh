#!/bin/sh
# Builds and runs the force feedback tests (plain C++, no Godot or wheel needed; macOS), and builds
# everything but the Godot wrappers as another program would, without exceptions or RTTI.
set -eu
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/force_feedback_tests"
c++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -I. -o "$out" \
	-framework IOKit -framework CoreFoundation \
	tests/*.cpp \
	wheel_log.cpp vibration.cpp wheel_player.cpp wheel_session.cpp known_wheels.cpp \
	drivers/*.cpp drivers/*/*.cpp macos/*.cpp
"$out"
