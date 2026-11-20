#include "gpu_time.h"

#include "core/object/callable_mp.h"
#include "main/performance.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/rendering/rendering_server.h"

namespace GpuTime {

double frame_ms(Viewport *p_viewport) {
	if (p_viewport == nullptr) {
		return 0.0;
	}
	RenderingServer::get_singleton()->viewport_set_measure_render_time(p_viewport->get_viewport_rid(), true);
	return RenderingServer::get_singleton()->viewport_get_measured_render_time_gpu(p_viewport->get_viewport_rid());
}

double main_viewport_ms() {
	SceneTree *tree = SceneTree::get_singleton();
	return frame_ms(tree != nullptr ? tree->get_root() : nullptr);
}

void add_monitor() {
	Performance *performance = Performance::get_singleton();
	const StringName name = "graphics/gpu_ms";
	if (performance != nullptr && !performance->has_custom_monitor(name)) {
		performance->add_custom_monitor(name, callable_mp_static(&main_viewport_ms), Vector<Variant>(), Performance::MONITOR_TYPE_TIME);
	}
}

} // namespace GpuTime
