-- Leaderboards across every platform a game is on. A board is made by the first score sent to it,
-- with the order and reset the game gave (its Project Settings), and keeps them: a game can't
-- change them afterwards, the operator can (npm run moderate). Each player keeps their best score
-- in each period: since the start of the day, week or month (UTC) for boards that reset, or one
-- period from 0 for those that don't. The period before the current one is kept too.
CREATE TABLE leaderboards (
	game TEXT NOT NULL,
	id TEXT NOT NULL,
	-- 1 if higher scores are better, 0 if lower ones are (a lap time).
	higher_first INTEGER NOT NULL,
	-- "never", "daily", "weekly" (from Monday) or "monthly".
	reset TEXT NOT NULL,
	-- Scores outside these are refused; NULL for no limit.
	min_score INTEGER,
	max_score INTEGER,
	created_at INTEGER NOT NULL,
	PRIMARY KEY (game, id)
);

CREATE TABLE scores (
	game TEXT NOT NULL,
	board TEXT NOT NULL,
	period INTEGER NOT NULL,
	account_id TEXT NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
	score INTEGER NOT NULL,
	at INTEGER NOT NULL,
	-- Hidden by the operator: only its player sees it.
	hidden INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY (game, board, period, account_id)
);
CREATE INDEX scores_by_score ON scores (game, board, period, score, at);
CREATE INDEX scores_by_account ON scores (account_id);

-- Test players (developers' and testers' accounts): signed in from a debug build, or marked by
-- the operator. Other players never see their scores, and they don't count in totals.
ALTER TABLE accounts ADD COLUMN test INTEGER NOT NULL DEFAULT 0;
