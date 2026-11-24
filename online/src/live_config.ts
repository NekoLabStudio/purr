// Live config: values the game reads when it starts and every so often (the engine's LiveConfig),
// set on the instance with npm run live-config. Each value can be scheduled, with a start and an
// end, and the game switches to it on time by the service's clock, which comes with the values.
// Values that have ended go in the daily cleanup.

import type { Env } from "./env";
import { now } from "./time";

export interface LiveValue {
	key: string;
	value: unknown;
	starts_at: number | null;
	ends_at: number | null;
}

export async function liveConfig(env: Env, game: string): Promise<{ values: LiveValue[]; time: number }> {
	const time = now();
	const rows = await env.DB.prepare(
		"SELECT key, value, starts_at, ends_at FROM live_config WHERE game = ? AND (ends_at IS NULL OR ends_at > ?) ORDER BY key, starts_at",
	)
		.bind(game, time)
		.all<{ key: string; value: string; starts_at: number | null; ends_at: number | null }>();
	const values = rows.results.flatMap((row): LiveValue[] => {
		try {
			return [{ key: row.key, value: JSON.parse(row.value), starts_at: row.starts_at, ends_at: row.ends_at }];
		} catch {
			return []; // Set by hand, and not JSON: left out.
		}
	});
	return { values, time };
}

export async function deleteEndedValues(env: Env): Promise<void> {
	await env.DB.prepare("DELETE FROM live_config WHERE ends_at IS NOT NULL AND ends_at <= ?").bind(now()).run();
}
