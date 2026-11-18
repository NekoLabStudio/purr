#pragma once

// Packs a terrain texture's separate height and roughness images as the terrain draws them: height
// in the albedo's alpha channel, roughness in the normal map's. The results are PNG files beside the
// albedo and normal textures, imported compressed like the rest, which the texture then draws.

class Terrain3DTextureAsset;

class TerrainTexturePacker {
public:
	static void pack(Terrain3DTextureAsset *p_asset);
};
