#include "mcp_process.h"
#include "mcp_tool.h"

#include "core/core_bind.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

namespace {

List<String> base_arguments(bool p_headless) {
	List<String> arguments;
	if (p_headless) {
		arguments.push_back("--headless");
	}
	arguments.push_back("--path");
	arguments.push_back(McpProcess::project_path());
	return arguments;
}

String ended(const McpProcess::Run &p_run) {
	if (!p_run.started) {
		return "It couldn't start.";
	}
	return p_run.timed_out ? "It ran out of time and was stopped." : vformat("It exited with code %d.", p_run.exit_code);
}

Dictionary run_info(const McpProcess::Run &p_run, const PackedStringArray &p_problems) {
	Dictionary info;
	info["exit_code"] = p_run.exit_code;
	info["timed_out"] = p_run.timed_out;
	Array problems;
	for (const String &problem : p_problems) {
		problems.push_back(problem);
	}
	info["problems"] = problems;
	return info;
}

McpResult check_project(const Dictionary &p_arguments) {
	List<String> arguments = base_arguments(true);
	arguments.push_back("--check-project");
	const McpProcess::Run run = McpProcess::run(arguments, 300.0);
	Array errors;
	Array warnings;
	String listed;
	for (const String &line : run.output.split("\n")) {
		if (line.begins_with("ERROR    ")) {
			errors.push_back(line.substr(9).strip_edges());
			listed += line.strip_edges() + "\n";
		} else if (line.begins_with("WARNING  ")) {
			warnings.push_back(line.substr(9).strip_edges());
			listed += line.strip_edges() + "\n";
		}
	}
	McpResult result;
	result.text = errors.is_empty() && warnings.is_empty() ? "No problems: every scene, resource and script loads." : listed;
	result.text += "\n" + ended(run);
	result.error = run.timed_out || !run.started;
	Dictionary structured;
	structured["errors"] = errors;
	structured["warnings"] = warnings;
	structured["exit_code"] = run.exit_code;
	result.structured = structured;
	return result;
}

McpResult check_script(const Dictionary &p_arguments) {
	const String path = McpTools::text(p_arguments, "path");
	if (!path.ends_with(".gd")) {
		return McpResult::failed("Give a GDScript file, such as res://player.gd.");
	}
	List<String> arguments = base_arguments(true);
	arguments.push_back("--check-only");
	arguments.push_back("--script");
	arguments.push_back(path);
	const McpProcess::Run run = McpProcess::run(arguments, 60.0);
	const PackedStringArray problems = McpProcess::problems(run.output);
	McpResult result;
	result.text = problems.is_empty() && run.exit_code == 0 ? path + " parses without errors." : String("\n").join(problems) + "\n" + McpProcess::tail(run.output, 20);
	result.structured = run_info(run, problems);
	return result;
}

McpResult run_tests(const Dictionary &p_arguments) {
	const String filter = McpTools::text(p_arguments, "filter");
	List<String> arguments = base_arguments(true);
	arguments.push_back(filter.is_empty() ? String("--run-tests") : "--run-tests=" + filter);
	const McpProcess::Run run = McpProcess::run(arguments, double(McpTools::integer(p_arguments, "timeout_seconds", 600)));
	Array failures;
	String summary;
	for (const String &line : run.output.split("\n")) {
		if (line.begins_with("FAIL  ")) {
			failures.push_back(line.substr(6).strip_edges());
		}
		if (line.contains(" passed") || line.contains(" failed") || line.begins_with("No tests")) {
			summary = line.strip_edges();
		}
	}
	McpResult result;
	result.text = (summary.is_empty() ? McpProcess::tail(run.output, 30) : summary) + "\n";
	for (const Variant &failure : failures) {
		result.text += "FAIL  " + String(failure) + "\n";
	}
	const PackedStringArray problems = McpProcess::problems(run.output);
	if (!problems.is_empty()) {
		result.text += "\nErrors while running:\n" + String("\n").join(problems) + "\n";
	}
	result.text += ended(run);
	Dictionary structured = run_info(run, problems);
	structured["summary"] = summary;
	structured["failures"] = failures;
	result.structured = structured;
	return result;
}

McpResult run_scene(const Dictionary &p_arguments) {
	const String scene = McpTools::text(p_arguments, "scene");
	const int64_t frames = CLAMP(McpTools::integer(p_arguments, "frames", 300), 1, 100000);
	List<String> arguments = base_arguments(!McpTools::flag(p_arguments, "window", false));
	if (!scene.is_empty()) {
		arguments.push_back(scene);
	}
	arguments.push_back("--quit-after");
	arguments.push_back(itos(frames));
	const McpProcess::Run run = McpProcess::run(arguments, double(McpTools::integer(p_arguments, "timeout_seconds", 120)));
	const PackedStringArray problems = McpProcess::problems(run.output);
	McpResult result;
	result.text = problems.is_empty() ? String("No errors or warnings.\n") : "Errors and warnings:\n" + String("\n").join(problems) + "\n";
	result.text += "\nOutput (the end of it):\n" + McpProcess::tail(run.output, 40) + "\n\n" + ended(run);
	Dictionary structured = run_info(run, problems);
	structured["output_tail"] = McpProcess::tail(run.output, 40);
	result.structured = structured;
	return result;
}

// A game in a window, its own window in front, for the capture node to save from.
McpProcess::Run run_captured(const Dictionary &p_arguments, const String &p_mode, const String &p_out, double p_seconds) {
	List<String> arguments = base_arguments(false);
	const String scene = McpTools::text(p_arguments, "scene");
	if (!scene.is_empty()) {
		arguments.push_back(scene);
	}
	const int64_t width = McpTools::integer(p_arguments, "width", 1280);
	const int64_t height = McpTools::integer(p_arguments, "height", 720);
	arguments.push_back("--resolution");
	arguments.push_back(vformat("%dx%d", width, height));
	arguments.push_back("--");
	arguments.push_back("--purr-mcp-capture=" + p_mode);
	arguments.push_back("--purr-mcp-out=" + p_out);
	arguments.push_back("--purr-mcp-seconds=" + rtos(p_seconds));
	return McpProcess::run(arguments, p_seconds + 120.0);
}

String temporary(const String &p_extension) {
	return OS::get_singleton()->get_temp_path().path_join(vformat("purr_mcp_%d_%d.%s", OS::get_singleton()->get_process_id(), OS::get_singleton()->get_ticks_usec(), p_extension));
}

McpResult screenshot(const Dictionary &p_arguments) {
	const String out = temporary("png");
	const double seconds = CLAMP(McpTools::integer(p_arguments, "seconds", 2), 0, 120);
	const McpProcess::Run run = run_captured(p_arguments, "screenshot", out, seconds);
	const PackedByteArray png = FileAccess::get_file_as_bytes(out);
	DirAccess::remove_absolute(out);
	const PackedStringArray problems = McpProcess::problems(run.output);
	if (png.is_empty()) {
		return McpResult::failed("No screenshot was taken. " + ended(run) + "\n" + String("\n").join(problems));
	}
	McpResult result;
	result.text = vformat("The game after %d seconds.", int(seconds)) + (problems.is_empty() ? String() : "\nErrors and warnings:\n" + String("\n").join(problems));
	Dictionary image;
	image["data"] = CryptoCore::b64_encode_str(png.ptr(), png.size());
	image["mimeType"] = "image/png";
	result.images.push_back(image);
	result.structured = run_info(run, problems);
	return result;
}

McpResult measure(const Dictionary &p_arguments) {
	const String out = temporary("json");
	const double seconds = CLAMP(McpTools::integer(p_arguments, "seconds", 10), 1, 300);
	const McpProcess::Run run = run_captured(p_arguments, "measure", out, seconds);
	const String text = FileAccess::get_file_as_string(out);
	DirAccess::remove_absolute(out);
	const Variant measured = JSON::parse_string(text);
	const PackedStringArray problems = McpProcess::problems(run.output);
	if (measured.get_type() != Variant::DICTIONARY) {
		return McpResult::failed("Nothing was measured. " + ended(run) + "\n" + String("\n").join(problems));
	}
	const Dictionary stats = measured;
	McpResult result;
	result.text = vformat("Over %d frames: mean %.2f ms, median %.2f ms, 95th percentile %.2f ms, worst %.2f ms. GPU %.2f ms a frame "
						  "(on a Mac this counts the GPU's idle time too, so it reads high). %d draw calls, %d objects, %d primitives, %.0f MB of video memory. "
						  "Screen at %.0f Hz, frame rate cap %d.",
			int(stats["frames"]), double(stats["mean_ms"]), double(stats["median_ms"]), double(stats["p95_ms"]), double(stats["max_ms"]), double(stats["gpu_ms"]),
			int(stats["draw_calls"]), int(stats["objects"]), int(stats["primitives"]), double(stats["video_memory_mb"]), double(stats["refresh_rate"]), int(stats["max_fps"]));
	if (!problems.is_empty()) {
		result.text += "\nErrors and warnings:\n" + String("\n").join(problems);
	}
	result.structured = stats;
	return result;
}

McpTool slow_tool(const char *p_name, const char *p_title, const char *p_description, const Dictionary &p_schema, McpResult (*p_run)(const Dictionary &)) {
	McpTool tool;
	tool.name = p_name;
	tool.title = p_title;
	tool.description = p_description;
	tool.input_schema = p_schema;
	tool.slow = true;
	tool.run = p_run;
	return tool;
}

} // namespace

void McpTools::add_run_tools(Vector<McpTool> &r_tools) {
	r_tools.push_back(slow_tool("check_project", "Check the project",
			"Loads every scene, resource and script in the project, as `purr --check-project` does, and lists those that are broken: missing files, scripts that don't parse, "
			"a main scene or autoload that isn't there. Run it after changing files.",
			McpTool::schema({}), &check_project));
	r_tools.push_back(slow_tool("check_script", "Check a script",
			"Parses one GDScript file and lists its errors and warnings: quicker than check_project after editing a script.",
			McpTool::schema({ { "path", "string", "The script, such as res://player.gd.", true } }), &check_script));
	r_tools.push_back(slow_tool("run_tests", "Run the tests",
			"Runs the project's tests (TestCase scripts in testing/tests_folder), as `purr --run-tests` does, and lists the failures.",
			McpTool::schema({
					{ "filter", "string", "Only tests whose file or name contains this." },
					{ "timeout_seconds", "integer", "Stops the run after this long, 600 if not given." },
			}),
			&run_tests));
	McpTool run = slow_tool("run_scene", "Run a scene",
			"Runs the game (or one scene) for a number of frames, without a window unless asked, and returns its errors, warnings and the end of its output. "
			"Its scripts and autoloads run as in the game, so it does what the game does.",
			McpTool::schema({
					{ "scene", "string", "The scene to run, such as res://levels/forest.tscn. The main scene if not given." },
					{ "frames", "integer", "How many frames to run, 300 if not given." },
					{ "window", "boolean", "Run in a window, with rendering, rather than headless." },
					{ "timeout_seconds", "integer", "Stops it after this long, 120 if not given." },
			}),
			&run_scene);
	run.read_only = false;
	r_tools.push_back(run);
	McpTool shot = slow_tool("screenshot", "Screenshot",
			"Runs the game (or one scene) in a window for a few seconds and returns a picture of it: to see what a change looks like. The window comes to the front while it runs.",
			McpTool::schema({
					{ "scene", "string", "The scene to show, such as res://levels/forest.tscn. The main scene if not given." },
					{ "seconds", "integer", "How long to run before taking it, 2 if not given." },
					{ "width", "integer", "The window's width, 1280 if not given." },
					{ "height", "integer", "The window's height, 720 if not given." },
			}),
			&screenshot);
	shot.read_only = false;
	r_tools.push_back(shot);
	McpTool timing = slow_tool("measure", "Measure performance",
			"Runs the game (or one scene) in a window, in front, and measures its frame times, GPU time, draw calls, objects, primitives and video memory. "
			"Measure before and after a change meant to make it faster.",
			McpTool::schema({
					{ "scene", "string", "The scene to measure. The main scene if not given." },
					{ "seconds", "integer", "How long to measure, 10 if not given (after a second to load)." },
					{ "width", "integer", "The window's width, 1280 if not given." },
					{ "height", "integer", "The window's height, 720 if not given." },
			}),
			&measure);
	timing.read_only = false;
	r_tools.push_back(timing);
}
