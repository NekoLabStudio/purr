#include "play_games.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

PlayGames *PlayGames::singleton = nullptr;

void PlayGamesCall::_bind_methods() {
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
}

namespace {

// Saved games' names may only use letters, digits and "-._~", up to 100 characters: anything
// else is written as ~ and its UTF-8 bytes in hex.
String encode_name(const String &p_name) {
	const CharString utf8 = p_name.utf8();
	String encoded;
	for (int i = 0; i < utf8.length(); i++) {
		const uint8_t c = utf8[i];
		if (is_ascii_alphanumeric_char(c) || c == '-' || c == '.' || c == '_') {
			encoded += String::chr(c);
		} else {
			encoded += "~" + String::num_int64(c, 16, true).lpad(2, "0");
		}
	}
	return encoded;
}

String decode_name(const String &p_name) {
	CharString utf8;
	for (int i = 0; i < p_name.length(); i++) {
		if (p_name[i] == '~' && i + 2 < p_name.length()) {
			utf8 += (char)p_name.substr(i + 1, 2).hex_to_int();
			i += 2;
		} else {
			utf8 += (char)p_name[i];
		}
	}
	return String::utf8(utf8.get_data());
}

// The save's metadata (its summary) travels as JSON in the saved game's description.
Dictionary metadata_of(const Dictionary &p_file) {
	const Variant parsed = JSON::parse_string(p_file.get("description", ""));
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

} // namespace

PlayGames::PlayGames() {
	singleton = this;
}

PlayGames::~PlayGames() {
	singleton = nullptr;
}

Object *PlayGames::android() const {
	return Engine::get_singleton()->has_singleton("PlayGamesAndroid") ? Engine::get_singleton()->get_singleton_object("PlayGamesAndroid") : nullptr;
}

// Godot registers Java plugins after the modules, so the plugin is looked up when first used.
bool PlayGames::connect_android() {
	Object *plugin = android();
	if (!plugin) {
		return false;
	}
	const Callable on_finished = callable_mp(this, &PlayGames::finished);
	if (!plugin->is_connected(SNAME("finished"), on_finished)) {
		plugin->connect(SNAME("finished"), on_finished);
	}
	return true;
}

bool PlayGames::is_available() const {
	return android() && !String(GLOBAL_GET("play_games/app_id")).strip_edges().is_empty();
}

bool PlayGames::is_saved_games_enabled() const {
	return is_authenticated() && (bool)GLOBAL_GET("play_games/saved_games");
}

bool PlayGames::is_authenticated() const {
	return !player_id.is_empty();
}

int PlayGames::begin_call(Ref<PlayGamesCall> &r_call) {
	r_call.instantiate();
	const int id = next_call_id++;
	pending[id] = r_call;
	return id;
}

// Fails a call before it starts; deferred so the caller can connect to the signal first.
Signal PlayGames::fail_now(const String &p_error) {
	Ref<PlayGamesCall> call;
	const int id = begin_call(call);
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	callable_mp(this, &PlayGames::finished).call_deferred(id, result);
	return Signal(call.ptr(), SNAME("completed"));
}

void PlayGames::finished(int p_call_id, const Dictionary &p_result) {
	if (!pending.has(p_call_id)) {
		return;
	}
	const Ref<PlayGamesCall> call = pending[p_call_id];
	pending.erase(p_call_id);
	call->emit_signal(SNAME("completed"), p_result);
}

// Signing in.

Signal PlayGames::authenticate(bool p_interactive) {
	if (!is_available() || !connect_android()) {
		return fail_now("unavailable");
	}
	Ref<PlayGamesCall> android_call;
	const int id = begin_call(android_call);
	Ref<PlayGamesCall> call;
	call.instantiate();
	android_call->connect(SNAME("completed"), callable_mp(this, &PlayGames::signed_in).bind(call), CONNECT_ONE_SHOT);
	android()->call("signIn", id, p_interactive);
	return Signal(call.ptr(), SNAME("completed"));
}

void PlayGames::signed_in(const Dictionary &p_result, const Ref<PlayGamesCall> &p_call) {
	player_id = p_result.get("ok", false) ? String(p_result.get("player_id", "")) : String();
	player_name = p_result.get("ok", false) ? String(p_result.get("player_name", "")) : String();
	p_call->emit_signal(SNAME("completed"), p_result);
}

Signal PlayGames::request_server_auth_code() {
	const String client_id = String(GLOBAL_GET("play_games/server_client_id")).strip_edges();
	if (!is_authenticated() || client_id.is_empty() || !connect_android()) {
		return fail_now(client_id.is_empty() ? "not_configured" : "not_signed_in");
	}
	Ref<PlayGamesCall> call;
	const int id = begin_call(call);
	android()->call("requestServerAuthCode", id, client_id);
	return Signal(call.ptr(), SNAME("completed"));
}

// Achievements and leaderboards.

void PlayGames::unlock_achievement(const String &p_id) {
	if (is_authenticated()) {
		android()->call("unlockAchievement", p_id);
	}
}

void PlayGames::increment_achievement(const String &p_id, int p_steps) {
	if (is_authenticated()) {
		android()->call("incrementAchievement", p_id, p_steps);
	}
}

void PlayGames::set_achievement_steps(const String &p_id, int p_steps) {
	if (is_authenticated()) {
		android()->call("setAchievementSteps", p_id, p_steps);
	}
}

// {"achievements": {id: {"unlocked", "current_steps", "total_steps"}}}, steps 0 for achievements
// that aren't incremental.
Signal PlayGames::get_achievements() {
	if (!is_authenticated() || !connect_android()) {
		return fail_now("not_signed_in");
	}
	Ref<PlayGamesCall> call;
	const int id = begin_call(call);
	android()->call("loadAchievements", id);
	return Signal(call.ptr(), SNAME("completed"));
}

// {"scores": [{"rank", "name", "score", "player"}]}: "global", "friends" or "around_player".
Signal PlayGames::get_scores(const String &p_leaderboard_id, Scope p_scope, int p_count) {
	if (!is_authenticated() || !connect_android()) {
		return fail_now("not_signed_in");
	}
	Ref<PlayGamesCall> call;
	const int id = begin_call(call);
	static const char *scopes[] = { "global", "friends", "around_player" };
	android()->call("loadScores", id, p_leaderboard_id, scopes[CLAMP((int)p_scope, 0, 2)], p_count);
	return Signal(call.ptr(), SNAME("completed"));
}

void PlayGames::submit_score(const String &p_leaderboard_id, int64_t p_score) {
	if (is_authenticated()) {
		android()->call("submitScore", p_leaderboard_id, p_score);
	}
}

void PlayGames::show_achievements() {
	if (is_authenticated()) {
		android()->call("showAchievements");
	}
}

void PlayGames::show_leaderboard(const String &p_leaderboard_id) {
	if (is_authenticated()) {
		android()->call("showLeaderboard", p_leaderboard_id);
	}
}

// Saved games.

Signal PlayGames::save_file(const String &p_name, const PackedByteArray &p_data, const Dictionary &p_metadata) {
	if (!is_saved_games_enabled()) {
		return fail_now("unavailable");
	}
	Ref<PlayGamesCall> call;
	const int id = begin_call(call);
	android()->call("saveFile", id, encode_name(p_name), p_data, JSON::stringify(p_metadata));
	return Signal(call.ptr(), SNAME("completed"));
}

Signal PlayGames::load_file(const String &p_name) {
	if (!is_saved_games_enabled()) {
		return fail_now("unavailable");
	}
	Ref<PlayGamesCall> android_call;
	const int id = begin_call(android_call);
	Ref<PlayGamesCall> call;
	call.instantiate();
	android_call->connect(SNAME("completed"), callable_mp(this, &PlayGames::file_loaded).bind(call), CONNECT_ONE_SHOT);
	android()->call("loadFile", id, encode_name(p_name));
	return Signal(call.ptr(), SNAME("completed"));
}

void PlayGames::file_loaded(const Dictionary &p_result, const Ref<PlayGamesCall> &p_call) {
	Dictionary result = p_result.duplicate();
	result["metadata"] = metadata_of(p_result);
	result.erase("description");
	p_call->emit_signal(SNAME("completed"), result);
}

Signal PlayGames::list_files() {
	if (!is_saved_games_enabled()) {
		return fail_now("unavailable");
	}
	Ref<PlayGamesCall> android_call;
	const int id = begin_call(android_call);
	Ref<PlayGamesCall> call;
	call.instantiate();
	android_call->connect(SNAME("completed"), callable_mp(this, &PlayGames::file_listed).bind(call), CONNECT_ONE_SHOT);
	android()->call("listFiles", id);
	return Signal(call.ptr(), SNAME("completed"));
}

void PlayGames::file_listed(const Dictionary &p_result, const Ref<PlayGamesCall> &p_call) {
	Dictionary result = p_result.duplicate();
	Array files;
	for (const Variant &entry : Array(p_result.get("files", Array()))) {
		const Dictionary file = entry;
		Dictionary listed;
		listed["name"] = decode_name(file.get("name", ""));
		listed["modified"] = file.get("modified", 0);
		listed["metadata"] = metadata_of(file);
		files.push_back(listed);
	}
	result["files"] = files;
	p_call->emit_signal(SNAME("completed"), result);
}

Signal PlayGames::delete_file(const String &p_name) {
	if (!is_saved_games_enabled()) {
		return fail_now("unavailable");
	}
	Ref<PlayGamesCall> call;
	const int id = begin_call(call);
	android()->call("deleteFile", id, encode_name(p_name));
	return Signal(call.ptr(), SNAME("completed"));
}

void PlayGames::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &PlayGames::is_available);
	ClassDB::bind_method(D_METHOD("authenticate", "interactive"), &PlayGames::authenticate, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("is_authenticated"), &PlayGames::is_authenticated);
	ClassDB::bind_method(D_METHOD("get_player_name"), &PlayGames::get_player_name);
	ClassDB::bind_method(D_METHOD("get_player_id"), &PlayGames::get_player_id);
	ClassDB::bind_method(D_METHOD("request_server_auth_code"), &PlayGames::request_server_auth_code);
	ClassDB::bind_method(D_METHOD("unlock_achievement", "id"), &PlayGames::unlock_achievement);
	ClassDB::bind_method(D_METHOD("increment_achievement", "id", "steps"), &PlayGames::increment_achievement);
	ClassDB::bind_method(D_METHOD("set_achievement_steps", "id", "steps"), &PlayGames::set_achievement_steps);
	ClassDB::bind_method(D_METHOD("get_achievements"), &PlayGames::get_achievements);
	ClassDB::bind_method(D_METHOD("get_scores", "leaderboard_id", "scope", "count"), &PlayGames::get_scores, DEFVAL(SCOPE_GLOBAL), DEFVAL(10));
	ClassDB::bind_method(D_METHOD("submit_score", "leaderboard_id", "score"), &PlayGames::submit_score);
	ClassDB::bind_method(D_METHOD("show_achievements"), &PlayGames::show_achievements);
	ClassDB::bind_method(D_METHOD("show_leaderboard", "leaderboard_id"), &PlayGames::show_leaderboard, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("is_saved_games_enabled"), &PlayGames::is_saved_games_enabled);
	ClassDB::bind_method(D_METHOD("save_file", "name", "data", "metadata"), &PlayGames::save_file, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("load_file", "name"), &PlayGames::load_file);
	ClassDB::bind_method(D_METHOD("list_files"), &PlayGames::list_files);
	ClassDB::bind_method(D_METHOD("delete_file", "name"), &PlayGames::delete_file);

	BIND_ENUM_CONSTANT(SCOPE_GLOBAL);
	BIND_ENUM_CONSTANT(SCOPE_FRIENDS);
	BIND_ENUM_CONSTANT(SCOPE_AROUND_PLAYER);
}
