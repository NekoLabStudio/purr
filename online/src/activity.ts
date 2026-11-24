// What happened on an account (migrations/0008_account_activity.sql), for the player's "recent
// activity" and the operator helping them. Sign-ins are recorded at most once a day for each
// provider, so a player signing in all day long fills nothing up. No IP addresses or locations,
// and 90 days at most.

import type { Env } from "./env";
import { DAY, now } from "./time";

export type ActivityKind =
	| "signed_in" // with a platform, a device account or email (provider)
	| "guest_signed_in" // brought their profile to someone else's device (detail: its label, if remembered)
	| "email_added"
	| "email_changed"
	| "account_joined" // another account joined this one, adding an email (detail: its providers)
	| "device_remembered" // a host device remembers them as a guest (detail: its label)
	| "device_forgotten" // (detail: its label)
	| "name_changed"; // (detail: the name)

export const ACTIVITY_DAYS = 90;
const MAX_LISTED = 100;
const MAX_DETAIL = 128;

export async function recordActivity(env: Env, accountId: string, kind: ActivityKind, fields: { provider?: string; game?: string; detail?: string } = {}): Promise<void> {
	const time = now();
	const provider = fields.provider ?? "";
	const values = [accountId, time, kind, provider, fields.game ?? "", (fields.detail ?? "").slice(0, MAX_DETAIL)];
	if (kind === "signed_in") {
		await env.DB.prepare(
			`INSERT INTO account_activity (account_id, at, kind, provider, game, detail) SELECT ?1, ?2, ?3, ?4, ?5, ?6
			 WHERE NOT EXISTS (SELECT 1 FROM account_activity WHERE account_id = ?1 AND kind = ?3 AND provider = ?4 AND at > ?2 - ?7)`,
		)
			.bind(...values, DAY)
			.run();
		return;
	}
	await env.DB.prepare("INSERT INTO account_activity (account_id, at, kind, provider, game, detail) VALUES (?, ?, ?, ?, ?, ?)").bind(...values).run();
}

export async function listActivity(env: Env, accountId: string) {
	const rows = await env.DB.prepare("SELECT at, kind, provider, game, detail FROM account_activity WHERE account_id = ? ORDER BY at DESC, rowid DESC LIMIT ?")
		.bind(accountId, MAX_LISTED)
		.all<{ at: number; kind: ActivityKind; provider: string; game: string; detail: string }>();
	return { activity: rows.results };
}

export async function deleteOldActivity(env: Env): Promise<void> {
	await env.DB.prepare("DELETE FROM account_activity WHERE at < ?").bind(now() - ACTIVITY_DAYS * DAY).run();
}
