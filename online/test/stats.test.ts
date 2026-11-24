import { describe, expect, it } from "vitest";
import { callJson, deviceAccount, newEmail, testEnv, verifyEmail } from "./helpers";
import { cleanUp } from "../src/cleanup";
import { DAY, now } from "../src/time";

function stat(): string {
	return `km-${crypto.randomUUID().slice(0, 8)}`;
}

function add(token: string, changes: Record<string, unknown>, limits: Record<string, unknown> = {}) {
	return callJson("POST", "/stats/test", { token, body: { changes, limits } });
}

async function total(token: string, id: string, days = 0) {
	return (await callJson("GET", `/stats/test/${id}?days=${days}`, { token })).body as { total: number; days: { day: number; added: number }[] };
}

describe("stats", () => {
	it("adds to the player's value and to the total over every player, by day", async () => {
		const id = stat();
		const ana = await deviceAccount();
		const ben = await deviceAccount();
		expect((await add(ana.token, { [id]: 12.5 })).body.stats[id]).toBe(12.5);
		expect((await add(ana.token, { [id]: 2.5 })).body.stats[id]).toBe(15);
		await add(ben.token, { [id]: 5 });
		expect((await callJson("GET", "/stats/test", { token: ben.token })).body.stats[id]).toBe(5);
		const counted = await total(ana.token, id, 7);
		expect(counted.total).toBe(20);
		expect(counted.days).toEqual([{ day: Math.floor(now() / DAY) * DAY, added: 20 }]);
		expect((await total(ana.token, id)).days).toEqual([]);
	});

	it("keeps the daily limit the first change came with, for each player", async () => {
		const id = stat();
		const ana = await deviceAccount();
		await add(ana.token, { [id]: 80 }, { [id]: 100 });
		expect((await add(ana.token, { [id]: 50 }, { [id]: 1e9 })).body.stats[id]).toBe(100);
		// Tomorrow, another 100.
		await testEnv.DB.prepare("UPDATE player_stats SET day = day - ? WHERE stat = ?").bind(DAY, id).run();
		expect((await add(ana.token, { [id]: 150 })).body.stats[id]).toBe(200);
		expect((await total(ana.token, id)).total).toBe(200);
	});

	it("counts test players' changes for them only", async () => {
		const id = stat();
		const tester = await callJson("POST", "/auth/device", { body: { game: "test", secret: crypto.randomUUID() + crypto.randomUUID() }, headers: { "x-purr-test": "1" } });
		expect((await add(tester.body.token, { [id]: 3 })).body.stats[id]).toBe(3);
		expect((await total(tester.body.token, id, 1)).total).toBe(0);
	});

	it("refuses what isn't a change", async () => {
		const ana = await deviceAccount();
		expect((await add(ana.token, { [stat()]: -1 })).status).toBe(400);
		expect((await add(ana.token, { [stat()]: "3" })).status).toBe(400);
		expect((await add(ana.token, { "bad stat": 1 })).status).toBe(400);
		expect((await add(ana.token, Object.fromEntries(Array.from({ length: 51 }, (_, i) => [`s${i}`, 1])))).status).toBe(400);
		expect((await callJson("POST", "/stats/test", { body: { changes: {} } })).status).toBe(401);
	});

	it("adds a joining account's values to the address's account's", async () => {
		const id = stat();
		const email = newEmail();
		const phone = await deviceAccount();
		await verifyEmail(email, phone.token);
		await add(phone.token, { [id]: 4 });
		const laptop = await deviceAccount();
		await add(laptop.token, { [id]: 6 });
		const joined = await verifyEmail(email, laptop.token);
		expect((await callJson("GET", "/stats/test", { token: joined.body.token })).body.stats[id]).toBe(10);
	});

	it("forgets days of history after 2 years", async () => {
		const id = stat();
		const ana = await deviceAccount();
		await add(ana.token, { [id]: 1 });
		await testEnv.DB.prepare("UPDATE stat_days SET day = day - ? WHERE stat = ?").bind(731 * DAY, id).run();
		await cleanUp(testEnv);
		expect(await testEnv.DB.prepare("SELECT COUNT(*) AS n FROM stat_days WHERE stat = ?").bind(id).first("n")).toBe(0);
		expect((await total(ana.token, id)).total).toBe(1);
	});
});
