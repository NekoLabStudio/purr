// The rules of lobbies without the online services: three players' games side by side, each
// message going to all of them, checked to reach the same outcomes as the online service would.

#include "lobby_rules.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using Event = LobbyRules::Event;
using Members = std::vector<std::string>;

int failures = 0;

void check(const char *p_what, bool p_ok) {
	printf("check: %s%s\n", p_what, p_ok ? "" : "  FAILED");
	if (!p_ok) {
		failures++;
	}
}

// A lobby: every player's game, and what each one decided, in order.
struct Lobby {
	std::map<std::string, LobbyRules> games;
	std::map<std::string, std::vector<Event>> events;

	explicit Lobby(const Members &p_members = { "a", "b", "c" }, bool p_ranked = false) {
		for (const std::string &own : p_members) {
			LobbyRules &rules = games[own];
			for (const std::string &member : p_members) {
				rules.add_member(member);
			}
			rules.set_host(p_members[0]);
			rules.set_ranked(p_ranked);
			events[own];
		}
	}

	template <typename Message>
	void send(Message p_message) {
		for (auto &game : games) {
			const std::vector<Event> decided = p_message(game.second);
			events[game.first].insert(events[game.first].end(), decided.begin(), decided.end());
		}
	}

	void leave(const std::string &p_member) {
		games.erase(p_member);
		events.erase(p_member);
		send([&](LobbyRules &rules) { return rules.remove_member(p_member); });
	}

	// What every game decided, if they all decided the same; nullptr if they didn't.
	const std::vector<Event> *same_everywhere() const {
		const std::vector<Event> *first = nullptr;
		for (const auto &entry : events) {
			if (!first) {
				first = &entry.second;
				continue;
			}
			if (entry.second.size() != first->size()) {
				return nullptr;
			}
			for (size_t i = 0; i < first->size(); i++) {
				const Event &a = (*first)[i];
				const Event &b = entry.second[i];
				if (a.type != b.type || a.name != b.name || a.agreed != b.agreed || a.members != b.members || a.changes != b.changes || a.reason != b.reason) {
					return nullptr;
				}
			}
		}
		return first;
	}

	size_t decided() const {
		const std::vector<Event> *events_here = same_everywhere();
		return events_here ? events_here->size() : (size_t)-1;
	}
};

auto check_message(const std::string &p_from, const std::string &p_name, const std::string &p_value) {
	return [=](LobbyRules &rules) { return rules.check(p_from, p_name, p_value); };
}

auto flag_message(const std::string &p_from, const std::string &p_target, const std::string &p_reason, uint64_t p_now) {
	return [=](LobbyRules &rules) { return rules.flag(p_from, p_target, p_reason, p_now); };
}

auto result_message(const std::string &p_from, const Members &p_ranking) {
	return [=](LobbyRules &rules) { return rules.result(p_from, p_ranking); };
}

auto rating_message(const std::string &p_from, double p_rating) {
	return [=](LobbyRules &rules) {
		rules.rating(p_from, p_rating);
		return std::vector<Event>();
	};
}

auto kick_message(const std::string &p_from, const std::string &p_member) {
	return [=](LobbyRules &rules) { return rules.kick(p_from, p_member); };
}

bool is_check(const std::vector<Event> *p_events, size_t p_index, const std::string &p_name, bool p_agreed, const Members &p_disagreeing) {
	if (!p_events || p_events->size() <= p_index) {
		return false;
	}
	const Event &event = (*p_events)[p_index];
	return event.type == Event::CHECK_COMPLETED && event.name == p_name && event.agreed == p_agreed && event.members == p_disagreeing;
}

} // namespace

int main() {
	// Checks.
	{
		Lobby lobby;
		for (const char *member : { "a", "b", "c" }) {
			lobby.send(check_message(member, "lap_1", "same"));
		}
		check("a check everyone agrees on settles as agreed", lobby.decided() == 1 && is_check(lobby.same_everywhere(), 0, "lap_1", true, {}));
	}
	{
		Lobby lobby;
		lobby.send(check_message("a", "lap_1", "x"));
		lobby.send(check_message("b", "lap_1", "x"));
		check("a check doesn't settle before everyone answers", lobby.decided() == 0);
		lobby.send(check_message("c", "lap_1", "y"));
		check("the player who disagrees with the majority is named, on every game", lobby.decided() == 1 && is_check(lobby.same_everywhere(), 0, "lap_1", false, { "c" }));
		lobby.send(check_message("c", "lap_1", "x"));
		check("each name settles once", lobby.decided() == 1);
	}
	{
		Lobby lobby({ "a", "b" });
		lobby.send(check_message("a", "score", "1"));
		lobby.send(check_message("b", "score", "2"));
		check("with no majority, nobody is named", lobby.decided() == 1 && is_check(lobby.same_everywhere(), 0, "score", false, {}));
	}
	{
		Lobby lobby;
		lobby.send(check_message("a", "end", "x"));
		lobby.send(check_message("b", "end", "x"));
		lobby.leave("c");
		check("a player leaving settles what everyone else answered", lobby.decided() == 1 && is_check(lobby.same_everywhere(), 0, "end", true, {}));
	}
	{
		Lobby lobby;
		for (const char *member : { "a", "b", "c" }) {
			lobby.send(check_message(member, "not a name!", "x"));
		}
		lobby.send(check_message("z", "lap_1", "x"));
		check("a check with a bad name, or from someone not here, is ignored", lobby.decided() == 0);
	}

	// Flags.
	{
		Lobby lobby;
		lobby.send(flag_message("a", "c", "teleport", 1000));
		check("one player can't get another flagged alone", lobby.decided() == 0);
		lobby.send(flag_message("b", "c", "teleport", 2000));
		const std::vector<Event> *events = lobby.same_everywhere();
		check("two of the other players flagging the same thing confirms it everywhere", events && events->size() == 1 && (*events)[0].type == Event::PLAYER_FLAGGED && (*events)[0].members == Members{ "c" } && (*events)[0].reason == "teleport");
	}
	{
		Lobby lobby;
		lobby.send(flag_message("a", "c", "teleport", 1000));
		lobby.send(flag_message("b", "c", "teleport", 12000));
		check("flags more than 10 seconds apart don't add up", lobby.decided() == 0);
		lobby.send(flag_message("c", "c", "teleport", 12000));
		check("a player can't flag themselves", lobby.decided() == 0);
	}
	{
		Lobby lobby({ "a", "b" });
		lobby.send(flag_message("a", "b", "speed", 0));
		check("a lobby of two never confirms a flag", lobby.decided() == 0);
	}

	// Ranked results.
	const Members ranking = { "b", "a", "c" };
	{
		Lobby lobby;
		lobby.send(result_message("a", ranking));
		lobby.send(result_message("b", ranking));
		check("results don't count outside a matchmade lobby", lobby.decided() == 0);
	}
	{
		Lobby lobby({ "a", "b", "c" }, true);
		lobby.send(rating_message("a", 1200));
		lobby.send(rating_message("b", 1000));
		lobby.send(rating_message("c", 1000));
		lobby.send(result_message("a", ranking));
		check("one player's result alone doesn't count", lobby.decided() == 0);
		lobby.send(result_message("c", { "b", "a" }));
		check("a result that leaves someone out doesn't count", lobby.decided() == 0);
		lobby.send(result_message("b", ranking));
		const std::vector<Event> *events = lobby.same_everywhere();
		check("more than half agreeing records it, the same everywhere", events && events->size() == 1 && (*events)[0].type == Event::RESULT_AGREED && (*events)[0].members == ranking);
		check("with the ratings each player sent", events && events->size() == 1 && (*events)[0].changes == LobbyRules::elo_changes({ 1000, 1200, 1000 }));
		lobby.send(result_message("c", ranking));
		check("a lobby's result counts once", lobby.decided() == 1);
	}
	{
		Lobby lobby({ "a", "b", "c" }, true);
		lobby.send(check_message("a", "lap_1", "x"));
		lobby.send(check_message("b", "lap_1", "x"));
		lobby.send(check_message("c", "lap_1", "y"));
		for (const char *member : { "a", "b", "c" }) {
			lobby.send(result_message(member, ranking));
		}
		check("a failed check stops the result counting", lobby.decided() == 1);
	}
	{
		const std::vector<double> even = LobbyRules::elo_changes({ 1000, 1000 });
		check("elo matches the online service's (16 points between equals)", std::fabs(even[0] - 16) < 1e-9 && std::fabs(even[1] + 16) < 1e-9);
		const std::vector<double> four = LobbyRules::elo_changes({ 1000, 1100, 900, 1000 });
		double total = 0;
		for (const double change : four) {
			total += change;
		}
		check("ratings only move between players (the changes add up to nothing)", std::fabs(total) < 1e-9);
	}

	// Removals.
	{
		Lobby lobby;
		lobby.send(kick_message("b", "c"));
		check("only the host can remove a player", lobby.decided() == 0);
		lobby.send(kick_message("a", "c"));
		const std::vector<Event> *events = lobby.same_everywhere();
		check("the host's removal reaches everyone", events && events->size() == 1 && (*events)[0].type == Event::MEMBER_KICKED && (*events)[0].members == Members{ "c" });
		check("the removed player's game knows it was removed", lobby.games["c"].is_kicked("c"));
		for (const char *member : { "c", "a", "b" }) {
			lobby.send(check_message(member, "after", "x"));
		}
		check("a removed player's messages no longer count", lobby.decided() == 1);
		lobby.send(kick_message("a", "a"));
		check("the host can't remove itself", lobby.decided() == 1);
	}

	printf("lobby rules test: %s\n", failures == 0 ? "all checks passed" : "some checks failed");
	return failures == 0 ? 0 : 1;
}
