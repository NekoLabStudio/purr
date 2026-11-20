# AI assistants

AI coding assistants know Godot well, but not what Purr adds to it. Without help, they'll write their own save system or weather from scratch. Purr comes with a server for the Model Context Protocol (MCP), which assistants use to look things up and try things. With it, an assistant can:

- search Purr's guides and the class reference of every class, Purr's and Godot's, for the version you're running
- read your project: its settings, scenes and input actions
- check the project, run its tests, run a scene and read its errors, take a screenshot and measure performance
- reach the editor open on the project and the game it's running: the edited scene, playing and stopping, the game's live tree, log and monitors, and commands in its console

It's part of the editor, so there's nothing to install. An assistant starts it as `purr --path <project> --mcp`, and it talks to the assistant over standard input and output. Nothing leaves your computer.

## Setting it up

The examples run `purr`, which Homebrew puts on your path. Otherwise use the full path to Purr's executable: on a Mac, `/Applications/Purr.app/Contents/MacOS/Purr`. Replace `/path/to/game` with your project's folder, the one with `project.godot` in it.

### Claude Code

From your project's folder:

```sh
claude mcp add --scope project purr -- purr --path . --mcp
```

This writes `.mcp.json` beside your project, so everyone working on it gets the server. Claude Code asks once before using a server from that file. Written by hand, it's:

```json
{
  "mcpServers": {
    "purr": {
      "type": "stdio",
      "command": "purr",
      "args": ["--path", ".", "--mcp"]
    }
  }
}
```

### Codex

In `~/.codex/config.toml`, or `.codex/config.toml` in a project you trust:

```toml
[mcp_servers.purr]
command = "purr"
args = ["--path", "/path/to/game", "--mcp"]
cwd = "/path/to/game"
```

Or from the command line: `codex mcp add purr -- purr --path /path/to/game --mcp`.

### Antigravity

In `.agents/mcp_config.json` in your project, or in `~/.gemini/config/mcp_config.json` for every project:

```json
{
  "mcpServers": {
    "purr": {
      "command": "purr",
      "args": ["--path", "/path/to/game", "--mcp"],
      "cwd": "/path/to/game"
    }
  }
}
```

In the editor, the same file is under the agent panel's menu: **MCP Servers**, then **Manage MCP Servers**, then **View raw config**. Refresh the servers there after changing it.

### OpenCode

In `opencode.json` in your project:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "servers": {
      "purr": {
        "type": "local",
        "command": ["purr", "--path", "/path/to/game", "--mcp"],
        "cwd": "/path/to/game"
      }
    }
  }
}
```

Or from the command line: `opencode mcp add purr -- purr --path /path/to/game --mcp`.

## What it can do

| Tool | What it does |
| --- | --- |
| `search_docs`, `read_doc` | Searches and reads Purr's guides and the class reference, Purr's classes and Godot's |
| `project_info`, `project_settings`, `scene_tree` | Reads the project: its main scene, autoloads, input actions, settings and scenes |
| `check_project`, `check_script` | Finds scenes, resources and scripts that don't load or parse |
| `run_tests` | Runs the project's tests, as `purr --run-tests` does |
| `run_scene` | Runs the game or a scene for a number of frames, and returns its errors and output |
| `screenshot`, `measure` | Runs the game in a window and returns a picture of it, or its frame times, GPU time and draw calls |
| `editor_state`, `editor_scene_tree`, `editor_log` | What the open editor is doing: open scenes, the edited scene's tree, its log |
| `editor_play`, `editor_stop`, `editor_rescan`, `editor_save` | Plays and stops the game, picks up files changed outside the editor, saves the open scenes |
| `game_tree`, `game_log`, `game_monitors`, `game_screenshot` | The running game's live scene tree, log, performance monitors and window |
| `game_console`, `game_eval` | Runs a command in the game's console, or evaluates an expression in the game |

The server itself runs none of your game's code: it starts the project without its main scene or autoloads. `run_scene`, `run_tests`, `screenshot` and `measure` run the game in a separate process, just as it runs when you play it. `screenshot` and `measure` bring their window to the front while they run, because a window behind others isn't drawn or timed properly.

## The editor and the game

The `editor_` and `game_` tools work while the Purr editor has the project open. The editor listens for the server on your computer only, with a random token in the project's `.godot` folder. The `game_` tools reach the game the editor is running, through the editor's debugger, so play it from the editor (the assistant can, with `editor_play`).

`game_eval` and `game_console` can do anything the game can. They're marked as tools that change things, so most assistants ask before using them.

When an assistant edits a scene that's open in the editor, the editor still has its own copy, and saving it would undo the assistant's change. `editor_rescan` with `reload_scene` has the editor read the file again.
