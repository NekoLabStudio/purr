#pragma once

// FoliageBender3D: pushes grass and plants aside round where it is, for a player walking through a
// meadow or a car driving over a verge. FoliageMaterial bends away from the nearest benders to the
// camera (up to MAX_BENDERS of them), read through the foliage_benders shader global.

#include "scene/3d/node_3d.h"
#include "scene/resources/image_texture.h"

class FoliageBender3D : public Node3D {
	GDCLASS(FoliageBender3D, Node3D);

	float radius = 0.8;
	float strength = 1.0;

	static LocalVector<FoliageBender3D *> benders;
	static Ref<ImageTexture> texture;
	static PackedByteArray last;

	static void _publish(Viewport *p_viewport);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static constexpr int MAX_BENDERS = 16;
	static constexpr const char *SHADER_GLOBAL = "foliage_benders";

	void set_radius(float p_radius);
	float get_radius() const;
	void set_strength(float p_strength);
	float get_strength() const;

	// The shader global FoliageMaterial reads, added when first needed: two texels for each bender.
	static void ensure_global();
	static void cleanup();
};
