# A small scene for the MCP server's tools to look at: a node or two, a console command and a print.
extends Node3D

var score := 3


func _ready() -> void:
	Console.add_command("score", func() -> String: return "score %d" % score, "Prints the score.")
	print("MCP sample started")
