import { describe, expect, it, vi } from "vitest";
import { callJson, deviceAccount, testEnv } from "./helpers";
import { cleanUp } from "../src/cleanup";
import { sendFeedbackDigest } from "../src/feedback";
import { DAY, now } from "../src/time";

function send(token: string, body: Record<string, unknown>) {
	return callJson("POST", "/feedback", { token, body: { game: "test", category: "bugs", text: "The ferry sank.", ...body }, headers: { "x-game-version": "1.2.0" } });
}

async function latest(category: string) {
	return testEnv.DB.prepare("SELECT * FROM feedback WHERE category = ? ORDER BY created_at DESC, rowid DESC LIMIT 1").bind(category).first<Record<string, any>>();
}

describe("feedback", () => {
	it("keeps anonymous feedback with no sender and no addresses", async () => {
		const player = await deviceAccount("Ana");
		const category = `bugs-${crypto.randomUUID().slice(0, 6)}`;
		const sent = await send(player.token, { category, text: "Write to me at ana@example.com, from 203.0.113.7", details: "OS: macOS\nUser: /Users/ana/game" });
		expect(sent.body).toEqual({ received: true });
		const kept = await latest(category);
		expect(kept).toMatchObject({ game: "test", category, text: "Write to me at [email], from [address]", details: "OS: macOS\nUser: ~/game", version: "1.2.0", account_id: null, display_name: "" });
	});

	it("names the sender of a category that does, with their words as written", async () => {
		const player = await deviceAccount("Ben");
		const category = `support-${crypto.randomUUID().slice(0, 6)}`;
		await send(player.token, { category, named: true, text: "Reach me at ben@example.com" });
		expect(await latest(category)).toMatchObject({ account_id: player.id, display_name: "Ben", text: "Reach me at ben@example.com" });
	});

	it("turns away what isn't feedback", async () => {
		const player = await deviceAccount();
		expect((await send(player.token, { text: "   " })).status).toBe(400);
		expect((await send(player.token, { text: "x".repeat(2001) })).status).toBe(400);
		expect((await send(player.token, { category: "Bad Category" })).status).toBe(400);
		expect((await send(player.token, { game: "nobody" })).status).toBe(404);
		expect((await callJson("POST", "/feedback", { body: { game: "test", category: "bugs", text: "hi" } })).status).toBe(401);
	});

	it("takes only so much a day for each game", async () => {
		const player = await deviceAccount();
		const env = { ...testEnv, MAX_FEEDBACK_BYTES_PER_DAY: 0 };
		const { receiveFeedback } = await import("../src/feedback");
		await expect(receiveFeedback(env, { accountId: player.id, scope: "account" }, { game: "test", category: "bugs", text: "hi" }, null)).rejects.toMatchObject({ code: "feedback_full" });
	});

	it("forgets feedback after 90 days, and its sender once their account goes", async () => {
		const player = await deviceAccount("Cy");
		const category = `old-${crypto.randomUUID().slice(0, 6)}`;
		await send(player.token, { category, named: true });
		await callJson("DELETE", "/account", { token: player.token });
		expect(await latest(category)).toMatchObject({ account_id: null });
		await testEnv.DB.prepare("UPDATE feedback SET created_at = ? WHERE category = ?").bind(now() - 91 * DAY, category).run();
		await cleanUp(testEnv);
		expect(await latest(category)).toBeNull();
	});

	it("emails the day's feedback to FEEDBACK_EMAIL, when it's set", async () => {
		const player = await deviceAccount("Dee");
		await send(player.token, { category: `digest-${crypto.randomUUID().slice(0, 6)}`, text: "Loved the night drive." });
		expect(await sendFeedbackDigest(testEnv)).toBe(false);
		const log = vi.spyOn(console, "log").mockImplementation(() => {});
		try {
			expect(await sendFeedbackDigest({ ...testEnv, FEEDBACK_EMAIL: "team@example.com" })).toBe(true);
			const sent = log.mock.calls.map((args) => String(args[0])).find((line) => line.startsWith("[email] to team@example.com:"));
			expect(sent).toMatch(/Players' feedback today/);
			expect(sent).toMatch(/Loved the night drive\./);
		} finally {
			log.mockRestore();
		}
	});
});
