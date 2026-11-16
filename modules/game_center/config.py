def can_build(env, platform):
    return platform in ("macos", "ios", "tvos", "visionos")


def configure(env):
    pass


def get_doc_classes():
    return ["GameCenter"]


def get_doc_path():
    return "doc_classes"
