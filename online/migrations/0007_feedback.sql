-- Players' feedback, sent from the game (the engine's Feedback): a category the game names, the
-- player's words and what the game ran on. Anonymous unless the category names its sender (so the
-- developer can follow up): then the account, and its name when sent. Kept for 90 days.
CREATE TABLE feedback (
	id TEXT PRIMARY KEY,
	game TEXT NOT NULL,
	category TEXT NOT NULL,
	text TEXT NOT NULL,
	-- What the game ran on, and the end of its log if the game sent it.
	details TEXT NOT NULL DEFAULT '',
	version TEXT NOT NULL DEFAULT '',
	-- The sender, for a category that names them; NULL otherwise or once the account is gone.
	account_id TEXT REFERENCES accounts (id) ON DELETE SET NULL,
	display_name TEXT NOT NULL DEFAULT '',
	created_at INTEGER NOT NULL
);
CREATE INDEX feedback_by_time ON feedback (game, created_at);
CREATE INDEX feedback_by_account ON feedback (account_id);
