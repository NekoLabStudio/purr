def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "AerialPerspectiveEffect",
        "AtmosphereSkyMaterial",
        "ChromaticAberrationEffect",
        "ColorGrade",
        "ColorGradeVolume3D",
        "DynamicResolution",
        "FilmGrainEffect",
        "FrameBudget",
        "LensFlareEffect",
        "LightShaftsEffect",
        "LocalExposureEffect",
        "MotionBlurEffect",
        "RecommendedGraphics",
        "VignetteEffect",
        "WetSurfacesEffect",
    ]


def get_doc_path():
    return "doc_classes"
