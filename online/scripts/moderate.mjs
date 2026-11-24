#!/usr/bin/env node
// Reviews players' reports and acts on them, on this instance's database: lists the players
// reported in the last 90 days, shows what was reported, and bans from multiplayer, lifts a ban,
// resets a name or dismisses the reports. Also looks after leaderboards (hiding a player's scores,
// changing how a board is ordered or resets) and marks test players, whose scores only they see.
//
//   npm run moderate                                 the reported players, to review one by one
//   npm run moderate -- show <account>               one player's reports
//   npm run moderate -- find <name>                  players with that name
//   npm run moderate -- ban <account> <days|forever>
//   npm run moderate -- unban <account>
//   npm run moderate -- rename <account>             clears the name; the player chooses another
//   npm run moderate -- dismiss <account>            deletes the reports about the player
//   npm run moderate -- scores <game> <board>        a leaderboard's best scores, with account IDs
//   npm run moderate -- hide <account> [<game> <board>]    hides the player's scores (all, or one board's)
//   npm run moderate -- unhide <account> [<game> <board>]
//   npm run moderate -- board <game> <board> <higher|lower> <never|daily|weekly|monthly> [<min>|none] [<max>|none]
//   npm run moderate -- test <account>               a test player: only they see their scores
//   npm run moderate -- untest <account>
//
// Add --local to work on the local development database instead (wrangler dev's).

import { ask, confirm, positional, quote, requireInstanceFolder, say, sql, start } from "./cli.mjs";

// Forever, for a ban with no end: the year 3000 (as the service's own).
const FOREVER = 32_503_680_000;
const DAY = 86_400;
const MAX_LISTED = 50;
const ACCOUNT_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
// Games and leaderboards, as the service names them.
const NAME = /^[a-z0-9_-]{1,64}$/;
const RESETS = ["never", "daily", "weekly", "monthly"];

// A leaderboard's ID, as Purr's Project Settings take it.
const BOARD_ID = /^[A-Za-z0-9_.-]{1,64}$/;

function checkedName(kind, text) {
	if (!(kind === "leaderboard" ? BOARD_ID : NAME).test(text ?? "")) {
		throw new Error(`"${text ?? ""}" isn't a ${kind} ID.`);
	}
	return text;
}

function accountId(text) {
	const id = (text ?? "").trim().toLowerCase();
	if (!ACCOUNT_ID.test(id)) {
		throw new Error(`"${text ?? ""}" isn't an account ID.`);
	}
	return id;
}

const time = () => Math.floor(Date.now() / 1000);
const date = (seconds) => new Date(seconds * 1000).toISOString().slice(0, 16).replace("T", " ");
const plural = (count, word) => `${count} ${word}${count === 1 ? "" : "s"}`;

function banText(bannedUntil) {
	if (bannedUntil <= time()) {
		return "";
	}
	return bannedUntil >= FOREVER ? "banned for good" : `banned until ${date(bannedUntil)}`;
}

function findAccount(id) {
	const [account] = sql(`SELECT id, display_name, name_chosen, banned_until, auto_bans, test, created_at FROM accounts WHERE id = ${quote(id)}`)[0];
	if (!account) {
		throw new Error(`There's no account ${id}.`);
	}
	return account;
}

function listReported() {
	const players = sql(
		`SELECT reports.reported_id AS id, accounts.display_name AS name, accounts.banned_until AS banned_until,
		        COUNT(*) AS reports, COUNT(DISTINCT reports.reporter_id) AS reporters,
		        group_concat(DISTINCT reports.reason) AS reasons, MAX(reports.created_at) AS latest
		 FROM reports JOIN accounts ON accounts.id = reports.reported_id
		 GROUP BY reports.reported_id ORDER BY reporters DESC, latest DESC LIMIT ${MAX_LISTED}`,
	)[0];
	if (players.length === 0) {
		say("No one has been reported.");
		return players;
	}
	players.forEach((player, index) => {
		const ban = banText(player.banned_until);
		say(`${String(index + 1).padStart(3)}. ${player.name || "(no name)"}  ${player.id}`);
		say(`     ${plural(player.reports, "report")} from ${plural(player.reporters, "player")}: ${player.reasons.split(",").join(", ")}; latest ${date(player.latest)}${ban ? `; ${ban}` : ""}`);
	});
	return players;
}

function show(id) {
	const account = findAccount(id);
	const ban = banText(account.banned_until);
	say(`${account.display_name || "(no name)"}  ${account.id}`);
	say(`Account since ${date(account.created_at)}${account.name_chosen ? ", chose their name" : ""}${ban ? `, ${ban}` : ""}${account.auto_bans ? `, ${plural(account.auto_bans, "automatic ban")}` : ""}${account.test ? ", a test player" : ""}`);
	const reports = sql(
		`SELECT reports.created_at, reports.game, reports.reason, reports.details, reports.automatic, reports.actioned,
		        reporters.display_name AS reporter, reports.reporter_id
		 FROM reports JOIN accounts AS reporters ON reporters.id = reports.reporter_id
		 WHERE reports.reported_id = ${quote(id)} ORDER BY reports.created_at DESC LIMIT 100`,
	)[0];
	say(reports.length === 0 ? "No reports." : "");
	for (const report of reports) {
		const by = report.automatic ? "the anti-cheat" : `${report.reporter || "(no name)"} (${report.reporter_id})`;
		say(`${date(report.created_at)}  ${report.game}  ${report.reason}${report.actioned ? " (acted on automatically)" : ""}, by ${by}`);
		if (report.details) {
			say(`    ${report.details}`);
		}
	}
	const activity = sql(`SELECT at, kind, provider, game, detail FROM account_activity WHERE account_id = ${quote(id)} ORDER BY at DESC, rowid DESC LIMIT 20`)[0];
	if (activity.length > 0) {
		say();
		say("Recent activity:");
		for (const entry of activity) {
			say(`${date(entry.at)}  ${[ACTIVITY[entry.kind] ?? entry.kind, entry.provider, entry.game, entry.detail].filter(Boolean).join(", ")}`);
		}
	}
}

const ACTIVITY = {
	signed_in: "signed in",
	guest_signed_in: "played as a guest",
	email_added: "added an email address",
	email_changed: "changed their email address",
	account_joined: "another account joined this one",
	device_remembered: "remembered on a device",
	device_forgotten: "forgotten on a device",
	name_changed: "chose a name",
};

function ban(id, length) {
	findAccount(id);
	const forever = /^forever$/i.test(length ?? "");
	const days = Number(length);
	if (!forever && !(Number.isInteger(days) && days > 0 && days <= 36_500)) {
		throw new Error('A ban is a number of days, or "forever".');
	}
	const until = forever ? FOREVER : time() + days * DAY;
	sql(`UPDATE accounts SET banned_until = ${until} WHERE id = ${quote(id)}`);
	say(forever ? "Banned from multiplayer for good." : `Banned from multiplayer until ${date(until)}.`);
}

function unban(id) {
	findAccount(id);
	sql(`UPDATE accounts SET banned_until = 0 WHERE id = ${quote(id)}`);
	say("The ban is lifted.");
}

function rename(id) {
	findAccount(id);
	sql(`UPDATE accounts SET display_name = '', name_chosen = 0 WHERE id = ${quote(id)}`);
	say("The name is cleared. The player gets their platform's name back, or chooses a new one.");
}

function dismiss(id) {
	findAccount(id);
	sql(`DELETE FROM reports WHERE reported_id = ${quote(id)}`);
	say("The reports are dismissed.");
}

// A board's best scores in its current period (by the time of the last score), every player's.
function scores(game, board) {
	const [kept] = sql(`SELECT higher_first, reset, min_score, max_score FROM leaderboards WHERE game = ${quote(game)} AND id = ${quote(board)}`)[0];
	if (!kept) {
		throw new Error(`There's no leaderboard ${board} in ${game}.`);
	}
	const rows = sql(
		`SELECT scores.account_id, scores.score, scores.hidden, scores.at, accounts.display_name, accounts.test, accounts.banned_until
		 FROM scores JOIN accounts ON accounts.id = scores.account_id
		 WHERE scores.game = ${quote(game)} AND scores.board = ${quote(board)}
		   AND scores.period = (SELECT MAX(period) FROM scores WHERE game = ${quote(game)} AND board = ${quote(board)})
		 ORDER BY scores.score ${kept.higher_first ? "DESC" : "ASC"}, scores.at ASC LIMIT ${MAX_LISTED}`,
	)[0];
	const limits = [kept.min_score !== null ? `lowest ${kept.min_score}` : "", kept.max_score !== null ? `highest ${kept.max_score}` : ""].filter(Boolean).join(", ");
	say(`${kept.higher_first ? "Higher" : "Lower"} scores first; resets ${kept.reset}${limits ? `; takes ${limits}` : ""}.`);
	say(rows.length === 0 ? "No scores." : "");
	rows.forEach((row, index) => {
		const marks = [row.hidden ? "hidden" : "", row.test ? "test player" : "", banText(row.banned_until)].filter(Boolean).join(", ");
		say(`${String(index + 1).padStart(3)}. ${row.score}  ${row.display_name || "(no name)"}  ${row.account_id}${marks ? `  (${marks})` : ""}`);
	});
}

function hide(id, game, board, hidden) {
	findAccount(id);
	const where = game || board ? ` AND game = ${quote(checkedName("game", game))} AND board = ${quote(checkedName("leaderboard", board))}` : "";
	sql(`UPDATE scores SET hidden = ${hidden ? 1 : 0} WHERE account_id = ${quote(id)}${where}`);
	say(hidden ? "Their scores are hidden: only they see them." : "Their scores are shown again.");
}

// A board's lowest or highest score: a whole number, or "none" (or nothing) for no limit.
function limit(text) {
	if (text === undefined || /^none$/i.test(text)) {
		return "NULL";
	}
	if (!/^-?\d{1,15}$/.test(text)) {
		throw new Error(`"${text}" isn't a whole number, or "none".`);
	}
	return text;
}

function setBoard(game, board, order, reset, min, max) {
	if (order !== "higher" && order !== "lower") {
		throw new Error('A board\'s order is "higher" or "lower" (which scores come first).');
	}
	if (!RESETS.includes(reset)) {
		throw new Error(`A board resets ${RESETS.join(", ")}.`);
	}
	const [kept] = sql(`SELECT reset FROM leaderboards WHERE game = ${quote(game)} AND id = ${quote(board)}`)[0];
	if (!kept) {
		throw new Error(`There's no leaderboard ${board} in ${game}: it's made by its first score.`);
	}
	sql(`UPDATE leaderboards SET higher_first = ${order === "higher" ? 1 : 0}, reset = ${quote(reset)}, min_score = ${limit(min)}, max_score = ${limit(max)} WHERE game = ${quote(game)} AND id = ${quote(board)}`);
	say(kept.reset !== reset ? "Changed. Scores so far stay in the period they were made in." : "Changed.");
}

function markTest(id, test) {
	findAccount(id);
	sql(`UPDATE accounts SET test = ${test ? 1 : 0} WHERE id = ${quote(id)}`);
	say(test ? "A test player now: only they see their scores, and they don't count in totals." : "No longer a test player.");
}

function find(name) {
	const text = (name ?? "").trim();
	if (!text) {
		throw new Error("Give a name to look for.");
	}
	const pattern = quote(`%${text.replace(/[\\%_]/g, (match) => `\\${match}`)}%`);
	const accounts = sql(`SELECT id, display_name, banned_until FROM accounts WHERE display_name LIKE ${pattern} ESCAPE '\\' ORDER BY display_name LIMIT ${MAX_LISTED}`)[0];
	say(accounts.length === 0 ? "No one has that name." : "");
	for (const account of accounts) {
		const status = banText(account.banned_until);
		say(`${account.display_name}  ${account.id}${status ? `  (${status})` : ""}`);
	}
}

// Going through the reported players one at a time.
function review() {
	for (;;) {
		const players = listReported();
		if (players.length === 0) {
			return;
		}
		say();
		const choice = ask("player", "Review which player? (number or account ID, empty to finish)");
		if (!choice) {
			return;
		}
		const id = /^\d+$/.test(choice) ? players[Number(choice) - 1]?.id : choice;
		try {
			reviewOne(accountId(id ?? choice));
		} catch (error) {
			say(error.message);
		}
		say();
	}
}

function reviewOne(id) {
	say();
	show(id);
	say();
	const action = ask("action", "ban, unban, rename, dismiss, or empty to go back").toLowerCase();
	if (action === "ban") {
		ban(id, ask("days", 'For how many days? (or "forever")', "7"));
	} else if (action === "unban") {
		unban(id);
	} else if (action === "rename") {
		rename(id);
	} else if (action === "dismiss") {
		dismiss(id);
		return;
	} else {
		return;
	}
	if (confirm("clear", "Dismiss their reports too?", true)) {
		dismiss(id);
	}
}

async function main() {
	requireInstanceFolder();
	const [command, target, extra] = positional;
	const actions = {
		show: () => show(accountId(target)),
		find: () => find(positional.slice(1).join(" ")),
		ban: () => ban(accountId(target), extra),
		unban: () => unban(accountId(target)),
		rename: () => rename(accountId(target)),
		dismiss: () => dismiss(accountId(target)),
		scores: () => scores(checkedName("game", target), checkedName("leaderboard", extra)),
		hide: () => hide(accountId(target), extra, positional[3], true),
		unhide: () => hide(accountId(target), extra, positional[3], false),
		board: () => setBoard(checkedName("game", target), checkedName("leaderboard", extra), positional[3], positional[4], positional[5], positional[6]),
		test: () => markTest(accountId(target), true),
		untest: () => markTest(accountId(target), false),
	};
	if (!command) {
		review();
	} else if (actions[command]) {
		actions[command]();
	} else {
		throw new Error(`There's no "${command}". Use show, find, ban, unban, rename, dismiss, scores, hide, unhide, board, test or untest, or nothing to review the reported players.`);
	}
}

start(main);
