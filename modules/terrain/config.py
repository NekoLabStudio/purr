def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "Terrain3D",
        "Terrain3DAssetResource",
        "Terrain3DAssets",
        "Terrain3DCollision",
        "Terrain3DData",
        "Terrain3DInstancer",
        "Terrain3DMaterial",
        "Terrain3DMeshAsset",
        "Terrain3DRegion",
        "Terrain3DTextureAsset",
        "Terrain3DUtil",
    ]


def get_doc_path():
    return "doc_classes"
