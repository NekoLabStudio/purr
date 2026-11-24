-- What happened on each account, for the player (Account.get_activity(), "recent activity") and
-- the operator helping them: sign-ins (by provider, at most one a day each), an email address added
-- or changed, another account joining this one, guests' sessions and remembered devices, a name
-- chosen. No IP addresses or locations. Kept for 90 days, and with the account.
CREATE TABLE account_activity (
	account_id TEXT NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
	at INTEGER NOT NULL,
	kind TEXT NOT NULL,
	provider TEXT NOT NULL DEFAULT '',
	game TEXT NOT NULL DEFAULT '',
	-- A device's label, a name, the providers of an account that joined.
	detail TEXT NOT NULL DEFAULT ''
);
CREATE INDEX account_activity_by_account ON account_activity (account_id, at);
CREATE INDEX account_activity_by_time ON account_activity (at);
