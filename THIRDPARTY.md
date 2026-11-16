# Third-party components

Purr is Godot Engine plus a few patches, so almost everything in it comes from Godot.

## Godot Engine

MIT license, see [LICENSE.txt](LICENSE.txt). Godot bundles many third-party libraries in `godot/thirdparty/`. Each one, with its copyright and license, is listed in [godot/COPYRIGHT.txt](godot/COPYRIGHT.txt), and the full license texts are in the editor under Help > About > Third-party Licenses.

## Downloaded at build time

Godot's own scripts download these when Purr is built. They aren't stored in this repository, but they end up in the editor and export templates.

| Component | Platform | How it's included | License |
|---|---|---|---|
| [MoltenVK](https://github.com/KhronosGroup/MoltenVK), from the Vulkan SDK | macOS | linked into the binaries | Apache 2.0 |
| [AccessKit](https://github.com/AccessKit/accesskit-c) | macOS, Windows | linked into the binaries | MIT or Apache 2.0 |
| [Mesa NIR](https://github.com/godotengine/godot-nir-static) | Windows | linked into the binaries | MIT |
| [Direct3D 12 Agility SDK](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12) | Windows | `D3D12Core.dll`, shipped next to the editor and templates | Microsoft DirectX SDK license (redistributable) |

## Engine modules

| Component | Where | License |
|---|---|---|
| [GodotSteam](https://godotsteam.com) | `modules/godotsteam/` | MIT, see [its license](modules/godotsteam/license.md) |
| [webrtc-native](https://github.com/godotengine/webrtc-native) (the engine code in `modules/webrtc_native/` is based on it) | `modules/webrtc_native/` | MIT |
| [libdatachannel](https://github.com/paullouisageneau/libdatachannel) | `modules/webrtc_native/thirdparty/libdatachannel/` | MPL 2.0, see [its license](modules/webrtc_native/thirdparty/libdatachannel/LICENSE) |
| [libjuice](https://github.com/paullouisageneau/libjuice) | `modules/webrtc_native/thirdparty/libjuice/` | MPL 2.0, see [its license](modules/webrtc_native/thirdparty/libjuice/LICENSE) |
| [usrsctp](https://github.com/sctplab/usrsctp) | `modules/webrtc_native/thirdparty/usrsctp/` | BSD 3-Clause, see [its license](modules/webrtc_native/thirdparty/usrsctp/LICENSE.md) |
| [plog](https://github.com/SergiusTheBest/plog) | `modules/webrtc_native/thirdparty/plog/` | MIT, see [its license](modules/webrtc_native/thirdparty/plog/LICENSE) |

libdatachannel and libjuice are under the Mozilla Public License 2.0, which covers those files only. Purr changes libdatachannel with one patch, [patches/0014-libdatachannel-build-with-Mbed-TLS-4.patch](patches/0014-libdatachannel-build-with-Mbed-TLS-4.patch), so their source (with Purr's changes) is available here as the license asks. Games that ship Purr's export templates should credit them and say where their source is available.

## The VR streamer

`modules/vr_streamer/` builds Purr's OpenXR runtime for macOS (`libvr_runtime.dylib`; Purr's own code) and the streamer it runs (`vr_streamer`, Purr's own code, written in Rust). macOS exports ship both when the export preset's Include VR Streamer option is on (it is by default in projects with OpenXR on).

The streamer speaks Valve's Steam Link VR protocol, as described in [OpenVRLink](https://github.com/shinyquagsire23/OpenVRLink)'s documentation; no code from it is included.

The streamer compiles in about 55 crates from crates.io, downloaded at build time (their versions are pinned in `modules/vr_streamer/streamer/Cargo.lock`). All are under permissive licenses: mostly MIT or Apache 2.0, with some BSD, Zlib and Unlicense. Releases put all of their notices in `vr_streamer_notices.txt` in the app's `Resources`, made by `support/streamer-notices.py`; exports carry it with the streamer.

## The online services

`online/` runs on Cloudflare Workers. It bundles these libraries, for reading Apple's Game Center certificates:

| Component | License |
|---|---|
| [@peculiar/asn1-schema](https://github.com/PeculiarVentures/asn1-schema), [@peculiar/asn1-x509](https://github.com/PeculiarVentures/asn1-schema) and [@peculiar/utils](https://github.com/PeculiarVentures/utils) | MIT |
| [asn1js](https://github.com/PeculiarVentures/asn1.js) | BSD 3-Clause |

## Steamworks

The Steam module is built with Valve's Steamworks SDK, under Valve's [Steamworks SDK Access Agreement](https://partner.steamgames.com/documentation/sdk_access_agreement). The SDK isn't in this repository. Only its redistributable library (`libsteam_api.dylib`, `steam_api64.dll`, `libsteam_api.so`) ships with Purr's editor and export templates, and with every desktop game exported with Purr, as the agreement allows.

## Credits in games

Games made with Purr should credit "Engine: Purr (based on Godot Engine)" and include Godot's license text and the third-party notices above. Godot's [Complying with licenses](https://docs.godotengine.org/en/latest/about/complying_with_licenses.html) page explains what that involves.
