import { describe, expect, it } from "vitest";
import { callJson, deviceAccount, testEnv, verifyEmail, newEmail } from "./helpers";
import { cleanUp } from "../src/cleanup";
import { identityHash } from "../src/identities";
import { periodStart, previousPeriodStart } from "../src/leaderboards";
import { DAY, now } from "../src/time";

function board(): string {
	return `laps-${crypto.randomUUID().slice(0, 8)}`;
}

function submit(token: string, id: string, score: number, definition: Record<string, unknown> = {}) {
	return callJson("PUT", `/leaderboards/test/${id}/score`, { token, body: { score, ...definition } });
}

async function scores(token: string, id: string, query: Record<string, unknown> = {}) {
	const { status, body } = await callJson("POST", `/leaderboards/test/${id}/query`, { token, body: query });
	expect(status).toBe(200);
	return body.scores as { rank: number; name: string; score: number; player: boolean }[];
}

describe("leaderboards", () => {
	it("ranks the best score of each player, higher first", async () => {
		const id = board();
		const ana = await deviceAccount("Ana");
		const ben = await deviceAccount("Ben");
		expect((await submit(ana.token, id, 300)).body).toEqual({ best: 300, improved: true });
		await submit(ben.token, id, 500);
		expect((await submit(ana.token, id, 200)).body).toEqual({ best: 300, improved: false });
		expect(await scores(ana.token, id)).toEqual([
			{ rank: 1, name: "Ben", score: 500, player: false },
			{ rank: 2, name: "Ana", score: 300, player: true },
		]);
	});

	it("keeps how the first score said a board is ordered, and its limits", async () => {
		const id = board();
		const ana = await deviceAccount("Ana");
		const ben = await deviceAccount("Ben");
		await submit(ana.token, id, 61_000, { order: "lower", min: 30_000 });
		// A later score can't turn the board round.
		await submit(ben.token, id, 59_000, { order: "higher" });
		await submit(ben.token, id, 70_000, { order: "higher" });
		expect((await scores(ana.token, id)).map((entry) => entry.score)).toEqual([59_000, 61_000]);
		const tooFast = await submit(ana.token, id, 1_000);
		expect(tooFast.status).toBe(400);
		expect(tooFast.body.error).toBe("score_out_of_range");
	});

	it("refuses scores that aren't whole numbers, and unknown orders and resets", async () => {
		const ana = await deviceAccount();
		expect((await submit(ana.token, board(), 1.5)).status).toBe(400);
		expect((await submit(ana.token, board(), 1, { order: "sideways" })).status).toBe(400);
		expect((await submit(ana.token, board(), 1, { reset: "hourly" })).status).toBe(400);
		expect((await callJson("PUT", `/leaderboards/test/Bad%20Name/score`, { token: ana.token, body: { score: 1 } })).status).toBe(400);
		// Project Settings' IDs are fine.
		expect((await callJson("PUT", `/leaderboards/test/Weekly.Tips-${crypto.randomUUID().slice(0, 4)}/score`, { token: ana.token, body: { score: 1 } })).status).toBe(200);
	});

	it("shows the scores around the player's own", async () => {
		const id = board();
		const players = [];
		for (let i = 0; i < 9; i++) {
			const player = await deviceAccount(`P${i}`);
			await submit(player.token, id, 100 - i);
			players.push(player);
		}
		const around = await scores(players[6].token, id, { scope: "around_player", count: 3 });
		expect(around.map((entry) => entry.rank)).toEqual([6, 7, 8]);
		expect(around[1].player).toBe(true);
		// Without a score of their own, the best.
		const newcomer = await deviceAccount();
		expect((await scores(newcomer.token, id, { scope: "around_player", count: 2 })).map((entry) => entry.rank)).toEqual([1, 2]);
	});

	it("finds friends by their platform IDs, which it never stores", async () => {
		const id = board();
		const me = await deviceAccount("Me");
		const friend = await deviceAccount("Friend");
		const stranger = await deviceAccount("Stranger");
		await testEnv.DB.prepare("INSERT INTO identities (identity_hash, key_version, provider, account_id) VALUES (?, 1, 'steam', ?)")
			.bind(await identityHash(testEnv.IDENTITY_SECRET, "steam", "76561190000000001"), friend.id)
			.run();
		for (const [player, score] of [[me, 10], [friend, 20], [stranger, 30]] as const) {
			await submit(player.token, id, score);
		}
		const friends = await scores(me.token, id, { scope: "friends", friends: [{ provider: "steam", id: "76561190000000001" }, { provider: "steam", id: "76561190000000002" }] });
		expect(friends.map((entry) => entry.name)).toEqual(["Friend", "Me"]);
		expect(friends.map((entry) => entry.rank)).toEqual([1, 2]);
	});

	it("shows test players', hidden and banned players' scores only to themselves", async () => {
		const id = board();
		const player = await deviceAccount("Player");
		const tester = await callJson("POST", "/auth/device", { body: { game: "test", secret: crypto.randomUUID() + crypto.randomUUID(), display_name: "Tester" }, headers: { "x-purr-test": "1" } });
		const hidden = await deviceAccount("Hidden");
		const banned = await deviceAccount("Banned");
		for (const [token, score] of [[player.token, 1], [tester.body.token, 2], [hidden.token, 3], [banned.token, 4]] as const) {
			await submit(token, id, score);
		}
		await testEnv.DB.prepare("UPDATE scores SET hidden = 1 WHERE account_id = ?").bind(hidden.id).run();
		await testEnv.DB.prepare("UPDATE accounts SET banned_until = ? WHERE id = ?").bind(now() + DAY, banned.id).run();
		expect((await scores(player.token, id)).map((entry) => entry.name)).toEqual(["Player"]);
		expect((await scores(tester.body.token, id)).map((entry) => entry.name)).toEqual(["Tester", "Player"]);
		expect((await scores(hidden.token, id)).map((entry) => entry.name)).toEqual(["Hidden", "Player"]);
	});

	it("marks a test build's sign-in as a test player for good", async () => {
		const secret = crypto.randomUUID() + crypto.randomUUID();
		const first = await callJson("POST", "/auth/device", { body: { game: "test", secret }, headers: { "x-purr-test": "1" } });
		await callJson("POST", "/auth/device", { body: { game: "test", secret } });
		expect(await testEnv.DB.prepare("SELECT test FROM accounts WHERE id = ?").bind(first.body.account_id).first("test")).toBe(1);
	});

	it("starts a new period when a board resets, keeping the one before", async () => {
		const id = board();
		const ana = await deviceAccount("Ana");
		await submit(ana.token, id, 50, { reset: "weekly" });
		const time = now();
		const before = previousPeriodStart("weekly", time);
		await testEnv.DB.prepare("UPDATE scores SET period = ? WHERE board = ?").bind(before, id).run();
		expect(await scores(ana.token, id)).toEqual([]);
		expect((await scores(ana.token, id, { period: "previous" })).map((entry) => entry.score)).toEqual([50]);
		// Periods before the previous one go in the daily cleanup.
		await testEnv.DB.prepare("UPDATE scores SET period = ? WHERE board = ?").bind(before - 7 * DAY, id).run();
		await cleanUp(testEnv);
		expect(await testEnv.DB.prepare("SELECT COUNT(*) AS n FROM scores WHERE board = ?").bind(id).first("n")).toBe(0);
	});

	it("works out periods in UTC, weeks from Monday", () => {
		const wednesday = Date.UTC(2026, 10, 25, 15, 30) / 1000;
		expect(periodStart("daily", wednesday)).toBe(Date.UTC(2026, 10, 25) / 1000);
		expect(periodStart("weekly", wednesday)).toBe(Date.UTC(2026, 10, 23) / 1000);
		expect(periodStart("monthly", wednesday)).toBe(Date.UTC(2026, 10, 1) / 1000);
		expect(previousPeriodStart("monthly", wednesday)).toBe(Date.UTC(2026, 9, 1) / 1000);
		expect(previousPeriodStart("weekly", Date.UTC(2026, 10, 23) / 1000)).toBe(Date.UTC(2026, 10, 16) / 1000);
		expect(periodStart("never", wednesday)).toBe(0);
	});

	it("keeps the better of two accounts' scores when one joins the other", async () => {
		const id = board();
		const email = newEmail();
		const phone = await deviceAccount("Phone");
		await verifyEmail(email, phone.token);
		await submit(phone.token, id, 40, { order: "lower" });
		const laptop = await deviceAccount("Laptop");
		await submit(laptop.token, id, 30);
		const joined = await verifyEmail(email, laptop.token);
		expect(joined.body.merged).toBe(true);
		expect((await scores(joined.body.token, id)).map((entry) => entry.score)).toEqual([30]);
	});

	it("needs a signed-in account", async () => {
		expect((await callJson("PUT", `/leaderboards/test/${board()}/score`, { body: { score: 1 } })).status).toBe(401);
		expect((await callJson("POST", `/leaderboards/nobody/${board()}/query`, { token: (await deviceAccount()).token, body: {} })).status).toBe(404);
	});
});
