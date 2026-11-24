-- Stats (the engine's Stats): counters each player adds to ("deliveries", "km driven"), with every
-- player's added up into a total, and each day's additions kept for 2 years for a history. A stat
-- is made by the first change sent to it, with the most a player can add in a day that came with
-- it (the game's Project Settings). Test players' changes count for them, never in totals.
CREATE TABLE stats (
	game TEXT NOT NULL,
	id TEXT NOT NULL,
	-- The most one player adds in a day (UTC); NULL for no limit.
	max_per_day REAL,
	total REAL NOT NULL DEFAULT 0,
	created_at INTEGER NOT NULL,
	PRIMARY KEY (game, id)
);

CREATE TABLE stat_days (
	game TEXT NOT NULL,
	stat TEXT NOT NULL,
	day INTEGER NOT NULL,
	added REAL NOT NULL,
	PRIMARY KEY (game, stat, day)
);

CREATE TABLE player_stats (
	account_id TEXT NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
	game TEXT NOT NULL,
	stat TEXT NOT NULL,
	value REAL NOT NULL DEFAULT 0,
	-- What the player added on that day, against max_per_day.
	day INTEGER NOT NULL DEFAULT 0,
	day_added REAL NOT NULL DEFAULT 0,
	PRIMARY KEY (account_id, game, stat)
);
