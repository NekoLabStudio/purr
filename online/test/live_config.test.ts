import { describe, expect, it } from "vitest";
import { callJson, testEnv } from "./helpers";
import { cleanUp } from "../src/cleanup";
import { DAY, now } from "../src/time";

async function setValue(key: string, value: unknown, startsAt: number | null = null, endsAt: number | null = null, game = "test") {
	await testEnv.DB.prepare("INSERT INTO live_config (id, game, key, value, starts_at, ends_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)")
		.bind(crypto.randomUUID(), game, key, JSON.stringify(value), startsAt, endsAt, now())
		.run();
}

describe("live config", () => {
	it("gives the game's values, scheduled ones with their times, and the service's clock, to anyone", async () => {
		const key = `bonus-${crypto.randomUUID().slice(0, 6)}`;
		const time = now();
		await setValue(key, 1.5);
		await setValue(key, 3, time + DAY, time + 2 * DAY);
		await setValue(key, 9, time - 2 * DAY, time - DAY); // over
		await setValue(key, "other game's", null, null, "small");
		const { status, body } = await callJson("GET", "/live-config/test");
		expect(status).toBe(200);
		expect(Math.abs(body.time - time)).toBeLessThan(5);
		expect(body.values.filter((value: { key: string }) => value.key === key)).toEqual([
			{ key, value: 1.5, starts_at: null, ends_at: null },
			{ key, value: 3, starts_at: time + DAY, ends_at: time + 2 * DAY },
		]);
	});

	it("leaves out values that aren't JSON, and forgets ended ones in the cleanup", async () => {
		const key = `broken-${crypto.randomUUID().slice(0, 6)}`;
		await testEnv.DB.prepare("INSERT INTO live_config (id, game, key, value, updated_at) VALUES (?, 'test', ?, 'not json', 0)").bind(crypto.randomUUID(), key).run();
		await setValue(key, true, null, now() - 1);
		expect((await callJson("GET", "/live-config/test")).body.values.some((value: { key: string }) => value.key === key)).toBe(false);
		await cleanUp(testEnv);
		expect(await testEnv.DB.prepare("SELECT COUNT(*) AS n FROM live_config WHERE key = ?").bind(key).first("n")).toBe(1);
	});

	it("is only for games the service knows", async () => {
		expect((await callJson("GET", "/live-config/nobody")).status).toBe(404);
		expect((await callJson("GET", "/live-config/Bad%20Game")).status).toBe(400);
	});
});
