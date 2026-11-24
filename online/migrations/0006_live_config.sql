-- Values the game reads at launch and the developer changes on the instance (npm run live-config)
-- without shipping an update: feature flags, tuning, seasonal events. Each row is a key's value,
-- optionally only from starts_at and until ends_at: the game picks the row in effect, the latest
-- to start if several are. Nothing about players is kept here.
CREATE TABLE live_config (
	id TEXT PRIMARY KEY,
	game TEXT NOT NULL,
	key TEXT NOT NULL,
	-- JSON.
	value TEXT NOT NULL,
	starts_at INTEGER,
	ends_at INTEGER,
	updated_at INTEGER NOT NULL
);
CREATE INDEX live_config_by_game ON live_config (game, key);
