// The game's immersive space on an Apple Vision Pro: macOS renders it and the Vision Pro shows
// it (RemoteImmersiveSpace, macOS 26). SwiftUI owns the scene; everything else is the
// presenter's (vision_pro.mm), which this hands the compositor layer to.

import ARKit
import CompositorServices
import SwiftUI

@available(macOS 26.0, *)
private struct Layout: CompositorLayerConfiguration {
    func makeConfiguration(capabilities: LayerRenderer.Capabilities, configuration: inout LayerRenderer.Configuration) {
        // One texture per eye, in the game's own colour encoding, drawn evenly (no foveation).
        configuration.layout = .dedicated
        configuration.colorFormat = .bgra8Unorm_srgb
        configuration.depthFormat = .depth32Float
        configuration.isFoveationEnabled = false
    }
}

@available(macOS 26.0, *)
private struct GameSpace: CompositorContent {
    @Environment(\.remoteDeviceIdentifier) private var device

    var body: some CompositorContent {
        CompositorLayer(configuration: Layout()) { layerRenderer in
            let layer = Unmanaged.passRetained(layerRenderer).toOpaque()
            let device = device.map { Unmanaged.passRetained($0.cDevice as AnyObject).toOpaque() }
            purr_vision_pro_layer_ready(layer, device)
        }
        .onWorldRecenter {
            purr_vision_pro_recentred()
        }
    }
}

@available(macOS 26.0, *)
@MainActor
private enum Space {
    static let id = "game"
    static var scene: NSHostingSceneRepresentation<RemoteImmersiveSpace<CompositorContentBuilder.Content<GameSpace>, Never>>?

    static func open() {
        let scene = Space.scene ?? {
            let made = NSHostingSceneRepresentation {
                RemoteImmersiveSpace(id: Space.id) {
                    GameSpace()
                }
            }
            NSApplication.shared.addSceneRepresentation(made)
            Space.scene = made
            return made
        }()
        guard scene.environment.supportsRemoteScenes else {
            purr_vision_pro_failed("this Mac can't show scenes on a Vision Pro")
            return
        }
        purr_vision_pro_log("Vision Pro: asking which one to use")
        Task { @MainActor in
            // macOS asks which Vision Pro to use; the player accepts on the headset.
            switch await scene.environment.openImmersiveSpace(id: Space.id) {
            case .opened:
                purr_vision_pro_log("Vision Pro: the space is open")
            case .userCancelled:
                purr_vision_pro_failed("the player cancelled")
            default:
                purr_vision_pro_failed("the space couldn't open")
            }
        }
    }

    static func close() {
        guard let scene = Space.scene else { return }
        Task { @MainActor in
            await scene.environment.dismissImmersiveSpace()
        }
    }
}

/// Opens the game's space on a Vision Pro. Call on the main thread.
@_cdecl("purr_vision_pro_open")
public func purrVisionProOpen() {
    if #available(macOS 26.0, *) {
        MainActor.assumeIsolated { Space.open() }
    } else {
        purr_vision_pro_failed("it needs macOS 26 or later")
    }
}

/// Closes it. Call on the main thread.
@_cdecl("purr_vision_pro_close")
public func purrVisionProClose() {
    if #available(macOS 26.0, *) {
        MainActor.assumeIsolated { Space.close() }
    }
}
