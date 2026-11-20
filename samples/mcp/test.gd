# Checks Purr's MCP server as an AI assistant uses it: starts `purr --path . --mcp`, speaks the
# protocol to it over its stdin and stdout, and calls its tools. Then, for the live tools, opens
# this project in a headless editor, plays the game from it and asks the game. Exits non-zero if
# a check fails:
#   purr --headless --path samples/mcp -s test.gd
extends SceneTree

var failures := 0
var server: Dictionary
var next_id := 0
var pending := PackedByteArray()
var last_line := "" # The last answer as sent.
var editor: Dictionary
var editor_output := "" # The end of what the editor printed, for when it fails.


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func _initialize() -> void:
	_run.call_deferred()


# What the server prints to stderr, and the editor to either, is read as it comes: a pipe nobody
# reads fills (on Windows after a few KB), and the next print then waits for ever.
func _process(_delta: float) -> bool:
	if server.has("stderr"):
		while not server["stderr"].get_buffer(65536).is_empty():
			pass
	for pipe: String in ["stdio", "stderr"]:
		if editor.has(pipe):
			var chunk: PackedByteArray = editor[pipe].get_buffer(65536)
			while not chunk.is_empty():
				editor_output = (editor_output + chunk.get_string_from_utf8()).right(4000)
				chunk = editor[pipe].get_buffer(65536)
	return false


func project() -> String:
	return ProjectSettings.globalize_path("res://").trim_suffix("/")


# Sends a request and waits for its answer (seconds at most).
func ask(method: String, params := {}, seconds := 120.0) -> Dictionary:
	next_id += 1
	var message := { "jsonrpc": "2.0", "id": next_id, "method": method }
	if not params.is_empty():
		message["params"] = params
	server["stdio"].store_string(JSON.stringify(message) + "\n")
	server["stdio"].flush()
	var started := Time.get_ticks_msec()
	while Time.get_ticks_msec() - started < seconds * 1000.0:
		var chunk: PackedByteArray = server["stdio"].get_buffer(65536)
		pending.append_array(chunk)
		var newline := pending.find(10)
		if newline >= 0:
			var line := pending.slice(0, newline).get_string_from_utf8()
			pending = pending.slice(newline + 1)
			last_line = line
			var parsed: Variant = JSON.parse_string(line)
			if parsed is Dictionary:
				return parsed
			return { "not_json": line }
		if chunk.is_empty():
			await process_frame
	return { "timed_out": true }


func tool(name: String, arguments := {}, seconds := 120.0) -> Dictionary:
	var answer := await ask("tools/call", { "name": name, "arguments": arguments }, seconds)
	return answer.get("result", answer)


func text_of(result: Dictionary) -> String:
	var content: Array = result.get("content", [])
	return content[0].get("text", "") if not content.is_empty() else str(result)


func _run() -> void:
	server = OS.execute_with_pipe(OS.get_executable_path(), ["--path", project(), "--mcp"], false)
	check("the server starts", not server.is_empty())
	if server.is_empty():
		quit(1)
		return

	var started := await ask("initialize", { "protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": { "name": "test", "version": "1" } })
	check("it answers initialize, and nothing else is on its stdout", started.has("result"), started)
	check("with the id as sent, an integer", last_line.contains("\"id\":%d," % next_id), last_line)
	var result: Dictionary = started.get("result", {})
	check("in the version asked, with tools and instructions", result.get("protocolVersion") == "2025-11-25" and result.get("capabilities", {}).has("tools") and not str(result.get("instructions", "")).is_empty(), result)
	var meta := { "io.modelcontextprotocol/protocolVersion": "2026-07-28", "io.modelcontextprotocol/clientCapabilities": {} }
	var discovered := await ask("server/discover", { "_meta": meta })
	check("and server/discover, for the stateless protocol", discovered.get("result", {}).get("supportedVersions", []).has("2026-07-28"), discovered)
	var wrong := await ask("tools/list", { "_meta": { "io.modelcontextprotocol/protocolVersion": "1999-01-01" } })
	check("a version it doesn't know is refused", wrong.get("error", {}).get("code") == -32022, wrong)
	check("an unknown method too", (await ask("no/such")).get("error", {}).get("code") == -32601)
	check("ping", (await ask("ping")).get("result") == {})

	var tools: Array = (await ask("tools/list")).get("result", {}).get("tools", [])
	var names := tools.map(func(t: Dictionary) -> String: return t["name"])
	check("it lists its tools", names.has("search_docs") and names.has("run_tests") and names.has("editor_state") and names.has("game_eval"), names)
	check("each with a schema", tools.all(func(t: Dictionary) -> bool: return t["inputSchema"]["type"] == "object"))
	var resources: Array = (await ask("resources/list")).get("result", {}).get("resources", [])
	check("Purr's guides are resources", resources.any(func(r: Dictionary) -> bool: return r["uri"] == "purr://docs/modules/water.md"))
	var water := await ask("resources/read", { "uri": "purr://docs/modules/water.md" })
	check("that read as Markdown", str(water.get("result", {}).get("contents", [{}])[0].get("text", "")).begins_with("# Water"), water)

	var found := await tool("search_docs", { "query": "rain puddles" })
	check("search finds Purr's features", text_of(found).contains("WetSurfacesEffect") and found.get("structuredContent", {}).get("results", []).size() > 0, text_of(found))
	check("and Godot's classes", text_of(await tool("search_docs", { "query": "CharacterBody3D move_and_slide" })).contains("CharacterBody3D"))
	var read := await tool("read_doc", { "id": "class:Water3D.get_height_at" })
	check("read_doc gives a member's reference", text_of(read).contains("get_height_at(position: Vector3) -> float"), text_of(read))
	check("and a guide's section", text_of(await tool("read_doc", { "id": "guide:modules/cameras.md#shaking" })).contains("shake()"))
	check("and says so when there's none", (await tool("read_doc", { "id": "class:NoSuchClass" })).get("isError") == true)

	var info := await tool("project_info")
	check("project_info knows the project", info.get("structuredContent", {}).get("main_scene") == "res://main.tscn", info)
	var settings := await tool("project_settings", { "prefix": "application/config/name" })
	check("project_settings lists settings", text_of(settings).contains("MCP sample"), text_of(settings))
	var tree := await tool("scene_tree", { "path": "res://main.tscn", "properties": true })
	check("scene_tree reads a scene's nodes", text_of(tree).contains("Player: Node3D") and text_of(tree).contains("players") and text_of(tree).contains("position"), text_of(tree))
	var parsed := await tool("check_script", { "path": "res://main.gd" })
	check("a script that parses", text_of(parsed).contains("parses without errors"), parsed)
	check("check_project finds nothing wrong", text_of(await tool("check_project")).contains("No problems"))
	var tests := await tool("run_tests")
	check("run_tests runs the project's tests", text_of(tests).contains("1 passed") or text_of(tests).contains("passed"), text_of(tests))
	var ran := await tool("run_scene", { "frames": 30 })
	check("run_scene runs the game and gives its output", text_of(ran).contains("MCP sample started") and text_of(ran).contains("No errors"), text_of(ran))
	check("unknown tools are refused", (await ask("tools/call", { "name": "nope", "arguments": {} })).get("error", {}).get("code") == -32602)
	check("without the editor, live tools say so", (await tool("editor_state")).get("isError") == true)

	await _live()

	server["stdio"].close()
	var waited := Time.get_ticks_msec()
	while OS.is_process_running(server["pid"]) and Time.get_ticks_msec() - waited < 10000:
		await process_frame
	check("it quits when its stdin closes", not OS.is_process_running(server["pid"]))
	print("mcp test: %s" % ("all checks passed" if failures == 0 else "%d failed" % failures))
	quit(failures)


# The live tools: this project open in a headless editor, its game played from there.
func _live() -> void:
	var bridge := project().path_join(".godot/purr_mcp.json")
	DirAccess.remove_absolute(bridge)
	editor = OS.execute_with_pipe(OS.get_executable_path(), ["--headless", "--editor", "--path", project()], false)
	var waited := Time.get_ticks_msec()
	while not FileAccess.file_exists(bridge) and Time.get_ticks_msec() - waited < 120000:
		await create_timer(0.2).timeout
	check("the editor opens the bridge", FileAccess.file_exists(bridge), editor_output)
	if not FileAccess.file_exists(bridge):
		OS.kill(editor["pid"])
		return
	var state := await tool("editor_state")
	check("editor_state reaches it", not state.get("isError", true) and text_of(state).contains("Game: not running"), text_of(state))
	check("editor_log reads its log", not (await tool("editor_log")).get("isError", true))
	await tool("editor_play", { "scene": "res://main.tscn" })
	var answered := {}
	waited = Time.get_ticks_msec()
	while Time.get_ticks_msec() - waited < 180000:
		answered = await tool("game_eval", { "expression": "get_node('Main').score + 1" })
		if not answered.get("isError", true):
			break
		await create_timer(0.5).timeout
	check("game_eval evaluates in the running game", text_of(answered) == "4", text_of(answered))
	check("game_tree shows its nodes", text_of(await tool("game_tree", { "depth": 3 })).contains("Player: Node3D"))
	check("game_console runs its commands", text_of(await tool("game_console", { "command": "score" })).contains("score 3"))
	check("game_log has its prints", text_of(await tool("game_log")).contains("MCP sample started"))
	check("game_monitors has its monitors", text_of(await tool("game_monitors")).contains("time/fps"))
	await tool("editor_stop")
	OS.kill(editor["pid"])
	waited = Time.get_ticks_msec()
	while OS.is_process_running(editor["pid"]) and Time.get_ticks_msec() - waited < 10000:
		await process_frame
	DirAccess.remove_absolute(bridge)
