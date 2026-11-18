def can_build(env, platform):
    env.module_add_dependencies("debug_tools", ["graphics"])
    return True


def configure(env):
    pass


def get_doc_classes():
    return ["Console", "DebugDraw"]


def get_doc_path():
    return "doc_classes"
