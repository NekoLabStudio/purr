#pragma once

// What the Swift scene (vision_pro_scene.swift) and the presenter (vision_pro.mm) say to each
// other, in C. The objects pass as retained pointers: the receiver takes ownership.

#ifdef __cplusplus
extern "C" {
#endif

// Swift to the presenter: the space opened on a Vision Pro, with its compositor layer
// (cp_layer_renderer_t) and the device ARKit tracks it with (ar_device_t, or null).
void purr_vision_pro_layer_ready(void *layer_renderer, void *device);
// Swift to the presenter: the space couldn't open, and why.
void purr_vision_pro_failed(const char *reason);
// Swift to the runtime's log.
void purr_vision_pro_log(const char *message);
// Swift to the presenter: the player recentred the view.
void purr_vision_pro_recentred(void);

#ifdef __cplusplus
}
#endif
