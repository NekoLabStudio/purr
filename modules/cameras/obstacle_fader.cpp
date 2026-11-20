#include "obstacle_fader.h"

#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace {
constexpr int MOST_BLOCKERS = 8;
constexpr float FADE_TIME = 0.2; // Seconds to fade out or back in.

// A body's meshes: those under it, and the mesh it's under when it's a mesh's collision.
void meshes_of(Node *p_body, LocalVector<GeometryInstance3D *> &r_meshes) {
	if (GeometryInstance3D *parent = Object::cast_to<GeometryInstance3D>(p_body->get_parent())) {
		r_meshes.push_back(parent);
	}
	LocalVector<Node *> pending;
	pending.push_back(p_body);
	while (!pending.is_empty()) {
		Node *node = pending[pending.size() - 1];
		pending.resize(pending.size() - 1);
		if (GeometryInstance3D *mesh = Object::cast_to<GeometryInstance3D>(node)) {
			r_meshes.push_back(mesh);
		}
		for (int i = 0; i < node->get_child_count(); i++) {
			pending.push_back(node->get_child(i));
		}
	}
}
} // namespace

void ObstacleFader::update(World3D *p_world, Node3D *p_target, const Vector3 &p_camera, uint32_t p_mask, float p_amount, float p_delta) {
	for (KeyValue<ObjectID, Fade> &fade : fades) {
		fade.value.blocking = false;
	}
	PhysicsDirectSpaceState3D *space = p_world != nullptr ? p_world->get_direct_space_state() : nullptr;
	if (space != nullptr && p_target != nullptr) {
		PhysicsServer3DTypes::RayParameters ray;
		ray.from = p_target->get_global_transform_interpolated().origin;
		ray.to = p_camera;
		ray.collision_mask = p_mask;
		// The target's own body isn't in its way.
		for (Node *node = p_target; node != nullptr; node = node->get_parent()) {
			if (CollisionObject3D *own = Object::cast_to<CollisionObject3D>(node)) {
				ray.exclude.insert(own->get_rid());
			}
		}
		for (int i = 0; i < MOST_BLOCKERS; i++) {
			PhysicsServer3DTypes::RayResult hit;
			if (!space->intersect_ray(ray, hit)) {
				break;
			}
			ray.exclude.insert(hit.rid);
			Node *body = Object::cast_to<Node>(ObjectDB::get_instance(hit.collider_id));
			if (body == nullptr) {
				continue;
			}
			LocalVector<GeometryInstance3D *> meshes;
			meshes_of(body, meshes);
			for (GeometryInstance3D *mesh : meshes) {
				Fade *fade = fades.getptr(mesh->get_instance_id());
				if (fade == nullptr) {
					fade = &fades.insert(mesh->get_instance_id(), Fade())->value;
					fade->own = mesh->get_transparency();
				}
				fade->blocking = true;
			}
		}
	}
	const float step = p_delta / FADE_TIME;
	LocalVector<ObjectID> done;
	for (KeyValue<ObjectID, Fade> &entry : fades) {
		Fade &fade = entry.value;
		fade.faded = Math::move_toward(fade.faded, fade.blocking ? 1.0f : 0.0f, step);
		GeometryInstance3D *mesh = Object::cast_to<GeometryInstance3D>(ObjectDB::get_instance(entry.key));
		if (mesh == nullptr) {
			done.push_back(entry.key);
			continue;
		}
		mesh->set_transparency(Math::lerp(fade.own, MAX(fade.own, p_amount), fade.faded));
		if (!fade.blocking && fade.faded <= 0.0f) {
			mesh->set_transparency(fade.own);
			done.push_back(entry.key);
		}
	}
	for (const ObjectID &id : done) {
		fades.erase(id);
	}
}

void ObstacleFader::clear() {
	for (const KeyValue<ObjectID, Fade> &entry : fades) {
		GeometryInstance3D *mesh = Object::cast_to<GeometryInstance3D>(ObjectDB::get_instance(entry.key));
		if (mesh != nullptr) {
			mesh->set_transparency(entry.value.own);
		}
	}
	fades.clear();
}
