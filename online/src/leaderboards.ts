// Leaderboards across every platform a game is on (the engine's Leaderboards, with online/url
// set). A board is made by the first score sent to it, with the order and reset that score came
// with (the game's Project Settings), and keeps them after that: a forged client can only make a
// board that doesn't exist yet. Each player keeps their best score in each period; boards that
// reset daily, weekly (from Monday) or monthly start a new period at midnight UTC, and the period
// before the current one is kept, for last week's winners. Scores hidden by the operator, test
// players' and banned players' are seen only by their own player.

import type { Env, GameSettings } from "./env";
import type { Caller } from "./sessions";
import { HttpError } from "./http";
import { identityHash, type Provider } from "./identities";
import { requireOwner } from "./ownership";
import { DAY, now } from "./time";

// A leaderboard's ID, as Purr's Project Settings take it.
const BOARD_ID = /^[A-Za-z0-9_.-]{1,64}$/;

export function checkBoard(id: string): string {
	if (!BOARD_ID.test(id)) {
		throw new HttpError(400, "bad_request", 'A leaderboard ID is 1 to 64 letters, digits, "_", "." or "-".');
	}
	return id;
}

export const RESETS = ["never", "daily", "weekly", "monthly"] as const;
export type Reset = (typeof RESETS)[number];
export const MAX_SCORES = 100;
// Friends a game can name when asking for its player's friends' scores: each is hashed, and the
// free plan gives a request 10 ms of CPU.
export const MAX_FRIENDS = 200;
const FRIEND_PROVIDERS: Provider[] = ["steam", "game_center", "play_games"];

interface Board {
	higher_first: number;
	reset: Reset;
	min_score: number | null;
	max_score: number | null;
}

export interface Entry {
	rank: number;
	name: string;
	score: number;
	player: boolean;
}

// The start of the period a time falls in (UTC), and of the one before.
export function periodStart(reset: Reset, time: number): number {
	switch (reset) {
		case "daily":
			return Math.floor(time / DAY) * DAY;
		case "weekly": {
			// 5 January 1970 was a Monday.
			const monday = 4 * DAY;
			return Math.floor((time - monday) / (7 * DAY)) * 7 * DAY + monday;
		}
		case "monthly": {
			const date = new Date(time * 1000);
			return Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), 1) / 1000;
		}
		default:
			return 0;
	}
}

export function previousPeriodStart(reset: Reset, time: number): number {
	const start = periodStart(reset, time);
	return reset === "never" ? 0 : periodStart(reset, start - 1);
}

function optionalScore(body: Record<string, unknown>, key: string): number | null {
	const value = body[key];
	if (value === undefined || value === null) {
		return null;
	}
	if (!Number.isSafeInteger(value)) {
		throw new HttpError(400, "bad_request", `"${key}" must be a whole number.`);
	}
	return value as number;
}

async function findBoard(env: Env, game: string, board: string): Promise<Board | null> {
	return env.DB.prepare("SELECT higher_first, reset, min_score, max_score FROM leaderboards WHERE game = ? AND id = ?").bind(game, board).first<Board>();
}

// A score for the player: kept if it's their best this period. The body also says how the board
// is ordered and when it resets, which only counts the first time the board sees a score.
export async function submitScore(env: Env, caller: Caller, game: string, settings: GameSettings, boardId: string, body: Record<string, unknown>) {
	const board = checkBoard(boardId);
	await requireOwner(env, game, settings, caller);
	if (env.SCORE_LIMITER) {
		const { success } = await env.SCORE_LIMITER.limit({ key: `account:${caller.accountId}` });
		if (!success) {
			throw new HttpError(429, "rate_limited", "Too many scores; try again in a minute.");
		}
	}
	const score = body.score;
	if (!Number.isSafeInteger(score)) {
		throw new HttpError(400, "bad_request", '"score" must be a whole number.');
	}
	const order = body.order ?? "higher";
	const reset = body.reset ?? "never";
	if (order !== "higher" && order !== "lower") {
		throw new HttpError(400, "bad_request", '"order" is "higher" or "lower".');
	}
	if (!(RESETS as readonly unknown[]).includes(reset)) {
		throw new HttpError(400, "bad_request", `"reset" is one of ${RESETS.join(", ")}.`);
	}
	const time = now();
	await env.DB.prepare("INSERT OR IGNORE INTO leaderboards (game, id, higher_first, reset, min_score, max_score, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)")
		.bind(game, board, order === "higher" ? 1 : 0, reset, optionalScore(body, "min"), optionalScore(body, "max"), time)
		.run();
	const kept = (await findBoard(env, game, board))!;
	if ((kept.min_score !== null && (score as number) < kept.min_score) || (kept.max_score !== null && (score as number) > kept.max_score)) {
		throw new HttpError(400, "score_out_of_range", "That score is outside what this leaderboard takes.");
	}
	const better = kept.higher_first ? "excluded.score > scores.score" : "excluded.score < scores.score";
	const period = periodStart(kept.reset, time);
	await env.DB.prepare(
		`INSERT INTO scores (game, board, period, account_id, score, at) VALUES (?, ?, ?, ?, ?, ?)
		 ON CONFLICT (game, board, period, account_id) DO UPDATE SET score = excluded.score, at = excluded.at WHERE ${better}`,
	)
		.bind(game, board, period, caller.accountId, score, time)
		.run();
	const best = await env.DB.prepare("SELECT score FROM scores WHERE game = ? AND board = ? AND period = ? AND account_id = ?")
		.bind(game, board, period, caller.accountId)
		.first<number>("score");
	return { best, improved: best === score };
}

// Scores to show: "global" (the best), "around_player" (around the player's own, or the best if
// they have none) or "friends" (the best among the accounts of the platform friends named, with
// the player). "previous" gives the period before the current one.
export async function queryScores(env: Env, caller: Caller, game: string, boardId: string, body: Record<string, unknown>) {
	const board = checkBoard(boardId);
	const scope = body.scope ?? "global";
	if (scope !== "global" && scope !== "around_player" && scope !== "friends") {
		throw new HttpError(400, "bad_request", '"scope" is "global", "around_player" or "friends".');
	}
	const count = Number.isInteger(body.count) ? Math.min(Math.max(body.count as number, 1), MAX_SCORES) : 10;
	const kept = await findBoard(env, game, board);
	if (!kept) {
		return { scores: [] as Entry[], period_start: 0 };
	}
	const time = now();
	const period = body.period === "previous" ? previousPeriodStart(kept.reset, time) : periodStart(kept.reset, time);
	const direction = kept.higher_first ? "DESC" : "ASC";
	// What the player may see: everyone shown, and themselves whatever they are.
	const visible = "(scores.account_id = ?4 OR (scores.hidden = 0 AND accounts.test = 0 AND accounts.banned_until <= ?5))";
	const base = `FROM scores JOIN accounts ON accounts.id = scores.account_id WHERE scores.game = ?1 AND scores.board = ?2 AND scores.period = ?3 AND ${visible}`;
	const select = `SELECT scores.account_id, scores.score, accounts.display_name ${base}`;

	let rows: { account_id: string; score: number; display_name: string }[];
	let firstRank = 1;
	if (scope === "friends") {
		const hashes = await friendHashes(env, body.friends);
		rows = (
			await env.DB.prepare(
				`${select} AND (scores.account_id = ?4 OR scores.account_id IN (SELECT account_id FROM identities WHERE identity_hash IN (SELECT value FROM json_each(?7))))
				 ORDER BY scores.score ${direction}, scores.at ASC LIMIT ?6`,
			)
				.bind(game, board, period, caller.accountId, time, count, JSON.stringify(hashes))
				.all<{ account_id: string; score: number; display_name: string }>()
		).results;
	} else {
		let offset = 0;
		if (scope === "around_player") {
			const own = await env.DB.prepare("SELECT score, at FROM scores WHERE game = ? AND board = ? AND period = ? AND account_id = ?")
				.bind(game, board, period, caller.accountId)
				.first<{ score: number; at: number }>();
			if (own) {
				const ahead = kept.higher_first ? "scores.score > ?6 OR (scores.score = ?6 AND scores.at < ?7)" : "scores.score < ?6 OR (scores.score = ?6 AND scores.at < ?7)";
				const better = await env.DB.prepare(`SELECT COUNT(*) AS n ${base} AND (${ahead})`)
					.bind(game, board, period, caller.accountId, time, own.score, own.at)
					.first<number>("n");
				offset = Math.max(0, (better ?? 0) - Math.floor((count - 1) / 2));
			}
		}
		rows = (
			await env.DB.prepare(`${select} ORDER BY scores.score ${direction}, scores.at ASC LIMIT ?6 OFFSET ?7`)
				.bind(game, board, period, caller.accountId, time, count, offset)
				.all<{ account_id: string; score: number; display_name: string }>()
		).results;
		firstRank = offset + 1;
	}
	return {
		scores: rows.map((row, i): Entry => ({ rank: firstRank + i, name: row.display_name, score: row.score, player: row.account_id === caller.accountId })),
		period_start: period,
	};
}

// The keyed hashes of the platform friends a game named ({"provider", "id"} each), which is all
// the service ever compares: it stores none of their IDs.
async function friendHashes(env: Env, friends: unknown): Promise<string[]> {
	if (!Array.isArray(friends)) {
		return [];
	}
	const hashes: string[] = [];
	for (const friend of friends.slice(0, MAX_FRIENDS)) {
		if (friend && typeof friend === "object" && FRIEND_PROVIDERS.includes(friend.provider) && typeof friend.id === "string" && friend.id.length > 0 && friend.id.length <= 256) {
			hashes.push(await identityHash(env.IDENTITY_SECRET, friend.provider, friend.id));
		}
	}
	return hashes;
}

// Periods before the previous one, for boards that reset (the daily cleanup).
export async function deleteOldScores(env: Env): Promise<void> {
	const boards = await env.DB.prepare("SELECT game, id, reset FROM leaderboards WHERE reset != 'never'").all<{ game: string; id: string; reset: Reset }>();
	const time = now();
	for (const board of boards.results) {
		await env.DB.prepare("DELETE FROM scores WHERE game = ? AND board = ? AND period < ?").bind(board.game, board.id, previousPeriodStart(board.reset, time)).run();
	}
}
