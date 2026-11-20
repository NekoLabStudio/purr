#include "performance_overlay.h"

#include "modules/graphics/gpu_time.h"

#include "core/os/os.h"
#include "main/performance.h"
#include "scene/main/viewport.h"
#include "scene/resources/font.h"
#include "scene/theme/theme_db.h"
#include "servers/rendering/rendering_server.h"

namespace {
constexpr float WIDTH = 300.0;
constexpr float MARGIN = 8.0;
constexpr float GRAPH_HEIGHT = 48.0;
constexpr float GRAPH_MS = 33.3f; // The graph's top: two 60 Hz frames.

String ms(double p_value) {
	return String::num(p_value, p_value < 10.0 ? 2 : 1) + " ms";
}
} // namespace

PerformanceOverlay::~PerformanceOverlay() {
	stop(false);
}

// While the game runs, measuring stops with it; as the engine shuts down, the viewport may be gone.
void PerformanceOverlay::stop(bool p_running) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		return;
	}
	if (item.is_valid()) {
		rs->free_rid(item);
		item = RID();
	}
	if (measured_viewport.is_valid() && p_running) {
		rs->viewport_set_measure_render_time(measured_viewport, false);
	}
	measured_viewport = RID();
	if (profiling && p_running) {
		rs->set_frame_profiling_enabled(false);
	}
	profiling = false;
	frame_ms.clear();
	lines.clear();
	passes.clear();
	last_frame_usec = 0;
	window_start_usec = 0;
}

void PerformanceOverlay::update(Viewport *p_root, RID p_canvas) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!item.is_valid()) {
		item = rs->canvas_item_create();
		rs->canvas_item_set_parent(item, p_canvas);
	}
	const RID viewport = p_root->get_viewport_rid();
	if (viewport != measured_viewport) {
		if (measured_viewport.is_valid()) {
			rs->viewport_set_measure_render_time(measured_viewport, false);
		}
		rs->viewport_set_measure_render_time(viewport, true);
		measured_viewport = viewport;
	}
	if (!profiling) {
		rs->set_frame_profiling_enabled(true);
		profiling = true;
	}

	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	_sample(p_root, now);
	if (window_start_usec == 0) {
		window_start_usec = now;
	}
	if (now - window_start_usec >= AVERAGE_USEC || lines.is_empty()) {
		_publish();
		window_start_usec = now;
	}

	rs->canvas_item_clear(item);
	const Ref<Font> font = ThemeDB::get_singleton()->get_fallback_font();
	const int size = ThemeDB::get_singleton()->get_fallback_font_size();
	const float line_height = font->get_height(size);
	const int rows = lines.size() + (passes.is_empty() ? 0 : passes.size() + 1);
	const float height = MARGIN * 3 + GRAPH_HEIGHT + line_height * rows;
	const Vector2 corner(p_root->get_visible_rect().size.x - WIDTH - MARGIN, MARGIN);
	rs->canvas_item_add_rect(item, Rect2(corner, Vector2(WIDTH, height)), Color(0, 0, 0, 0.6));

	// The graph: each frame's time, with a line where 60 Hz is.
	const Rect2 graph(corner + Vector2(MARGIN, MARGIN), Vector2(WIDTH - MARGIN * 2, GRAPH_HEIGHT));
	const float target_y = graph.position.y + graph.size.y * (1.0f - (1000.0f / 60.0f) / GRAPH_MS);
	rs->canvas_item_add_line(item, Vector2(graph.position.x, target_y), Vector2(graph.get_end().x, target_y), Color(1, 1, 1, 0.25));
	if (frame_ms.size() > 1) {
		Vector<Vector2> points;
		for (int i = 0; i < frame_ms.size(); i++) {
			const float x = graph.position.x + graph.size.x * float(i) / float(HISTORY - 1);
			const float y = graph.get_end().y - graph.size.y * MIN(frame_ms[i] / GRAPH_MS, 1.0f);
			points.push_back(Vector2(x, y));
		}
		rs->canvas_item_add_polyline(item, points, { Color(0.55, 0.85, 1.0) }, 1.5);
	}

	float y = graph.get_end().y + MARGIN + font->get_ascent(size);
	const float left = corner.x + MARGIN;
	const float right = corner.x + WIDTH - MARGIN;
	auto row = [&](const String &p_label, const String &p_value, const Color &p_colour) {
		const float value_width = font->get_string_size(p_value, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x;
		// A label too long for the space beside its value loses its end.
		String label = p_label;
		const float room = right - left - value_width - MARGIN;
		while (label.length() > 1 && font->get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x > room) {
			label = label.substr(0, label.length() - 2) + U"…";
		}
		font->draw_string(item, Vector2(left, y), label, HORIZONTAL_ALIGNMENT_LEFT, -1, size, p_colour);
		font->draw_string(item, Vector2(right - value_width, y), p_value, HORIZONTAL_ALIGNMENT_LEFT, -1, size, Color(1, 1, 1));
		y += line_height;
	};
	for (const Pair<String, String> &line : lines) {
		row(line.first, line.second, Color(1, 1, 1, 0.7));
	}
	if (!passes.is_empty()) {
		row("GPU passes", String(), Color(1, 1, 1, 0.7));
		for (const Pair<String, String> &pass : passes) {
			row("  " + pass.first, pass.second, Color(1, 1, 1, 0.55));
		}
	}
}

void PerformanceOverlay::_sample(Viewport *p_root, uint64_t p_now) {
	if (last_frame_usec != 0) {
		const float frame = float(p_now - last_frame_usec) / 1000.0f;
		frame_ms.push_back(frame);
		if (frame_ms.size() > HISTORY) {
			frame_ms.remove_at(0);
		}
		frame_sum += frame;
		frames_summed++;
	}
	last_frame_usec = p_now;

	gpu_sum += GpuTime::frame_ms(p_root);
	window_frames++;

	// A pass's time is from its timestamp to the next; > and < open and close groups.
	RenderingServer *rs = RenderingServer::get_singleton();
	const uint64_t profile_frame = rs->get_frame_profile_frame();
	if (profile_frame != last_profile_frame) {
		last_profile_frame = profile_frame;
		const Vector<RenderingServerTypes::FrameProfileArea> areas = rs->get_frame_profile();
		for (int i = 0; i + 1 < areas.size(); i++) {
			const String &name = areas[i].name;
			if (name.is_empty() || name[0] == '<' || name[0] == '>') {
				continue;
			}
			pass_sums[name] = pass_sums.has(name) ? pass_sums[name] + (areas[i + 1].gpu_msec - areas[i].gpu_msec) : areas[i + 1].gpu_msec - areas[i].gpu_msec;
		}
		profile_frames++;
	}
}

void PerformanceOverlay::_publish() {
	const Performance *performance = Performance::get_singleton();
	const double frames = MAX(window_frames, 1);
	lines.clear();
	const double frame = frame_sum / MAX(frames_summed, 1);
	lines.push_back({ "FPS", String::num(frame > 0.0 ? 1000.0 / frame : 0.0, 0) });
	lines.push_back({ "Frame", ms(frame) });
	// The engine keeps these as the slowest of the last second.
	lines.push_back({ "CPU: process (max)", ms(performance->get_monitor(Performance::TIME_PROCESS) * 1000.0) });
	lines.push_back({ "CPU: physics (max)", ms(performance->get_monitor(Performance::TIME_PHYSICS_PROCESS) * 1000.0) });
	lines.push_back({ "GPU", ms(gpu_sum / frames) });
	lines.push_back({ "Draw calls", String::num_int64(int64_t(performance->get_monitor(Performance::RENDER_TOTAL_DRAW_CALLS_IN_FRAME))) });
	lines.push_back({ "Objects", String::num_int64(int64_t(performance->get_monitor(Performance::RENDER_TOTAL_OBJECTS_IN_FRAME))) });
	lines.push_back({ "Primitives", String::num_int64(int64_t(performance->get_monitor(Performance::RENDER_TOTAL_PRIMITIVES_IN_FRAME))) });
	lines.push_back({ "Video memory", String::num(performance->get_monitor(Performance::RENDER_VIDEO_MEM_USED) / (1024.0 * 1024.0), 0) + " MB" });

	// The five costliest passes, per frame.
	passes.clear();
	if (profile_frames > 0) {
		Vector<Pair<double, String>> costs;
		for (const KeyValue<String, double> &pass : pass_sums) {
			costs.push_back({ pass.value / profile_frames, pass.key });
		}
		costs.sort_custom<PairSort<double, String>>();
		for (int i = costs.size() - 1; i >= 0 && passes.size() < 5; i--) {
			passes.push_back({ costs[i].second.trim_prefix("Render "), ms(costs[i].first) });
		}
	}

	window_frames = 0;
	frame_sum = 0.0;
	frames_summed = 0;
	gpu_sum = 0.0;
	profile_frames = 0;
	pass_sums.clear();
}
