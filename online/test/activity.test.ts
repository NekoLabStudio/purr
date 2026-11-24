import { describe, expect, it } from "vitest";
import { callJson, deviceAccount, emailAccount, emailCode, newEmail, testEnv, verifyEmail } from "./helpers";
import { cleanUp } from "../src/cleanup";
import { DAY, now } from "../src/time";

async function activity(token: string) {
	const { status, body } = await callJson("GET", "/account/activity", { token });
	expect(status).toBe(200);
	return body.activity as { at: number; kind: string; provider: string; game: string; detail: string }[];
}

describe("account activity", () => {
	it("records a sign-in once a day for each provider", async () => {
		const secret = crypto.randomUUID() + crypto.randomUUID();
		const first = await callJson("POST", "/auth/device", { body: { game: "test", secret } });
		await callJson("POST", "/auth/device", { body: { game: "test", secret } });
		const listed = await activity(first.body.token);
		expect(listed).toHaveLength(1);
		expect(listed[0]).toMatchObject({ kind: "signed_in", provider: "device", game: "test", detail: "" });
	});

	it("records an email added and changed, a name chosen, and another account joining", async () => {
		const player = await deviceAccount("Ana");
		const email = newEmail();
		await verifyEmail(email, player.token);
		await callJson("PUT", "/account/name", { token: player.token, body: { display_name: "Ana B" } });
		const laptop = await deviceAccount("Laptop");
		const joined = await verifyEmail(email, laptop.token);
		const kinds = (await activity(joined.body.token)).map((entry) => `${entry.kind}:${entry.provider || entry.detail}`);
		// Newest first; the laptop's sign-in came after the name was chosen.
		expect(kinds).toEqual(["account_joined:device", "signed_in:device", "name_changed:Ana B", "email_added:", "signed_in:device"]);
		const changed = await verifyEmail(newEmail(), joined.body.token, { current_email: email });
		expect((await activity(changed.body.token))[0].kind).toBe("email_changed");
	});

	it("records a guest's sessions and the devices that remember them", async () => {
		const host = await deviceAccount("Host");
		const guest = await emailAccount("Inês");
		const { code, challenge } = await emailCode(guest.email, { hostToken: host.token });
		const brought = await callJson("POST", "/guest/email/verify", { token: host.token, body: { email: guest.email, code, challenge, remember: true, game: "test", device_label: "Living room Apple TV" } });
		const grants = await callJson("GET", "/grants", { token: guest.token });
		await callJson("DELETE", `/grants/${grants.body.grants[0].id}`, { token: guest.token });
		const kinds = (await activity(guest.token)).map((entry) => `${entry.kind}:${entry.detail}`);
		expect(brought.body.kind).toBe("grant");
		expect(kinds.slice(0, 2)).toEqual(["device_forgotten:Living room Apple TV", "device_remembered:Living room Apple TV"]);
	});

	it("is only for the account's own session, and goes after 90 days", async () => {
		const player = await deviceAccount();
		expect((await callJson("GET", "/account/activity")).status).toBe(401);
		await testEnv.DB.prepare("UPDATE account_activity SET at = ? WHERE account_id = ?").bind(now() - 91 * DAY, player.id).run();
		await cleanUp(testEnv);
		expect(await activity(player.token)).toEqual([]);
	});
});
