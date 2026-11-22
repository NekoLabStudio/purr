def can_build(env, platform):
    env.module_add_dependencies("app_store", ["purr"])
    return platform in ("macos", "ios", "tvos", "visionos")


def configure(env):
    pass


def get_doc_classes():
    return []
