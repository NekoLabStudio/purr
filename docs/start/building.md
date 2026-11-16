# Building from source

Purr builds on a Mac with Xcode. You also need SCons with its `zstandard` module:

```sh
brew install pipx
pipx install scons
pipx inject scons zstandard
```

Then, from the repository:

```sh
./build.sh            # the editor
./build.sh templates  # the macOS export templates
./build.sh all        # both
./build.sh tvos       # the Apple TV export templates
```

The binaries end up in `godot/bin/`. `build.sh` applies Purr's patches to the Godot source for the build and removes them afterwards, so `godot/` always matches the Godot release.

The editor uses Metal. To build the Vulkan renderer as well, install the Vulkan SDK first with `sh godot/misc/scripts/install_vulkan_sdk_macos.sh`.

## Steam

Steam support needs Valve's Steamworks SDK, which can't be stored in the repository. Download it from the [Steamworks partner site](https://partner.steamgames.com/downloads/list) (`UPSTREAM.txt` says which version) and unzip it so that `sdk/steamworks/public/` exists. Without it, the build leaves Steam out.

## Windows

The release workflow builds Windows on GitHub's Windows runners with Visual Studio. To build for Windows on a Mac, install mingw-w64 (`brew install mingw-w64`) and run SCons from `godot/` with `platform=windows use_mingw=yes`, after applying the patches the same way `build.sh` does.
