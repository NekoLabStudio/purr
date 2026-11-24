# Feedback

`Feedback.send()` lets players tell you something from inside the game: a bug they hit, an idea, a problem they need help with. It goes to your [online services](../online/index.md) instance with what the game runs on, so a report of a bug comes with the versions, the system and, if you ask for it, the end of the game's log.

```gdscript
func _on_send_pressed() -> void:
    var sent = await Feedback.send("bugs", text_edit.text, true)
    status.text = "Thanks!" if sent.ok else "Couldn't send that. Try again later."
```

Name a category for each kind of feedback (`bugs`, `ideas`, `help`). `Feedback.is_available()` is true once the player is signed in to the online services; offer the feedback screen only then.

## Anonymous or named

Feedback is anonymous: nothing in it says who sent it, and email and IP addresses are removed from the player's words and the details. When you'll want to answer (help with a lost save or a purchase, say), turn on `feedback/<category>/named` in Project Settings for that category. Its feedback then names the player's account and display name, and keeps their words as written, so an address they give you stays. Say on the screen which kind it is, so players know.

## Reading it

From your instance's folder:

```sh
npm run feedback                       # the last 90 days', newest first, to read one by one
npm run feedback -- category bugs      # one category's
npm run feedback -- show <feedback>    # one in full, with what the game ran on
npm run feedback -- delete <feedback>
```

Set `FEEDBACK_EMAIL` (in Feedback in `npm run configure`) and the service also emails you each day's feedback, with the start of each. Its wording is in `email-templates/feedback.txt` and `feedback.html`, which you can override (see [Email templates](../online/email.md#templates)). Add `--local` to work on the database of `npm run dev`.

Feedback is kept for 90 days. A player's game sends at most 3 a minute, and each game keeps at most 1 MB a day (`MAX_FEEDBACK_BYTES_PER_DAY`), so a flood can't fill the database; past that, it's turned away until the next day.
