def can_build(env, platform):
    # The web uses the browser's WebRTC.
    if platform == "web":
        return False
    env.module_add_dependencies("webrtc_native", ["webrtc", "mbedtls"])
    return True


def configure(env):
    pass


def get_doc_classes():
    return ["WebRTCNativePeerConnection"]


def get_doc_path():
    return "doc_classes"
