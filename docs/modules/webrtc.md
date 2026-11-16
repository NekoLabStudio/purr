# WebRTC

Godot's `WebRTCPeerConnection` and `WebRTCMultiplayerPeer` work in Purr on every platform without an addon. Godot needs the webrtc-native GDExtension for them; Purr builds the library it uses, [libdatachannel](https://github.com/paullouisageneau/libdatachannel), into the engine.

[Crossplay lobbies](../api/lobbies.md) use it, and you can use it directly as in Godot's [WebRTC tutorial](https://docs.godotengine.org/en/latest/tutorials/networking/webrtc.html).

Purr's WebRTC carries data channels, which is what games use. Audio and video tracks aren't included.

Besides the standard `iceServers`, `WebRTCPeerConnection.initialize()` takes `iceTransportPolicy` set to `"relay"`, to connect only through TURN, and a `libdatachannel` dictionary with `enableIceTcp`, `enableIceUdpMux`, `portRangeBegin`, `portRangeEnd` and `mtu`, as webrtc-native does.

A GDExtension that sets its own WebRTC implementation (with `WebRTCPeerConnection.set_default_extension()`) still replaces Purr's.
