def can_build(env, platform):
    env.module_add_dependencies("purr", ["purr_online", "webrtc"])
    return True


def configure(env):
    pass


def get_doc_classes():
    return ["Account", "LocalPlayers", "LocalPlayer", "Saves", "PlayerSaves", "Achievements", "Leaderboards", "Lobbies", "Friends", "Store"]


def get_doc_path():
    return "doc_classes"
