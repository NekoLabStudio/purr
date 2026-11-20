#include "mcp_capture.h"

#include "core/config/engine.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "main/performance.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

namespace {
void join() {
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	McpCapture *capture = memnew(McpCapture);
	tree->get_root()->add_child(capture, false, Node::INTERNAL_MODE_BACK);
}

String argument(const String &p_name) {
	for (const String &arg : OS::get_singleton()->get_cmdline_user_args()) {
		if (arg.begins_with(p_name + "=")) {
			return arg.substr(p_name.length() + 1);
		}
	}
	return String();
}
} // namespace

McpCapture::McpCapture() {
	set_name("McpCapture");
	set_process_mode(PROCESS_MODE_ALWAYS);
	mode = argument("--purr-mcp-capture");
	out_path = argument("--purr-mcp-out");
	const String wait = argument("--purr-mcp-seconds");
	if (!wait.is_empty()) {
		seconds = MAX(wait.to_float(), 0.1);
	}
}

void McpCapture::start_if_asked() {
	if (!argument("--purr-mcp-capture").is_empty()) {
		callable_mp_static(&join).call_deferred();
	}
}

void McpCapture::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			RenderingServer::get_singleton()->viewport_set_measure_render_time(get_viewport()->get_viewport_rid(), true);
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			DisplayServer *display = DisplayServer::get_singleton();
			if (display != nullptr && !display->window_is_focused()) {
				display->window_move_to_foreground();
			}
			const uint64_t now = OS::get_singleton()->get_ticks_usec();
			// The first second loads and compiles: measured from after it.
			if (last_usec != 0 && elapsed >= 1.0) {
				frames_ms.push_back(double(now - last_usec) / 1000.0);
				gpu_sum += RenderingServer::get_singleton()->viewport_get_measured_render_time_gpu(get_viewport()->get_viewport_rid());
				gpu_frames++;
			}
			if (last_usec != 0) {
				elapsed += double(now - last_usec) / 1000000.0;
			}
			last_usec = now;
			if (elapsed >= seconds + (mode == "measure" ? 1.0 : 0.0)) {
				_finish();
			}
		} break;
	}
}

void McpCapture::_finish() {
	set_process_internal(false);
	if (mode == "screenshot") {
		const Ref<Image> image = get_viewport()->get_texture()->get_image();
		if (image.is_valid()) {
			image->save_png(out_path);
		}
	} else {
		LocalVector<double> sorted;
		for (double ms : frames_ms) {
			sorted.push_back(ms);
		}
		sorted.sort();
		double sum = 0.0;
		for (double ms : frames_ms) {
			sum += ms;
		}
		const int count = frames_ms.size();
		Dictionary result;
		result["frames"] = count;
		result["mean_ms"] = count > 0 ? sum / count : 0.0;
		result["median_ms"] = count > 0 ? sorted[count / 2] : 0.0;
		result["p95_ms"] = count > 0 ? sorted[MIN(count - 1, int(count * 0.95))] : 0.0;
		result["max_ms"] = count > 0 ? sorted[count - 1] : 0.0;
		result["gpu_ms"] = gpu_frames > 0 ? gpu_sum / gpu_frames : 0.0;
		Performance *performance = Performance::get_singleton();
		result["draw_calls"] = performance->get_monitor(Performance::RENDER_TOTAL_DRAW_CALLS_IN_FRAME);
		result["objects"] = performance->get_monitor(Performance::RENDER_TOTAL_OBJECTS_IN_FRAME);
		result["primitives"] = performance->get_monitor(Performance::RENDER_TOTAL_PRIMITIVES_IN_FRAME);
		result["video_memory_mb"] = performance->get_monitor(Performance::RENDER_VIDEO_MEM_USED) / 1048576.0;
		result["refresh_rate"] = DisplayServer::get_singleton()->screen_get_refresh_rate();
		result["max_fps"] = Engine::get_singleton()->get_max_fps();
		Ref<FileAccess> file = FileAccess::open(out_path, FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_string(JSON::stringify(result));
		}
	}
	get_tree()->quit();
}
