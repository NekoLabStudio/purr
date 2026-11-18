def can_build(env, platform):
    env.module_add_dependencies("options", ["graphics"])
    return True


def configure(env):
    pass


def get_doc_classes():
    return ["Options"]


def get_doc_path():
    return "doc_classes"
