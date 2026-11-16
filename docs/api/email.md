# Progress on every platform

Players sign in through their platform without seeing an account at all. To take their progress to another platform, they add their email address: Purr's online services email them a 6-digit code, and once they've entered it, every platform signed in with that address shares one account and one set of saves.

```gdscript
func add_email(address: String) -> void:
	var sent = await Account.link_email(address)
	if not sent.ok:
		show_error(sent.error)  # "bad_email", or "rate_limited" right after another code
		return
	var code: String = await ask_player_for_code()
	var added = await Account.confirm_email(code)
	if added.ok:
		show_message("Your progress is saved to your account.")
```

On the player's other device, the same two calls bring them into that account. If the account already has a save in a slot this device also has, `Saves.conflict` lets the player choose (see [Saves](saves.md#two-devices-two-saves)).

`Account.has_email()` says whether the player has added one. Adding an email needs the online services: check `Account.can_link_email()` before offering it.

## Changing the address

The same calls change it. Pass the new address first and the current one second:

```gdscript
await Account.link_email(new_address, current_address)
var changed = await Account.confirm_email(code)
```

Changing the address signs the account out everywhere else; each device signs in again with the platform the next time the game starts.

## Errors

`Account.confirm_email()` returns `error` `"wrong_code"` for a code that's wrong, used or expired (a code works for 10 minutes and five tries), `"wrong_current_email"` when changing with the wrong current address, and `"email_in_use"` when the new address already belongs to another account.

The services keep no email addresses. They keep a one-way code made from the address, which is enough to recognise it again, and send each sign-in code through the email provider the service is set up with. See [Privacy](../online/privacy.md).
