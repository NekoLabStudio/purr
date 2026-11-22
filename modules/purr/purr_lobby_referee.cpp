#include "purr_lobby_referee.h"

#include "core/object/class_db.h"

namespace {

std::string std_string(const String &p_string) {
	return std::string(p_string.utf8().get_data());
}

String godot_string(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

} // namespace

void PurrLobbyReferee::reset() {
	rules.reset();
	peer_ids.clear();
}

void PurrLobbyReferee::add_member(const String &p_member, int64_t p_peer_id) {
	ERR_FAIL_COND(p_member.is_empty());
	rules.add_member(std_string(p_member));
	if (p_peer_id != 0 || !peer_ids.has(p_member)) {
		peer_ids[p_member] = p_peer_id;
	}
}

void PurrLobbyReferee::remove_member(const String &p_member) {
	const std::vector<LobbyRules::Event> events = rules.remove_member(std_string(p_member));
	emit_events(events);
	peer_ids.erase(p_member);
}

void PurrLobbyReferee::set_peer_id(const String &p_member, int64_t p_peer_id) {
	if (peer_ids.has(p_member)) {
		peer_ids[p_member] = p_peer_id;
	}
}

String PurrLobbyReferee::get_member(int64_t p_peer_id) const {
	for (const KeyValue<String, int64_t> &entry : peer_ids) {
		if (entry.value == p_peer_id && p_peer_id != 0) {
			return entry.key;
		}
	}
	return String();
}

int64_t PurrLobbyReferee::get_peer_id(const String &p_member) const {
	const HashMap<String, int64_t>::ConstIterator entry = peer_ids.find(p_member);
	return entry ? entry->value : 0;
}

void PurrLobbyReferee::set_host(const String &p_member) {
	rules.set_host(std_string(p_member));
}

String PurrLobbyReferee::get_host() const {
	return godot_string(rules.get_host());
}

bool PurrLobbyReferee::is_kicked(const String &p_member) const {
	return rules.is_kicked(std_string(p_member));
}

void PurrLobbyReferee::receive(const String &p_from, const Dictionary &p_message, uint64_t p_now_msec) {
	const std::string from = std_string(p_from);
	const String type = p_message.get("t", "");
	if (type == "check") {
		emit_events(rules.check(from, std_string(p_message.get("name", "")), std_string(p_message.get("value", ""))));
	} else if (type == "flag") {
		emit_events(rules.flag(from, std_string(p_message.get("target", "")), std_string(p_message.get("reason", "")), p_now_msec));
	} else if (type == "result" && p_message.get("ranking", Variant()).get_type() == Variant::ARRAY) {
		std::vector<std::string> ranking;
		for (const Variant &member : Array(p_message["ranking"])) {
			if (member.get_type() != Variant::STRING) {
				return;
			}
			ranking.push_back(std_string(member));
		}
		emit_events(rules.result(from, ranking));
	} else if (type == "rating" && p_message.get("rating", Variant()).get_type() == Variant::FLOAT) {
		rules.rating(from, p_message["rating"]);
	} else if (type == "kick") {
		emit_events(rules.kick(from, std_string(p_message.get("member", ""))));
	}
}

void PurrLobbyReferee::emit_events(const std::vector<LobbyRules::Event> &p_events) {
	for (const LobbyRules::Event &event : p_events) {
		switch (event.type) {
			case LobbyRules::Event::CHECK_COMPLETED: {
				Array disagreeing;
				for (const std::string &member : event.members) {
					disagreeing.push_back(get_peer_id(godot_string(member)));
				}
				emit_signal(SNAME("check_completed"), godot_string(event.name), event.agreed, disagreeing);
			} break;
			case LobbyRules::Event::PLAYER_FLAGGED:
				emit_signal(SNAME("player_flagged"), get_peer_id(godot_string(event.members[0])), godot_string(event.reason));
				break;
			case LobbyRules::Event::RESULT_AGREED: {
				Array ranking;
				PackedFloat64Array changes;
				for (size_t i = 0; i < event.members.size(); i++) {
					ranking.push_back(godot_string(event.members[i]));
					changes.push_back(event.changes[i]);
				}
				emit_signal(SNAME("result_agreed"), ranking, changes);
			} break;
			case LobbyRules::Event::MEMBER_KICKED:
				emit_signal(SNAME("member_kicked"), godot_string(event.members[0]));
				break;
		}
	}
}

Dictionary PurrLobbyReferee::make_check(const String &p_name, const String &p_value) {
	Dictionary message;
	message["t"] = "check";
	message["name"] = p_name;
	message["value"] = p_value;
	return message;
}

Dictionary PurrLobbyReferee::make_flag(const String &p_target, const String &p_reason) {
	Dictionary message;
	message["t"] = "flag";
	message["target"] = p_target;
	message["reason"] = p_reason;
	return message;
}

Dictionary PurrLobbyReferee::make_result(const PackedStringArray &p_ranking) {
	Dictionary message;
	message["t"] = "result";
	Array ranking;
	for (const String &member : p_ranking) {
		ranking.push_back(member);
	}
	message["ranking"] = ranking;
	return message;
}

Dictionary PurrLobbyReferee::make_rating(double p_rating) {
	Dictionary message;
	message["t"] = "rating";
	message["rating"] = p_rating;
	return message;
}

Dictionary PurrLobbyReferee::make_kick(const String &p_member) {
	Dictionary message;
	message["t"] = "kick";
	message["member"] = p_member;
	return message;
}

bool PurrLobbyReferee::is_valid_check_name(const String &p_name) {
	return LobbyRules::is_valid_check_name(std_string(p_name));
}

bool PurrLobbyReferee::is_valid_flag_reason(const String &p_reason) {
	return LobbyRules::is_valid_flag_reason(std_string(p_reason));
}

void PurrLobbyReferee::_bind_methods() {
	ADD_SIGNAL(MethodInfo("check_completed", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::BOOL, "agreed"), PropertyInfo(Variant::ARRAY, "disagreeing")));
	ADD_SIGNAL(MethodInfo("player_flagged", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::STRING, "reason")));
	ADD_SIGNAL(MethodInfo("result_agreed", PropertyInfo(Variant::ARRAY, "ranking"), PropertyInfo(Variant::PACKED_FLOAT64_ARRAY, "changes")));
	ADD_SIGNAL(MethodInfo("member_kicked", PropertyInfo(Variant::STRING, "member")));
}
