#pragma once

// What stands between a camera and what it follows, faded out so the player stays in sight: the
// meshes of each body a line from the target to the camera passes through become see-through,
// and come back once they're out of the way.

#include "core/object/object_id.h"
#include "core/templates/hash_map.h"
#include "core/math/vector3.h"

class Node3D;
class World3D;

class ObstacleFader {
	struct Fade {
		float own = 0.0; // The mesh's own transparency.
		float faded = 0.0; // 0 shown to 1 faded.
		bool blocking = false;
	};
	HashMap<ObjectID, Fade> fades;

public:
	void update(World3D *p_world, Node3D *p_target, const Vector3 &p_camera, uint32_t p_mask, float p_amount, float p_delta);
	// Everything back as it was.
	void clear();
	~ObstacleFader() { clear(); }
};
