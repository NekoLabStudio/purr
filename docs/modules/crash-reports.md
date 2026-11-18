# Crash reports

When a run of your game crashes, is closed by force or loses power, `CrashReports` notices on the next start and writes a report: a text file with what the game was running on and the end of its log, where a crash's backtrace is. Nothing to set up, and nothing is sent anywhere.

```gdscript
func _ready() -> void:
	if CrashReports.previous_run_crashed():
		$CrashDialog.show_report(CrashReports.get_reports()[0])
```

The reports are in `user://crashes`, newest first in `get_reports()`, and the newest 20 are kept. Each has:

- the engine's and the game's versions (`application/config/version`);
- the OS, processor, memory and GPU;
- anything you added with `set_context()`;
- the last 200 lines of that run's log, with a crash's backtrace: the engine's and the GDScript one, naming the script and the line.

## Adding context

What the game was doing helps most: add it as it changes, and it's in the report should the run crash.

```gdscript
CrashReports.set_context("level", level.name)
CrashReports.set_context("save", save_name)
```

## Sending them

With Purr's [online services](../online/index.md), reports can go to them, where `npm run crashes` lists them grouped by crash (see [Crash reports](../online/crash-reports.md)). Nothing is sent until the player agrees: ask once, in your settings or after a crash, and keep their answer in the built-in option `privacy/send_crash_reports`:

```gdscript
func _on_send_reports_toggled(on: bool) -> void:
	Options.set_value("privacy/send_crash_reports", on)
```

While it's on, every report waiting is sent once the player is signed in, as soon as they agree and on each start after, and removed from the device once the service has it. A report that couldn't be sent waits for the next start. To send one yourself, after asking about that crash alone:

```gdscript
var result: Dictionary = await CrashReports.send(CrashReports.get_reports()[0])
```

Before a report leaves the device, the player's home folder (often named after them) becomes `~`, and anything shaped like an email or IP address is removed: a log can pick those up from your game's own messages. The service removes them again, and keeps nothing that says who sent a report.

Without the online services, send reports to your own server, or open their folder for the player to attach one to an email. `delete_report()` removes one once it's sent.

```gdscript
OS.shell_show_in_file_manager(ProjectSettings.globalize_path(CrashReports.get_reports()[0]))
```

## How it knows

Each run leaves a note in `user://crashes` while it runs and removes it when it ends cleanly. A note whose process has gone is a run that didn't end cleanly. Two copies of the game running at once (testing multiplayer, say) don't mistake each other for crashes. Phones, tablets and Apple TV close games in the background whenever they need the memory, so the note is removed while the game is in the background, and that isn't reported either.

Runs started from the editor aren't watched: stopping one kills it, which would read as a crash every time. Turn crash reports off with `crash_reports/enabled` in Project Settings.
