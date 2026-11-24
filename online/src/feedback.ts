// Players' feedback (the engine's Feedback): a few words in a category the game names, with what
// the game ran on. Anonymous unless the game's category names its sender; then the account and its
// name are kept, so the developer can follow up. Email and IP addresses are removed from what's
// anonymous (the player's words and the details) and from every report's details, as with crash
// reports. The operator reads them with npm run feedback, or a daily email (FEEDBACK_EMAIL).

import type { Env } from "./env";
import type { Caller } from "./sessions";
import { findAccount } from "./accounts";
import { redact } from "./crash_reports";
import { sendEmail } from "./email";
import { renderEmail } from "./email_templates";
import { HttpError, requireString } from "./http";
import { requireGame } from "./identities";
import { DAY, now } from "./time";

export const MAX_FEEDBACK_TEXT = 2000;
export const MAX_FEEDBACK_DETAILS = 16384;
export const FEEDBACK_DAYS = 90;
// What each game keeps a day at most (MAX_FEEDBACK_BYTES_PER_DAY to change it): past it, feedback
// is turned away until the next day, so nobody can fill the database with it.
export const DEFAULT_MAX_FEEDBACK_BYTES_PER_DAY = 1_000_000;
const CATEGORY = /^[a-z0-9_-]{1,64}$/;
const MAX_VERSION = 64;
const MAX_EMAILED = 50;
const EMAILED_TEXT = 300;

export async function receiveFeedback(env: Env, caller: Caller, body: Record<string, unknown>, versionHeader: string | null) {
	const game = requireString(body, "game", 64);
	requireGame(env, game);
	const category = requireString(body, "category", 64);
	if (!CATEGORY.test(category)) {
		throw new HttpError(400, "bad_request", 'A category is 1 to 64 lowercase letters, digits, "-" or "_".');
	}
	const words = requireString(body, "text", MAX_FEEDBACK_TEXT).replace(/[\p{Cc}\p{Cf}]/gu, (c) => (c === "\n" ? c : " ")).trim();
	if (!words) {
		throw new HttpError(400, "bad_request", "Feedback needs some words.");
	}
	const details = typeof body.details === "string" ? redact(body.details.slice(0, MAX_FEEDBACK_DETAILS)) : "";
	const named = body.named === true && caller.scope === "account";
	const text = named ? words : redact(words);
	if (env.FEEDBACK_LIMITER) {
		const { success } = await env.FEEDBACK_LIMITER.limit({ key: `account:${caller.accountId}` });
		if (!success) {
			throw new HttpError(429, "rate_limited", "Too much feedback; try again in a minute.");
		}
	}
	const time = now();
	const today = await env.DB.prepare("SELECT COALESCE(SUM(LENGTH(text) + LENGTH(details)), 0) AS bytes FROM feedback WHERE game = ? AND created_at > ?")
		.bind(game, time - DAY)
		.first<number>("bytes");
	if ((today ?? 0) + text.length + details.length > (env.MAX_FEEDBACK_BYTES_PER_DAY ?? DEFAULT_MAX_FEEDBACK_BYTES_PER_DAY)) {
		throw new HttpError(503, "feedback_full", "The game has had all the feedback it takes today; try again tomorrow.");
	}
	const account = named ? await findAccount(env, caller.accountId) : null;
	await env.DB.prepare("INSERT INTO feedback (id, game, category, text, details, version, account_id, display_name, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")
		.bind(crypto.randomUUID(), game, category, text, details, (versionHeader ?? "").trim().slice(0, MAX_VERSION), account?.id ?? null, account?.display_name ?? "", time)
		.run();
	return { received: true };
}

export async function deleteOldFeedback(env: Env): Promise<void> {
	await env.DB.prepare("DELETE FROM feedback WHERE created_at < ?").bind(now() - FEEDBACK_DAYS * DAY).run();
}

// The daily email to the operator, with FEEDBACK_EMAIL set: the last day's feedback, each with the
// start of its words. No feedback, no email.
export async function sendFeedbackDigest(env: Env): Promise<boolean> {
	if (!env.FEEDBACK_EMAIL) {
		return false;
	}
	const rows = await env.DB.prepare(
		"SELECT game, category, text, version, display_name, account_id FROM feedback WHERE created_at > ? ORDER BY game, category, created_at LIMIT ?",
	)
		.bind(now() - DAY, MAX_EMAILED + 1)
		.all<{ game: string; category: string; text: string; version: string; display_name: string; account_id: string | null }>();
	if (rows.results.length === 0) {
		return false;
	}
	const lines = rows.results.slice(0, MAX_EMAILED).map((row) => {
		const words = row.text.length > EMAILED_TEXT ? `${row.text.slice(0, EMAILED_TEXT)}...` : row.text;
		const from = row.account_id ? `, from ${row.display_name || "(no name)"} (${row.account_id})` : "";
		return `${row.game}, ${row.category}${row.version ? ` (${row.version})` : ""}${from}:\n${words.replace(/\s*\n\s*/g, " ")}\n`;
	});
	if (rows.results.length > MAX_EMAILED) {
		lines.push("...and more.");
	}
	const count = rows.results.length > MAX_EMAILED ? `${MAX_EMAILED}+` : String(rows.results.length);
	await sendEmail(env, { to: env.FEEDBACK_EMAIL, ...(await renderEmail("feedback", "", { game: "Feedback", count, feedback: lines.join("\n") })) });
	return true;
}
