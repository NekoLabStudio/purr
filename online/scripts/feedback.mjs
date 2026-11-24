#!/usr/bin/env node
// Reads players' feedback, on this instance's database: lists the last 90 days', newest first,
// shows one in full with what the game ran on, and deletes it once dealt with.
//
//   npm run feedback                          the feedback, to read one by one
//   npm run feedback -- category <category>   only that category's
//   npm run feedback -- show <feedback>       one in full (its ID is in the list)
//   npm run feedback -- delete <feedback>
//
// Add --local to work on the local development database instead (wrangler dev's).

import { ask, positional, quote, requireInstanceFolder, say, sql, start } from "./cli.mjs";

const MAX_LISTED = 50;
const FEEDBACK_ID = /^[0-9a-f]{8}$/;
const CATEGORY = /^[a-z0-9_-]{1,64}$/;
const LISTED_TEXT = 100;

function feedbackId(text) {
	const id = (text ?? "").trim().toLowerCase();
	if (!FEEDBACK_ID.test(id)) {
		throw new Error(`"${text ?? ""}" isn't a feedback ID (8 letters and digits, from the list).`);
	}
	return id;
}

const date = (seconds) => new Date(seconds * 1000).toISOString().slice(0, 16).replace("T", " ");
const shortId = (id) => id.slice(0, 8);
const from = (row) => (row.account_id ? `${row.display_name || "(no name)"} (${row.account_id})` : "anonymous");

function list(category) {
	const filter = category ? `WHERE category = ${quote(category)}` : "";
	const rows = sql(`SELECT id, game, category, text, version, account_id, display_name, created_at FROM feedback ${filter} ORDER BY created_at DESC LIMIT ${MAX_LISTED}`)[0];
	if (rows.length === 0) {
		say(category ? `No feedback in ${category}.` : "No feedback yet.");
		return rows;
	}
	rows.forEach((row, index) => {
		const words = row.text.replace(/\s+/g, " ");
		say(`${String(index + 1).padStart(3)}. ${words.length > LISTED_TEXT ? `${words.slice(0, LISTED_TEXT)}...` : words}`);
		say(`     ${row.game}, ${row.category}${row.version ? `, version ${row.version}` : ""}; ${date(row.created_at)}; ${from(row)}; ${shortId(row.id)}`);
	});
	return rows;
}

function findFeedback(id) {
	const [row] = sql(`SELECT * FROM feedback WHERE substr(id, 1, 8) = ${quote(id)}`)[0];
	if (!row) {
		throw new Error(`There's no feedback ${id}.`);
	}
	return row;
}

function show(id) {
	const row = findFeedback(id);
	say(`${row.game}, ${row.category}${row.version ? `, version ${row.version}` : ""}, sent ${date(row.created_at)} by ${from(row)}:`);
	say();
	say(row.text.trimEnd());
	if (row.details) {
		say();
		say(row.details.trimEnd());
	}
}

function remove(id) {
	const row = findFeedback(id);
	sql(`DELETE FROM feedback WHERE id = ${quote(row.id)}`);
	say("Deleted.");
}

function review(category) {
	for (;;) {
		const rows = list(category);
		if (rows.length === 0) {
			return;
		}
		say();
		const choice = ask("feedback", "Read which? (number or ID, empty to finish)");
		if (!choice) {
			return;
		}
		try {
			const id = feedbackId(/^\d+$/.test(choice) && Number(choice) <= rows.length ? shortId(rows[Number(choice) - 1].id) : choice);
			say();
			show(id);
			say();
			if (ask("action", "delete, or empty to go back").toLowerCase() === "delete") {
				remove(id);
			}
		} catch (error) {
			say(error.message);
		}
		say();
	}
}

async function main() {
	requireInstanceFolder();
	const [command, target] = positional;
	const actions = {
		show: () => show(feedbackId(target)),
		delete: () => remove(feedbackId(target)),
		category: () => {
			if (!CATEGORY.test(target ?? "")) {
				throw new Error("Give a category, as the game sends it.");
			}
			review(target);
		},
	};
	if (!command) {
		review();
	} else if (actions[command]) {
		actions[command]();
	} else {
		throw new Error(`There's no "${command}". Use show, delete or category, or nothing to go through the feedback.`);
	}
}

start(main);
