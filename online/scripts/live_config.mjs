#!/usr/bin/env node
// Sets the live config games read (the engine's LiveConfig), on this instance's database: values
// players' games pick up within 15 minutes (or when they next start), each optionally only for a
// while, which games switch to and from on time.
//
//   npm run live-config                                        every game's values
//   npm run live-config -- set <game> <key> <value>            a value from now on
//   npm run live-config -- set <game> <key> <value> --from=2026-12-24T18:00 --until=2026-12-27
//   npm run live-config -- remove <game> <key>                 every value of the key
//   npm run live-config -- remove <game> <key> <id>            one of them
//
// A value is JSON (true, 1.5, "text", [1, 2], {"a": 1}); anything else is taken as text. Times
// are UTC. Add --local to work on the local development database instead (wrangler dev's).

import { args, positional, quote, requireInstanceFolder, say, sql, start } from "./cli.mjs";

const NAME = /^[a-z0-9_-]{1,64}$/;
const KEY = /^[A-Za-z0-9_./-]{1,64}$/;
const ID = /^[0-9a-f]{8}$/;
const MAX_VALUE = 4096;

function checked(kind, pattern, text) {
	if (!pattern.test(text ?? "")) {
		throw new Error(`"${text ?? ""}" isn't a ${kind}.`);
	}
	return text;
}

const date = (seconds) => new Date(seconds * 1000).toISOString().slice(0, 16).replace("T", " ");

// --from=... or --until=..., as Unix time: an ISO date and time, taken as UTC.
function timeOption(name) {
	const option = args.find((arg) => arg.startsWith(`--${name}=`));
	if (!option) {
		return null;
	}
	const text = option.slice(name.length + 3).trim();
	const time = Date.parse(/[zZ]|[+-]\d\d:?\d\d$/.test(text) ? text : `${text}Z`);
	if (!/^\d{4}-\d{2}-\d{2}/.test(text) || Number.isNaN(time)) {
		throw new Error(`--${name} takes a date and time in UTC, like 2026-12-24T18:00.`);
	}
	return Math.floor(time / 1000);
}

function list() {
	const rows = sql("SELECT id, game, key, value, starts_at, ends_at FROM live_config ORDER BY game, key, starts_at")[0];
	if (rows.length === 0) {
		say("No live config values.");
		return;
	}
	let game = "";
	for (const row of rows) {
		if (row.game !== game) {
			game = row.game;
			say(`${game}:`);
		}
		const from = row.starts_at ? ` from ${date(row.starts_at)}` : "";
		const until = row.ends_at ? ` until ${date(row.ends_at)}` : "";
		say(`  ${row.key} = ${row.value}${from}${until}  (${row.id.slice(0, 8)})`);
	}
}

function set(game, key, text) {
	if (text === undefined) {
		throw new Error("Give the value too.");
	}
	let value;
	try {
		value = JSON.stringify(JSON.parse(text));
	} catch {
		value = JSON.stringify(text);
	}
	if (value.length > MAX_VALUE) {
		throw new Error(`A value is at most ${MAX_VALUE} characters of JSON.`);
	}
	const startsAt = timeOption("from");
	const endsAt = timeOption("until");
	if (startsAt !== null && endsAt !== null && endsAt <= startsAt) {
		throw new Error("--until must be after --from.");
	}
	const id = crypto.randomUUID().replaceAll("-", "");
	const time = Math.floor(Date.now() / 1000);
	// A value with no dates replaces the key's other undated one; scheduled ones are added beside it.
	const replace = startsAt === null && endsAt === null ? `DELETE FROM live_config WHERE game = ${quote(game)} AND key = ${quote(key)} AND starts_at IS NULL AND ends_at IS NULL; ` : "";
	sql(
		`${replace}INSERT INTO live_config (id, game, key, value, starts_at, ends_at, updated_at)
		 VALUES (${quote(id)}, ${quote(game)}, ${quote(key)}, ${quote(value)}, ${startsAt ?? "NULL"}, ${endsAt ?? "NULL"}, ${time})`,
	);
	const when = `${startsAt ? ` from ${date(startsAt)}` : ""}${endsAt ? ` until ${date(endsAt)}` : ""}`;
	say(`${game}: ${key} = ${value}${when}. Games pick it up within 15 minutes.`);
}

function remove(game, key, id) {
	const one = id ? ` AND substr(id, 1, 8) = ${quote(checked("value ID", ID, id))}` : "";
	sql(`DELETE FROM live_config WHERE game = ${quote(game)} AND key = ${quote(key)}${one}`);
	say(id ? "Removed." : `Every value of ${key} is removed: games go back to their own default.`);
}

async function main() {
	requireInstanceFolder();
	const [command, game, key, value] = positional;
	if (!command) {
		list();
	} else if (command === "set") {
		set(checked("game", NAME, game), checked("key", KEY, key), value);
	} else if (command === "remove") {
		remove(checked("game", NAME, game), checked("key", KEY, key), value);
	} else {
		throw new Error(`There's no "${command}". Use set or remove, or nothing to list the values.`);
	}
}

start(main);
