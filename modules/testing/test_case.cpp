#include "test_case.h"

#include "test_wait.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/object/script_backtrace.h"
#include "core/object/script_language.h"

// The line in this test's script that the failing assertion was called from.
void TestCase::_fail(const String &p_message, const String &p_detail) {
	Failure failure;
	failure.message = p_message.is_empty() ? p_detail : vformat("%s (%s)", p_message, p_detail);
	const Ref<Script> script = get_script();
	const String path = script.is_valid() ? script->get_path() : String();
	for (const Ref<ScriptBacktrace> &backtrace : ScriptServer::capture_script_backtraces()) {
		for (int i = 0; backtrace.is_valid() && i < backtrace->get_frame_count(); i++) {
			if (backtrace->get_frame_file(i) == path) {
				failure.line = backtrace->get_frame_line(i);
				break;
			}
		}
		if (failure.line != 0) {
			break;
		}
	}
	failures.push_back(failure);
}

String TestCase::_show(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::STRING:
		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
			return String(p_value).json_escape().quote();
		case Variant::NIL:
			return "null";
		default:
			return p_value.stringify();
	}
}

void TestCase::begin_test() {
	failures.clear();
	skipped = false;
	skip_reason = String();
}

void TestCase::assert_true(bool p_condition, const String &p_message) {
	if (!p_condition) {
		_fail(p_message, "expected true, got false");
	}
}

void TestCase::assert_false(bool p_condition, const String &p_message) {
	if (p_condition) {
		_fail(p_message, "expected false, got true");
	}
}

void TestCase::assert_equal(const Variant &p_actual, const Variant &p_expected, const String &p_message) {
	bool valid = false;
	const Variant same = Variant::evaluate(Variant::OP_EQUAL, p_actual, p_expected);
	valid = same.get_type() == Variant::BOOL && bool(same);
	if (!valid) {
		_fail(p_message, vformat("expected %s, got %s", _show(p_expected), _show(p_actual)));
	}
}

void TestCase::assert_not_equal(const Variant &p_actual, const Variant &p_expected, const String &p_message) {
	const Variant same = Variant::evaluate(Variant::OP_EQUAL, p_actual, p_expected);
	if (same.get_type() == Variant::BOOL && bool(same)) {
		_fail(p_message, vformat("expected anything but %s", _show(p_expected)));
	}
}

// Numbers, vectors, colours, quaternions, bases and transforms, close enough to count as equal.
void TestCase::assert_almost_equal(const Variant &p_actual, const Variant &p_expected, const String &p_message) {
	bool close = false;
	const bool numbers = (p_actual.get_type() == Variant::FLOAT || p_actual.get_type() == Variant::INT) && (p_expected.get_type() == Variant::FLOAT || p_expected.get_type() == Variant::INT);
	if (numbers) {
		close = Math::is_equal_approx(double(p_actual), double(p_expected));
	} else if (p_actual.get_type() == p_expected.get_type() && p_actual.has_method(SNAME("is_equal_approx"))) {
		Callable::CallError error;
		const Variant *argument = &p_expected;
		Variant result;
		Variant actual = p_actual;
		actual.callp(SNAME("is_equal_approx"), &argument, 1, result, error);
		close = error.error == Callable::CallError::CALL_OK && bool(result);
	} else {
		const Variant same = Variant::evaluate(Variant::OP_EQUAL, p_actual, p_expected);
		close = same.get_type() == Variant::BOOL && bool(same);
	}
	if (!close) {
		_fail(p_message, vformat("expected about %s, got %s", _show(p_expected), _show(p_actual)));
	}
}

void TestCase::assert_greater(const Variant &p_actual, const Variant &p_than, const String &p_message) {
	const Variant greater = Variant::evaluate(Variant::OP_GREATER, p_actual, p_than);
	if (greater.get_type() != Variant::BOOL || !bool(greater)) {
		_fail(p_message, vformat("expected more than %s, got %s", _show(p_than), _show(p_actual)));
	}
}

void TestCase::assert_less(const Variant &p_actual, const Variant &p_than, const String &p_message) {
	const Variant less = Variant::evaluate(Variant::OP_LESS, p_actual, p_than);
	if (less.get_type() != Variant::BOOL || !bool(less)) {
		_fail(p_message, vformat("expected less than %s, got %s", _show(p_than), _show(p_actual)));
	}
}

void TestCase::assert_null(const Variant &p_value, const String &p_message) {
	if (p_value.get_type() != Variant::NIL && !(p_value.get_type() == Variant::OBJECT && p_value.get_validated_object() == nullptr)) {
		_fail(p_message, vformat("expected null, got %s", _show(p_value)));
	}
}

void TestCase::assert_not_null(const Variant &p_value, const String &p_message) {
	if (p_value.get_type() == Variant::NIL || (p_value.get_type() == Variant::OBJECT && p_value.get_validated_object() == nullptr)) {
		_fail(p_message, "expected a value, got null");
	}
}

// What GDScript's `in` finds: an item of an array, a key of a dictionary, a part of a string.
void TestCase::assert_has(const Variant &p_container, const Variant &p_value, const String &p_message) {
	bool valid = false;
	Variant found;
	Variant::evaluate(Variant::OP_IN, p_value, p_container, found, valid);
	if (!valid || !bool(found)) {
		_fail(p_message, vformat("expected %s in %s", _show(p_value), _show(p_container)));
	}
}

void TestCase::fail(const String &p_message) {
	_fail(String(), p_message);
}

void TestCase::skip(const String &p_reason) {
	skipped = true;
	skip_reason = p_reason;
}

Signal TestCase::_wait(const Ref<TestWait> &p_wait) {
	for (int i = waits.size() - 1; i >= 0; i--) {
		if (waits[i]->is_finished()) {
			waits.remove_at(i);
		}
	}
	waits.push_back(p_wait);
	return p_wait->get_done();
}

Signal TestCase::wait_frames(int p_count) {
	Ref<TestWait> wait;
	wait.instantiate();
	wait->start_frames(p_count, false);
	return _wait(wait);
}

Signal TestCase::wait_physics_frames(int p_count) {
	Ref<TestWait> wait;
	wait.instantiate();
	wait->start_frames(p_count, true);
	return _wait(wait);
}

Signal TestCase::wait_seconds(double p_seconds) {
	Ref<TestWait> wait;
	wait.instantiate();
	wait->start_seconds(p_seconds);
	return _wait(wait);
}

Signal TestCase::wait_for(const Signal &p_signal, double p_timeout) {
	Ref<TestWait> wait;
	wait.instantiate();
	wait->start_signal(p_signal, p_timeout);
	return _wait(wait);
}

Signal TestCase::measure(const String &p_what, const Callable &p_step, int p_frames) {
	for (int i = measures.size() - 1; i >= 0; i--) {
		if (measures[i]->is_finished()) {
			measures.remove_at(i);
		}
	}
	Ref<TestMeasure> run;
	run.instantiate();
	const Ref<Script> script = get_script();
	run->start(p_what, script.is_valid() ? script->get_path() : String(get_name()), p_step, p_frames);
	measures.push_back(run);
	return run->get_done();
}

void TestCase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("assert_true", "condition", "message"), &TestCase::assert_true, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_false", "condition", "message"), &TestCase::assert_false, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_equal", "actual", "expected", "message"), &TestCase::assert_equal, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_not_equal", "actual", "expected", "message"), &TestCase::assert_not_equal, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_almost_equal", "actual", "expected", "message"), &TestCase::assert_almost_equal, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_greater", "actual", "than", "message"), &TestCase::assert_greater, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_less", "actual", "than", "message"), &TestCase::assert_less, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_null", "value", "message"), &TestCase::assert_null, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_not_null", "value", "message"), &TestCase::assert_not_null, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("assert_has", "container", "value", "message"), &TestCase::assert_has, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("fail", "message"), &TestCase::fail);
	ClassDB::bind_method(D_METHOD("skip", "reason"), &TestCase::skip, DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("wait_frames", "count"), &TestCase::wait_frames);
	ClassDB::bind_method(D_METHOD("wait_physics_frames", "count"), &TestCase::wait_physics_frames);
	ClassDB::bind_method(D_METHOD("wait_seconds", "seconds"), &TestCase::wait_seconds);
	ClassDB::bind_method(D_METHOD("wait_for", "signal", "timeout"), &TestCase::wait_for, DEFVAL(5.0));
	ClassDB::bind_method(D_METHOD("measure", "what", "step", "frames"), &TestCase::measure, DEFVAL(120));

	GDVIRTUAL_BIND(_before_all);
	GDVIRTUAL_BIND(_after_all);
	GDVIRTUAL_BIND(_before_each);
	GDVIRTUAL_BIND(_after_each);
}
