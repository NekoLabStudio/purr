def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return ["InstanceGroup3D", "LightPool3D", "MultiMeshBody3D", "ReflectionProbePool", "Scatter3D", "TimeOfDay", "WorldOrigin", "WorldStreamer"]


def get_doc_path():
    return "doc_classes"
