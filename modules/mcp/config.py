def can_build(env, platform):
    # An editor tool: exported games don't carry it.
    env.module_add_dependencies("mcp", ["graphics", "debug_tools", "testing"])
    return env.editor_build


def configure(env):
    pass
