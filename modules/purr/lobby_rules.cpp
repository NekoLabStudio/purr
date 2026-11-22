#include "lobby_rules.h"

#include <algorithm>
#include <cmath>

namespace {

// The online service's elo constant (Trill's src/ratings.ts).
const double K = 32;

bool valid_identifier(const std::string &p_text, size_t p_max_length) {
	if (p_text.empty() || p_text.size() > p_max_length) {
		return false;
	}
	for (const char c : p_text) {
		const bool alphanumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (!alphanumeric && c != '_' && c != '.' && c != ':' && c != '-') {
			return false;
		}
	}
	return true;
}

} // namespace

bool LobbyRules::is_valid_check_name(const std::string &p_name) {
	return valid_identifier(p_name, 64);
}

bool LobbyRules::is_valid_flag_reason(const std::string &p_reason) {
	return valid_identifier(p_reason, 32);
}

void LobbyRules::reset() {
	*this = LobbyRules();
}

void LobbyRules::add_member(const std::string &p_member) {
	if (!p_member.empty() && members.count(p_member) == 0) {
		members[p_member] = START_RATING;
	}
}

std::vector<LobbyRules::Event> LobbyRules::remove_member(const std::string &p_member) {
	if (members.erase(p_member) == 0) {
		return {};
	}
	// The checks everyone still here has answered settle without the one who left.
	return settle_checks();
}

// A check settles once everyone still here has answered it: agreed when every value matched,
// and the players who differ from a majority named.
std::vector<LobbyRules::Event> LobbyRules::check(const std::string &p_from, const std::string &p_name, const std::string &p_value) {
	if (!counts(p_from) || !is_valid_check_name(p_name) || p_value.empty() || p_value.size() > MAX_CHECK_VALUE || settled_checks.count(p_name) > 0) {
		return {};
	}
	if (checks.count(p_name) == 0 && checks.size() + settled_checks.size() >= MAX_CHECKS) {
		return {};
	}
	std::map<std::string, std::string> &values = checks[p_name];
	if (values.count(p_from) > 0) {
		return {};
	}
	values[p_from] = p_value;
	return settle_checks();
}

std::vector<LobbyRules::Event> LobbyRules::settle_checks() {
	std::vector<Event> events;
	if (members.size() < 2) {
		return events;
	}
	for (auto check = checks.begin(); check != checks.end();) {
		const std::map<std::string, std::string> &values = check->second;
		const bool everyone = std::all_of(members.begin(), members.end(), [&](const auto &member) { return values.count(member.first) > 0; });
		if (!everyone) {
			++check;
			continue;
		}
		std::map<std::string, int> tally;
		for (const auto &member : members) {
			tally[values.at(member.first)] += 1;
		}
		// The most common value; a tie goes to the smaller value, so every game picks the same.
		std::string top;
		int top_count = 0;
		for (const auto &entry : tally) {
			if (entry.second > top_count) {
				top = entry.first;
				top_count = entry.second;
			}
		}
		Event event;
		event.type = Event::CHECK_COMPLETED;
		event.name = check->first;
		event.agreed = tally.size() == 1;
		if (top_count * 2 > (int)members.size()) {
			for (const auto &member : members) {
				if (values.at(member.first) != top) {
					event.members.push_back(member.first);
				}
			}
		}
		if (!event.agreed) {
			contested = true;
		}
		events.push_back(event);
		settled_checks.insert(check->first);
		check = checks.erase(check);
	}
	return events;
}

// Confirmed when more than half of the other players, and at least two, flag the same player for
// the same reason within the window; then the match is contested, as after a failed check.
std::vector<LobbyRules::Event> LobbyRules::flag(const std::string &p_from, const std::string &p_target, const std::string &p_reason, uint64_t p_now_msec) {
	if (!counts(p_from) || members.count(p_target) == 0 || p_target == p_from || !is_valid_flag_reason(p_reason)) {
		return {};
	}
	const std::string key = p_target + " " + p_reason;
	if (confirmed_flags.count(key) > 0) {
		return {};
	}
	flags.erase(std::remove_if(flags.begin(), flags.end(), [&](const Flag &f) { return p_now_msec - f.at > FLAG_WINDOW_MSEC; }), flags.end());
	const bool already = std::any_of(flags.begin(), flags.end(), [&](const Flag &f) { return f.target == p_target && f.reason == p_reason && f.by == p_from; });
	if (!already) {
		flags.push_back({ p_target, p_reason, p_from, p_now_msec });
	}
	std::set<std::string> flaggers;
	for (const Flag &f : flags) {
		if (f.target == p_target && f.reason == p_reason && members.count(f.by) > 0) {
			flaggers.insert(f.by);
		}
	}
	const int others = (int)members.size() - 1;
	if ((int)flaggers.size() < std::max(2, others / 2 + 1)) {
		return {};
	}
	confirmed_flags.insert(key);
	contested = true;
	Event event;
	event.type = Event::PLAYER_FLAGGED;
	event.members.push_back(p_target);
	event.reason = p_reason;
	return { event };
}

// Recorded once, when more than half of the players sent the same ranking of everyone here.
std::vector<LobbyRules::Event> LobbyRules::result(const std::string &p_from, const std::vector<std::string> &p_ranking) {
	if (!counts(p_from) || !ranked || recorded || contested) {
		return {};
	}
	const std::set<std::string> unique(p_ranking.begin(), p_ranking.end());
	const bool everyone_here = std::all_of(unique.begin(), unique.end(), [&](const std::string &member) { return members.count(member) > 0; });
	if (!everyone_here || unique.size() != p_ranking.size() || unique.size() < 2 || unique.size() != members.size()) {
		return {};
	}
	results[p_from] = p_ranking;
	int agreeing = 0;
	for (const auto &entry : results) {
		if (members.count(entry.first) > 0 && entry.second == p_ranking) {
			agreeing++;
		}
	}
	if (agreeing * 2 <= (int)members.size()) {
		return {};
	}
	recorded = true;
	std::vector<double> ratings;
	for (const std::string &member : p_ranking) {
		ratings.push_back(members.at(member));
	}
	Event event;
	event.type = Event::RESULT_AGREED;
	event.members = p_ranking;
	event.changes = elo_changes(ratings);
	return { event };
}

void LobbyRules::rating(const std::string &p_from, double p_rating) {
	if (counts(p_from) && std::isfinite(p_rating) && p_rating > 0 && p_rating < 100000) {
		members[p_from] = p_rating;
	}
}

// Only the host's removals count; a removed player's messages no longer do.
std::vector<LobbyRules::Event> LobbyRules::kick(const std::string &p_from, const std::string &p_member) {
	if (!counts(p_from) || p_from != host || p_member.empty() || p_member == host || kicked.count(p_member) > 0) {
		return {};
	}
	kicked.insert(p_member);
	Event event;
	event.type = Event::MEMBER_KICKED;
	event.members.push_back(p_member);
	return { event };
}

std::vector<double> LobbyRules::elo_changes(const std::vector<double> &p_ratings) {
	std::vector<double> changes(p_ratings.size(), 0.0);
	const int others = (int)p_ratings.size() - 1;
	if (others < 1) {
		return changes;
	}
	for (size_t i = 0; i < p_ratings.size(); i++) {
		for (size_t j = i + 1; j < p_ratings.size(); j++) {
			const double expected = 1.0 / (1.0 + std::pow(10.0, (p_ratings[j] - p_ratings[i]) / 400.0));
			const double change = K * (1.0 - expected) / others;
			changes[i] += change;
			changes[j] -= change;
		}
	}
	return changes;
}
