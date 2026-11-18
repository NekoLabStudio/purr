#pragma once

// TestCase: what a test script extends. Every method whose name starts with test_ is a test,
// run by `purr --run-tests` with _before_each() and _after_each() round it. Assertions record a
// failure (with the line it came from) and let the test go on; a test may await.

#include "test_measure.h"
#include "test_wait.h"

#include "scene/main/node.h"

class TestCase : public Node {
	GDCLASS(TestCase, Node);

public:
	struct Failure {
		String message;
		int line = 0;
	};

private:
	Vector<Failure> failures;
	String skip_reason;
	bool skipped = false;
	Vector<Ref<TestWait>> waits; // Kept alive until they've finished.
	Vector<Ref<TestMeasure>> measures; // So are these.

	void _fail(const String &p_message, const String &p_detail);
	static String _show(const Variant &p_value);
	Signal _wait(const Ref<TestWait> &p_wait);

protected:
	static void _bind_methods();

	GDVIRTUAL0(_before_all)
	GDVIRTUAL0(_after_all)
	GDVIRTUAL0(_before_each)
	GDVIRTUAL0(_after_each)

public:
	void assert_true(bool p_condition, const String &p_message = String());
	void assert_false(bool p_condition, const String &p_message = String());
	void assert_equal(const Variant &p_actual, const Variant &p_expected, const String &p_message = String());
	void assert_not_equal(const Variant &p_actual, const Variant &p_expected, const String &p_message = String());
	void assert_almost_equal(const Variant &p_actual, const Variant &p_expected, const String &p_message = String());
	void assert_greater(const Variant &p_actual, const Variant &p_than, const String &p_message = String());
	void assert_less(const Variant &p_actual, const Variant &p_than, const String &p_message = String());
	void assert_null(const Variant &p_value, const String &p_message = String());
	void assert_not_null(const Variant &p_value, const String &p_message = String());
	void assert_has(const Variant &p_container, const Variant &p_value, const String &p_message = String());
	void fail(const String &p_message);
	void skip(const String &p_reason = String());

	Signal wait_frames(int p_count);
	Signal wait_physics_frames(int p_count);
	Signal wait_seconds(double p_seconds);
	Signal wait_for(const Signal &p_signal, double p_timeout = 5.0);
	Signal measure(const String &p_what, const Callable &p_step, int p_frames = 120);

	// For the runner.
	void begin_test();
	const Vector<Failure> &get_failures() const { return failures; }
	bool is_skipped() const { return skipped; }
	String get_skip_reason() const { return skip_reason; }
};
