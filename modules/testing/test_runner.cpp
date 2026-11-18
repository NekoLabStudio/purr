#include "test_runner.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/logger.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

// Notes script errors (a test's GDScript failing as it runs), which end the function that hit
// them: a test that hits one fails, and an await it was in won't finish.
class TestErrorLogger : public Logger {
	Mutex mutex;
	Vector<String> errors;

public:
	bool active = true;

	virtual void logv(const char *p_format, va_list p_list, bool p_err) override {}

	virtual void log_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, bool p_editor_notify, ErrorType p_type, const Vector<Ref<ScriptBacktrace>> &p_script_backtraces) override {
		if (!active || p_type != ERR_SCRIPT) {
			return;
		}
		const String details = String::utf8(p_rationale && *p_rationale ? p_rationale : p_code);
		MutexLock lock(mutex);
		errors.push_back(vformat("%s (%s:%d)", details, String::utf8(p_file), p_line));
	}

	Vector<String> take() {
		MutexLock lock(mutex);
		Vector<String> taken = errors;
		errors.clear();
		return taken;
	}
};

namespace {
String xml_escape(const String &p_text) {
	return p_text.xml_escape(true);
}
} // namespace

TestRunner::TestRunner() {
	set_name("TestRunner");
	set_process_mode(PROCESS_MODE_ALWAYS);
}

TestRunner::~TestRunner() {
	if (logger != nullptr) {
		logger->active = false;
	}
}

bool TestRunner::_is_test_file(const String &p_name) {
	return p_name.ends_with(".gd") && (p_name.ends_with("_test.gd") || p_name.begins_with("test_"));
}

void TestRunner::_find_files(const String &p_folder) {
	Ref<DirAccess> dir = DirAccess::open(p_folder);
	// A .gdignore hides a folder from the project, except the project's own root.
	if (dir.is_null() || (p_folder != "res://" && dir->file_exists(".gdignore"))) {
		return;
	}
	Vector<String> names;
	Vector<String> folders;
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		if (dir->current_is_dir()) {
			folders.push_back(name);
		} else if (_is_test_file(name)) {
			names.push_back(name);
		}
	}
	dir->list_dir_end();
	names.sort();
	folders.sort();
	for (const String &name : names) {
		const String path = p_folder.path_join(name);
		if (filter.is_empty() || path.contains(filter)) {
			files.push_back(path);
		}
	}
	for (const String &folder : folders) {
		_find_files(p_folder.path_join(folder));
	}
}

void TestRunner::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			for (const String &argument : OS::get_singleton()->get_cmdline_args()) {
				if (argument.begins_with("--run-tests=")) {
					filter = argument.get_slicec('=', 1);
				} else if (argument.begins_with("--test-report=")) {
					report_path = argument.get_slicec('=', 1);
				}
			}
			logger = memnew(TestErrorLogger);
			OS::get_singleton()->add_logger(logger);
			run_started_usec = OS::get_singleton()->get_ticks_usec();
			const String folder = GLOBAL_GET("testing/tests_folder");
			_find_files(folder);
			// A filter can name a test as well as a file: run every file then, and only the
			// tests whose names match.
			if (files.is_empty() && !filter.is_empty()) {
				const String test_filter = filter;
				filter = String();
				_find_files(folder);
				filter = test_filter;
			}
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS: {
			if (waiting) {
				_record_script_errors();
				if (waiting && OS::get_singleton()->get_ticks_usec() > deadline_usec) {
					const double seconds = _timeout_seconds();
					current->fail(vformat("Timed out after %s s.", Math::is_equal_approx(seconds, Math::round(seconds)) ? itos(int64_t(Math::round(seconds))) : String::num(seconds, 1)));
					_stop_waiting();
				}
				if (waiting) {
					return;
				}
				waiting_on = Variant();
			}
			while (!waiting && step != STEP_DONE) {
				_advance();
			}
		} break;
	}
}

// Calls one of the test script's methods. When it awaits, the runner waits for it to finish
// (or fail, or time out) before going on.
bool TestRunner::_call(const StringName &p_method) {
	ScriptInstance *instance = current->get_script_instance();
	if (instance == nullptr || !instance->has_method(p_method)) {
		return false;
	}
	Callable::CallError error;
	const Variant result = current->callp(p_method, nullptr, 0, error);
	if (error.error != Callable::CallError::CALL_OK) {
		current->fail(vformat("Couldn't call %s(): it shouldn't take arguments.", p_method));
	}
	Object *state = result.get_validated_object();
	if (state != nullptr && state->has_signal(SNAME("completed"))) {
		waiting_on = result;
		waiting = true;
		deadline_usec = OS::get_singleton()->get_ticks_usec() + uint64_t(_timeout_seconds() * 1000000.0);
		state->connect(SNAME("completed"), callable_mp(this, &TestRunner::_resumed), CONNECT_ONE_SHOT);
	}
	_record_script_errors();
	return waiting;
}

void TestRunner::_resumed(const Variant &p_result) {
	waiting = false;
}

// A script error fails the test it happened in, and ends any await: the function that hit it
// has stopped.
void TestRunner::_record_script_errors() {
	if (logger == nullptr) {
		return;
	}
	const Vector<String> errors = logger->take();
	if (errors.is_empty() || current == nullptr) {
		return;
	}
	for (const String &error : errors) {
		current->fail("Script error: " + error);
	}
	if (waiting) {
		_stop_waiting();
	}
}

// Stops waiting on an await that won't finish in time (or ever): when it does finish, it mustn't
// end whatever the runner is waiting on by then.
void TestRunner::_stop_waiting() {
	waiting = false;
	Object *state = waiting_on.get_validated_object();
	if (state != nullptr && state->is_connected(SNAME("completed"), callable_mp(this, &TestRunner::_resumed))) {
		state->disconnect(SNAME("completed"), callable_mp(this, &TestRunner::_resumed));
	}
	waiting_on = Variant();
}

double TestRunner::_timeout_seconds() {
	return MAX(double(GLOBAL_GET("testing/timeout_seconds")), 0.1);
}

void TestRunner::_start_file() {
	const String path = files[file_index];
	const Ref<Script> script = ResourceLoader::load(path, "Script");
	// Errors loading it (such as a script it uses not compiling) were printed, and belong to
	// no test.
	if (logger != nullptr) {
		logger->take();
	}
	if (script.is_null() || !script->is_script_valid()) {
		file_errors.push_back({ path, "doesn't compile (see the errors above)." });
		step = STEP_NEXT_FILE;
		return;
	}
	if (!ClassDB::is_parent_class(script->get_instance_base_type(), "TestCase")) {
		file_errors.push_back({ path, "doesn't extend TestCase, so it has no tests to run." });
		step = STEP_NEXT_FILE;
		return;
	}
	current = memnew(TestCase);
	current->set_name(path.get_file().get_basename().to_pascal_case());
	current->set_script(script);
	add_child(current);

	tests.clear();
	List<MethodInfo> methods;
	script->get_script_method_list(&methods);
	// A filter that matched no file names tests instead.
	const bool filter_tests = !filter.is_empty() && !path.contains(filter);
	for (const MethodInfo &method : methods) {
		const String name = method.name;
		if (name.begins_with("test_") && !tests.has(method.name) && (!filter_tests || name.contains(filter))) {
			tests.push_back(method.name);
		}
	}
	test_index = -1;
	step = STEP_BEFORE_ALL;
}

void TestRunner::_end_file() {
	if (current != nullptr) {
		// Failures in _before_all() or _after_all() belong to no test.
		if (!current->get_failures().is_empty()) {
			Result result;
			result.file = files[file_index];
			result.test = "_before_all/_after_all";
			result.failures = current->get_failures();
			results.push_back(result);
			for (const TestCase::Failure &failure : result.failures) {
				print_line(vformat("FAIL  %s  %s: %s", result.file, result.test, failure.message));
			}
		}
		remove_child(current);
		current->queue_free();
		current = nullptr;
	}
}

void TestRunner::_advance() {
	switch (step) {
		case STEP_NEXT_FILE:
			_end_file();
			file_index++;
			if (file_index >= files.size()) {
				step = STEP_DONE;
				_finish();
			} else {
				_start_file();
			}
			break;
		case STEP_BEFORE_ALL:
			current->begin_test();
			step = STEP_NEXT_TEST;
			_call("_before_all");
			break;
		case STEP_NEXT_TEST:
			test_index++;
			if (test_index >= tests.size()) {
				step = STEP_AFTER_ALL;
				break;
			}
			if (!current->get_failures().is_empty()) {
				// _before_all() failed: the tests would only fail after it.
				step = STEP_AFTER_ALL;
				break;
			}
			children_before.clear();
			for (int i = 0; i < current->get_child_count(true); i++) {
				children_before.insert(current->get_child(i, true)->get_instance_id());
			}
			current->begin_test();
			test_started_usec = OS::get_singleton()->get_ticks_usec();
			step = STEP_BEFORE_EACH;
			break;
		case STEP_BEFORE_EACH:
			step = STEP_TEST;
			_call("_before_each");
			break;
		case STEP_TEST:
			step = STEP_AFTER_EACH;
			if (current->get_failures().is_empty() && !current->is_skipped()) {
				_call(tests[test_index]);
			}
			break;
		case STEP_AFTER_EACH:
			step = STEP_FINISH_TEST;
			_call("_after_each");
			break;
		case STEP_FINISH_TEST: {
			Result result;
			result.file = files[file_index];
			result.test = tests[test_index];
			result.failures = current->get_failures();
			result.skipped = current->is_skipped() && result.failures.is_empty();
			result.skip_reason = current->get_skip_reason();
			result.seconds = (OS::get_singleton()->get_ticks_usec() - test_started_usec) / 1000000.0;
			results.push_back(result);
			for (const TestCase::Failure &failure : result.failures) {
				const String where = failure.line > 0 ? vformat("%s:%d", result.file, failure.line) : result.file;
				print_line(vformat("FAIL  %s  %s: %s", where, result.test, failure.message));
			}
			if (result.skipped) {
				print_line(vformat("SKIP  %s  %s%s", result.file, result.test, result.skip_reason.is_empty() ? String() : ": " + result.skip_reason));
			}
			// Nodes the test added to its script go with it.
			for (int i = current->get_child_count(true) - 1; i >= 0; i--) {
				Node *child = current->get_child(i, true);
				if (!children_before.has(child->get_instance_id())) {
					current->remove_child(child);
					child->queue_free();
				}
			}
			current->begin_test();
			step = STEP_NEXT_TEST;
		} break;
		case STEP_AFTER_ALL:
			step = STEP_NEXT_FILE;
			_call("_after_all");
			break;
		case STEP_DONE:
			break;
	}
}

void TestRunner::_finish() {
	const double seconds = (OS::get_singleton()->get_ticks_usec() - run_started_usec) / 1000000.0;
	int passed = 0;
	int failed = 0;
	int skipped = 0;
	for (const Result &result : results) {
		if (!result.failures.is_empty()) {
			failed++;
		} else if (result.skipped) {
			skipped++;
		} else {
			passed++;
		}
	}
	for (const FileError &error : file_errors) {
		print_line(vformat("FAIL  %s: %s", error.file, error.message));
	}
	const String folder = GLOBAL_GET("testing/tests_folder");
	bool ok = failed == 0 && file_errors.is_empty();
	if (results.is_empty() && file_errors.is_empty()) {
		print_line(filter.is_empty() ? vformat("No tests found in %s.", folder) : vformat("No tests match \"%s\".", filter));
		ok = false;
	} else {
		String summary = vformat("Tests: %d passed", passed);
		if (failed > 0) {
			summary += vformat(", %d failed", failed);
		}
		if (skipped > 0) {
			summary += vformat(", %d skipped", skipped);
		}
		if (!file_errors.is_empty()) {
			summary += vformat(", %d %s couldn't run", file_errors.size(), file_errors.size() == 1 ? "file" : "files");
		}
		print_line(summary + vformat(" (%.1f s).", seconds));
	}
	if (!report_path.is_empty()) {
		_write_report(seconds);
	}
	if (logger != nullptr) {
		logger->active = false;
	}
	get_tree()->quit(ok ? 0 : 1);
}

// A JUnit XML report, which CI systems show: a suite a file, a case a test.
void TestRunner::_write_report(double p_seconds) {
	String path = report_path;
	if (path.is_relative_path()) {
		Ref<DirAccess> here = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		path = here->get_current_dir().path_join(path);
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		ERR_PRINT(vformat("Couldn't write the test report to %s.", path));
		return;
	}
	HashMap<String, Vector<int>> by_file;
	Vector<String> order;
	for (int i = 0; i < results.size(); i++) {
		if (!by_file.has(results[i].file)) {
			order.push_back(results[i].file);
		}
		by_file[results[i].file].push_back(i);
	}
	int failures = 0;
	int skipped = 0;
	for (const Result &result : results) {
		failures += result.failures.is_empty() ? 0 : 1;
		skipped += result.skipped ? 1 : 0;
	}
	file->store_line("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
	file->store_line(vformat("<testsuites name=\"tests\" tests=\"%d\" failures=\"%d\" errors=\"%d\" skipped=\"%d\" time=\"%.3f\">", results.size(), failures, file_errors.size(), skipped, p_seconds));
	for (const String &suite : order) {
		const Vector<int> &indices = by_file[suite];
		double suite_seconds = 0.0;
		int suite_failures = 0;
		int suite_skipped = 0;
		for (int index : indices) {
			suite_seconds += results[index].seconds;
			suite_failures += results[index].failures.is_empty() ? 0 : 1;
			suite_skipped += results[index].skipped ? 1 : 0;
		}
		const String class_name = suite.trim_prefix("res://").get_basename().replace("/", ".");
		file->store_line(vformat("\t<testsuite name=\"%s\" tests=\"%d\" failures=\"%d\" skipped=\"%d\" time=\"%.3f\">", xml_escape(suite), indices.size(), suite_failures, suite_skipped, suite_seconds));
		for (int index : indices) {
			const Result &result = results[index];
			file->store_line(vformat("\t\t<testcase name=\"%s\" classname=\"%s\" time=\"%.3f\">", xml_escape(result.test), xml_escape(class_name), result.seconds));
			for (const TestCase::Failure &failure : result.failures) {
				const String where = failure.line > 0 ? vformat("%s:%d", suite, failure.line) : suite;
				file->store_line(vformat("\t\t\t<failure message=\"%s\">%s</failure>", xml_escape(failure.message), xml_escape(where)));
			}
			if (result.skipped) {
				file->store_line(vformat("\t\t\t<skipped message=\"%s\" />", xml_escape(result.skip_reason)));
			}
			file->store_line("\t\t</testcase>");
		}
		file->store_line("\t</testsuite>");
	}
	for (const FileError &error : file_errors) {
		file->store_line(vformat("\t<testsuite name=\"%s\" tests=\"0\" errors=\"1\"><error message=\"%s\" /></testsuite>", xml_escape(error.file), xml_escape(error.message)));
	}
	file->store_line("</testsuites>");
}
