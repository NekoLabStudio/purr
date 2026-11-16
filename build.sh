#!/bin/sh
# Builds Purr for macOS (Apple Silicon) from this checkout.
#
#   ./build.sh            the editor
#   ./build.sh templates  the debug and release export templates
#   ./build.sh all        both
#   ./build.sh tvos       the Apple TV export templates (devices),
#                         bundled as godot/bin/godot_tvos.zip
#   ./build.sh visionos   the Apple Vision Pro export templates (devices), bundled as
#                         godot/bin/godot_visionos.zip
#   ./build.sh android    the Android export templates for arm64 devices, and Play Games' plugin
#                         (godot/bin/purr_play_games.aar); needs ANDROID_HOME and a JDK 17 in
#                         JAVA_HOME. Releases build every architecture.
#
# Binaries end up in godot/bin/.

set -eu
cd "$(dirname "$0")"
root="$(pwd)"

what="${1:-editor}"
case "$what" in
editor | templates | all | tvos | visionos | android) ;;
*)
	echo "Usage: ./build.sh [editor|templates|all|tvos|visionos|android]" >&2
	exit 1
	;;
esac

if ! command -v scons >/dev/null 2>&1; then
	echo "SCons is missing. Install it with: brew install pipx && pipx install scons && pipx inject scons zstandard" >&2
	exit 1
fi

# Purr's patches and branding go on top of the plain Godot source for the build, and
# come off again afterwards so godot/ never looks edited. On a work branch, where the
# patches are already commits, they're left alone.
# They're undone one at a time, newest first: patches can build on each other's changes to a
# file, which git apply can't undo in a single call.
newest=$(ls patches/*.patch 2>/dev/null | tail -n 1)
if [ -n "$newest" ] && ! git apply --reverse --check "$newest" 2>/dev/null; then
	git apply patches/*.patch
	trap 'cd "$root" && for p in $(ls -r patches/*.patch); do git apply --reverse "$p"; done && git checkout -q -- godot/main/splash.png godot/editor/icons' EXIT
else
	trap 'cd "$root" && git checkout -q -- godot/main/splash.png godot/editor/icons' EXIT
fi
trap 'exit 1' INT TERM
cp branding/splash.png godot/main/splash.png
cp branding/editor/*.svg godot/editor/icons/

# Godot reads its commit hash from godot/.git/HEAD, which can't be stored in this repo.
# Its build then asks git for that commit's date, which this repo doesn't have: the "fatal: bad
# object" line that follows is expected, and the build goes on without the date.
mkdir -p godot/.git
sed -n '/^\[godot\]/,/^$/s/^commit: //p' UPSTREAM.txt >godot/.git/HEAD

BUILD_NAME="$(cat VERSION.txt)"
GODOT_VERSION_STATUS="$(sed -n 's/^status: //p' UPSTREAM.txt)"
export BUILD_NAME GODOT_VERSION_STATUS

cd godot

# Vulkan (through MoltenVK) beside Metal: from the Vulkan SDK in ~/VulkanSDK, or VULKAN_SDK_PATH
# naming an SDK, or just a MoltenVK release's MoltenVK.xcframework.
vulkan=yes
vulkan_path=""
if [ -n "${VULKAN_SDK_PATH:-}" ]; then
	vulkan_path="vulkan_sdk_path=$VULKAN_SDK_PATH"
elif ! ls -d "$HOME"/VulkanSDK/*/ >/dev/null 2>&1; then
	vulkan=no
	echo "No Vulkan SDK in ~/VulkanSDK, so this build has Metal only."
	echo "To add Vulkan, run: sh godot/misc/scripts/install_vulkan_sdk_macos.sh"
fi

if [ ! -d bin/build_deps/accesskit ]; then
	python3 misc/scripts/install_accesskit.py >/dev/null
fi

# Valve's Steamworks SDK can't be stored in this repository, so it's copied into the
# Steam module from sdk/steamworks/ (see README). Without it, the module is left out.
steam=yes
if [ -d ../sdk/steamworks/public ]; then
	cp -R ../sdk/steamworks/public ../sdk/steamworks/redistributable_bin ../modules/godotsteam/sdk/
	# The binaries load Steam's library from their own folder.
	mkdir -p bin
	cp ../sdk/steamworks/redistributable_bin/osx/libsteam_api.dylib bin/
else
	steam=no
	echo "No Steamworks SDK in sdk/steamworks, so this build has no Steam module."
fi

# SCONS_CACHE, if set, is a folder SCons keeps compiled files in between builds (CI caches it).
cache=""
if [ -n "${SCONS_CACHE:-}" ]; then
	cache="cache_path=$SCONS_CACHE cache_limit=6"
fi

flags="platform=macos arch=arm64 profile=../build/purr.py custom_modules=../modules module_godotsteam_enabled=$steam vulkan=$vulkan $vulkan_path $cache -j$(sysctl -n hw.ncpu)"

if [ "$what" = editor ] || [ "$what" = all ]; then
	scons $flags target=editor
fi
if [ "$what" = templates ] || [ "$what" = all ]; then
	scons $flags target=template_debug
	scons $flags target=template_release production=yes lto=full
fi
# The VR streamer the editor and Mac games start beside Purr's OpenXR runtime (bin/libvr_runtime.dylib,
# built by SCons). It needs Rust (rustup.rs); without it, the build leaves the streamer out.
if [ "$what" = editor ] || [ "$what" = templates ] || [ "$what" = all ]; then
	if command -v cargo >/dev/null 2>&1; then
		cargo build --quiet --release --manifest-path ../modules/vr_streamer/streamer/Cargo.toml
		cp ../modules/vr_streamer/streamer/target/release/vr_streamer bin/
	else
		echo "No Rust toolchain, so this build has no VR streamer. Install it from https://rustup.rs."
	fi
fi
if [ "$what" = tvos ]; then
	tvos="platform=tvos arch=arm64 profile=../build/purr.py custom_modules=../modules $cache -j$(sysctl -n hw.ncpu)"
	scons $tvos target=template_debug
	scons $tvos target=template_release production=yes lto=full
	# The same flags again, now bundling the four into the Xcode project the exporter uses.
	scons $tvos target=template_release production=yes lto=full generate_bundle=yes
fi
if [ "$what" = visionos ]; then
	visionos="platform=visionos arch=arm64 profile=../build/purr.py custom_modules=../modules $cache -j$(sysctl -n hw.ncpu)"
	scons $visionos target=template_debug
	scons $visionos target=template_release production=yes lto=full
	scons $visionos target=template_release production=yes lto=full generate_bundle=yes
fi
if [ "$what" = android ]; then
	# Frame pacing, which release builds need (the libraries stay out of git: *.a in .gitignore).
	if [ ! -f thirdparty/swappy-frame-pacing/arm64-v8a/libswappy_static.a ]; then
		python3 misc/scripts/install_swappy_android.py >/dev/null
	fi
	android="platform=android arch=arm64 profile=../build/purr.py custom_modules=../modules module_godotsteam_enabled=no $cache -j$(sysctl -n hw.ncpu)"
	scons $android target=template_debug
	scons $android target=template_release production=yes
	(cd platform/android/java && ./gradlew -q generateGodotTemplates)
	platform/android/java/gradlew -q -p ../modules/play_games/android copyAar
	cp ../modules/play_games/android/build/purr/purr_play_games.aar bin/
fi
