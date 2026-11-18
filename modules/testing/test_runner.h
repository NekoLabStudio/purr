#pragma once

// TestRunner: the scene `purr --run-tests` runs instead of the game's. It finds test scripts
// (files ending in _test.gd, or starting with test_) in the tests folder, runs each one's test_
// methods in order, prints what failed and a summary, writes a JUnit report when asked, and
// quits with 1 if anything failed.

#include "test_case.h"

#include "core/os/mutex.h"
#include "scene/main/node.h"

class TestErrorLogger;

class TestRunner : public Node {
	GDCLASS(TestRunner, Node);

	enum Step {
		STEP_NEXT_FILE,
		STEP_BEFORE_ALL,
		STEP_NEXT_TEST,
		STEP_BEFORE_EACH,
		STEP_TEST,
		STEP_AFTER_EACH,
		STEP_FINISH_TEST,
		STEP_AFTER_ALL,
		STEP_DONE,
	};

	struct Result {
		String file;
		String test;
		Vector<TestCase::Failure> failures;
		bool skipped = false;
		String skip_reason;
		double seconds = 0.0;
	};

	String filter;
	String report_path;
	Vector<String> files;
	int file_index = -1;
	TestCase *current = nullptr;
	Vector<StringName> tests;
	int test_index = -1;
	HashSet<ObjectID> children_before;
	Step step = STEP_NEXT_FILE;

	Variant waiting_on; // A coroutine's state, while a call awaits.
	bool waiting = false;
	uint64_t deadline_usec = 0;
	uint64_t test_started_usec = 0;
	uint64_t run_started_usec = 0;

	Vector<Result> results;
	struct FileError {
		String file;
		String message;
	};
	Vector<FileError> file_errors; // Scripts that couldn't be run at all.
	TestErrorLogger *logger = nullptr; // Owned by OS, which outlives this.

	void _find_files(const String &p_folder);
	bool _call(const StringName &p_method);
	void _resumed(const Variant &p_result);
	void _stop_waiting();
	static double _timeout_seconds();
	void _record_script_errors();
	void _advance();
	void _start_file();
	void _end_file();
	void _finish();
	void _write_report(double p_seconds);
	static bool _is_test_file(const String &p_name);

protected:
	void _notification(int p_what);

public:
	TestRunner();
	~TestRunner();
};
