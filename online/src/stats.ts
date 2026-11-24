// Stats (the engine's Stats): counters each player adds to, and their sum over every player. The
// game adds up changes on the device and sends them every few minutes; each call adds to the
// player's values, to each stat's total and to the day's additions, which make its history. A stat
// is made by its first change, with the most one player can add in a day that came with it (the
// game's Project Settings): past that, the rest of the day's additions don't count, so a modified
// copy of the game can't run a total up. Test players' changes count for them, not in totals.

import type { Env, GameSettings } from "./env";
import type { Caller } from "./sessions";
import { HttpError } from "./http";
import { requireOwner } from "./ownership";
import { DAY, now } from "./time";

const STAT_ID = /^[A-Za-z0-9_.-]{1,64}$/;
export const MAX_CHANGES = 50;
const MAX_AMOUNT = 1e12;
export const MAX_HISTORY_DAYS = 730;

export function checkStat(id: string): string {
	if (!STAT_ID.test(id)) {
		throw new HttpError(400, "bad_request", 'A stat ID is 1 to 64 letters, digits, "_", "." or "-".');
	}
	return id;
}

function entries(value: unknown, key: string): [string, unknown][] {
	if (value === undefined || value === null) {
		return [];
	}
	if (typeof value !== "object" || Array.isArray(value)) {
		throw new HttpError(400, "bad_request", `"${key}" must be an object of stat IDs.`);
	}
	return Object.entries(value);
}

export async function playerStats(env: Env, accountId: string, game: string): Promise<{ stats: Record<string, number> }> {
	const rows = await env.DB.prepare("SELECT stat, value FROM player_stats WHERE account_id = ? AND game = ?").bind(accountId, game).all<{ stat: string; value: number }>();
	return { stats: Object.fromEntries(rows.results.map((row) => [row.stat, row.value])) };
}

// {"changes": {stat: amount}, "limits": {stat: most a player adds in a day}}: amounts are above 0.
// Returns the player's values for the game, after the changes.
export async function addStats(env: Env, caller: Caller, game: string, settings: GameSettings, body: Record<string, unknown>) {
	await requireOwner(env, game, settings, caller);
	if (env.STATS_LIMITER) {
		const { success } = await env.STATS_LIMITER.limit({ key: `account:${caller.accountId}` });
		if (!success) {
			throw new HttpError(429, "rate_limited", "Too many stats changes; try again in a minute.");
		}
	}
	const changes = entries(body.changes, "changes");
	if (changes.length > MAX_CHANGES) {
		throw new HttpError(400, "bad_request", `At most ${MAX_CHANGES} stats change at once.`);
	}
	for (const [id, amount] of changes) {
		checkStat(id);
		if (typeof amount !== "number" || !Number.isFinite(amount) || amount <= 0 || amount > MAX_AMOUNT) {
			throw new HttpError(400, "bad_request", `A change to "${id}" must be a number above 0.`);
		}
	}
	const limits = new Map(entries(body.limits, "limits").filter(([, most]) => typeof most === "number" && Number.isFinite(most) && most > 0) as [string, number][]);
	if (changes.length === 0) {
		return playerStats(env, caller.accountId, game);
	}

	const time = now();
	const day = Math.floor(time / DAY) * DAY;
	const ids = JSON.stringify(changes.map(([id]) => id));
	await env.DB.batch(
		changes.map(([id]) =>
			env.DB.prepare("INSERT OR IGNORE INTO stats (game, id, max_per_day, created_at) VALUES (?, ?, ?, ?)").bind(game, id, limits.get(id) ?? null, time),
		),
	);
	const kept = new Map(
		(await env.DB.prepare("SELECT id, max_per_day FROM stats WHERE game = ? AND id IN (SELECT value FROM json_each(?))").bind(game, ids).all<{ id: string; max_per_day: number | null }>()).results.map(
			(row) => [row.id, row.max_per_day],
		),
	);
	const today = new Map(
		(
			await env.DB.prepare("SELECT stat, day_added FROM player_stats WHERE account_id = ? AND game = ? AND day = ? AND stat IN (SELECT value FROM json_each(?))")
				.bind(caller.accountId, game, day, ids)
				.all<{ stat: string; day_added: number }>()
		).results.map((row) => [row.stat, row.day_added]),
	);
	const test = (await env.DB.prepare("SELECT test FROM accounts WHERE id = ?").bind(caller.accountId).first<number>("test")) === 1;

	const statements = [];
	for (const [id, amount] of changes as [string, number][]) {
		const most = kept.get(id) ?? null;
		const added = most === null ? amount : Math.min(amount, Math.max(0, most - (today.get(id) ?? 0)));
		if (added <= 0) {
			continue;
		}
		statements.push(
			env.DB.prepare(
				`INSERT INTO player_stats (account_id, game, stat, value, day, day_added) VALUES (?1, ?2, ?3, ?4, ?5, ?4)
				 ON CONFLICT (account_id, game, stat) DO UPDATE SET value = value + ?4,
				   day_added = CASE WHEN day = ?5 THEN day_added + ?4 ELSE ?4 END, day = ?5`,
			).bind(caller.accountId, game, id, added, day),
		);
		if (!test) {
			statements.push(env.DB.prepare("UPDATE stats SET total = total + ? WHERE game = ? AND id = ?").bind(added, game, id));
			statements.push(
				env.DB.prepare("INSERT INTO stat_days (game, stat, day, added) VALUES (?, ?, ?, ?) ON CONFLICT (game, stat, day) DO UPDATE SET added = added + excluded.added").bind(
					game,
					id,
					day,
					added,
				),
			);
		}
	}
	if (statements.length > 0) {
		await env.DB.batch(statements);
	}
	return playerStats(env, caller.accountId, game);
}

// A stat's total over every player, and what was added each day of the last `days` (0 to 730),
// oldest first.
export async function statTotal(env: Env, game: string, statId: string, daysText: string | null) {
	const stat = checkStat(statId);
	const days = Math.min(Math.max(Number.parseInt(daysText ?? "0", 10) || 0, 0), MAX_HISTORY_DAYS);
	const total = (await env.DB.prepare("SELECT total FROM stats WHERE game = ? AND id = ?").bind(game, stat).first<number>("total")) ?? 0;
	const since = Math.floor(now() / DAY) * DAY - (days - 1) * DAY;
	const history =
		days > 0
			? (await env.DB.prepare("SELECT day, added FROM stat_days WHERE game = ? AND stat = ? AND day >= ? ORDER BY day").bind(game, stat, since).all<{ day: number; added: number }>()).results
			: [];
	return { total, days: history };
}

export async function deleteOldStatDays(env: Env): Promise<void> {
	await env.DB.prepare("DELETE FROM stat_days WHERE day < ?").bind(now() - MAX_HISTORY_DAYS * DAY).run();
}
