#pragma once

// The rules of a lobby on a platform's own multiplayer (a Steam lobby, a Game Center match),
// without the online services: checks, flags, ranked results and removals, decided by every
// player's game from the same messages instead of by the service. Each game sends its messages to
// everyone, itself included, and every game reaches the same outcome, so no single player decides
// for the others. The same limits and rules as the service's (online/src/lobby.ts and
// src/ratings.ts), so a game behaves the same either way. Members are the platform's player IDs.
// Plain C++ (tests/run.sh builds it without Godot); PurrLobbyReferee is Godot's side.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class LobbyRules {
public:
	struct Event {
		enum Type {
			CHECK_COMPLETED, // name, agreed, members (those who disagreed with a majority)
			PLAYER_FLAGGED, // members[0] (the flagged player), reason
			RESULT_AGREED, // members (the ranking, first to last), changes (each one's rating change)
			MEMBER_KICKED, // members[0]
		};
		Type type;
		std::string name;
		bool agreed = false;
		std::vector<std::string> members;
		std::vector<double> changes;
		std::string reason;
	};

	static constexpr size_t MAX_CHECKS = 256;
	static constexpr size_t MAX_CHECK_VALUE = 128;
	static constexpr uint64_t FLAG_WINDOW_MSEC = 10000;
	static constexpr double START_RATING = 1000;

	void reset();
	void add_member(const std::string &p_member);
	std::vector<Event> remove_member(const std::string &p_member);
	bool has_member(const std::string &p_member) const { return members.count(p_member) > 0; }
	void set_host(const std::string &p_member) { host = p_member; }
	const std::string &get_host() const { return host; }
	void set_ranked(bool p_ranked) { ranked = p_ranked; }
	bool is_ranked() const { return ranked; }
	bool is_kicked(const std::string &p_member) const { return kicked.count(p_member) > 0; }

	// Messages from a member (themselves included), as the platform delivered them: only the
	// sender's own claims count, so nobody can speak for another player.
	std::vector<Event> check(const std::string &p_from, const std::string &p_name, const std::string &p_value);
	std::vector<Event> flag(const std::string &p_from, const std::string &p_target, const std::string &p_reason, uint64_t p_now_msec);
	std::vector<Event> result(const std::string &p_from, const std::vector<std::string> &p_ranking);
	void rating(const std::string &p_from, double p_rating);
	std::vector<Event> kick(const std::string &p_from, const std::string &p_member);

	static bool is_valid_check_name(const std::string &p_name);
	static bool is_valid_flag_reason(const std::string &p_reason);
	// Elo, as the online service works it out: each pair of players counts as a game between the
	// two, and each player's change is the average over the players they met.
	static std::vector<double> elo_changes(const std::vector<double> &p_ratings);

private:
	struct Flag {
		std::string target;
		std::string reason;
		std::string by;
		uint64_t at = 0;
	};

	std::map<std::string, double> members; // Member -> their rating in this lobby's mode.
	std::string host;
	bool ranked = false;
	std::map<std::string, std::map<std::string, std::string>> checks; // Name -> member -> value.
	std::set<std::string> settled_checks;
	std::vector<Flag> flags;
	std::set<std::string> confirmed_flags; // "<target> <reason>"
	std::map<std::string, std::vector<std::string>> results; // Member -> their ranking.
	bool contested = false; // A check disagreed or a flag was confirmed: the result doesn't count.
	bool recorded = false;
	std::set<std::string> kicked;

	bool counts(const std::string &p_from) const { return members.count(p_from) > 0 && kicked.count(p_from) == 0; }
	std::vector<Event> settle_checks();
};
