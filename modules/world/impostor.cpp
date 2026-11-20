#include "impostor.h"

#include "core/io/image.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/image_texture.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

#include "impostor_material.h"

namespace {
void complete_bake(const Ref<ImpostorBake> &p_bake, const Dictionary &p_result) {
	p_bake->emit_signal(SNAME("completed"), p_result);
}

Dictionary failure(const String &p_error, const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["message"] = p_message;
	return result;
}

// The basis ImpostorMaterial turns its quad to, looking from p_towards: the bake's camera matches it.
Basis facing(const Vector3 &p_towards) {
	const Vector3 right = (Vector3(0, 1, 0).cross(p_towards) + Vector3(1e-5, 0, 0)).normalized();
	const Vector3 up = p_towards.cross(right);
	return Basis(right, up, p_towards);
}
} // namespace

// The direction a cell of the hemi-octahedral grid was baked from (towards the camera).
Vector3 Impostor::_direction(int p_grid, int p_cell) {
	const Vector2 uv = (Vector2(p_cell % p_grid, p_cell / p_grid) + Vector2(0.5, 0.5)) / float(p_grid);
	const Vector2 p = uv * 2.0 - Vector2(1, 1);
	const float x = (p.x + p.y) * 0.5f;
	const float z = (p.x - p.y) * 0.5f;
	return Vector3(x, 1.0f - Math::abs(x) - Math::abs(z), z).normalized();
}

Signal Impostor::bake(Node3D *p_node, int p_grid, int p_tile_size) {
	Ref<ImpostorBake> job;
	job.instantiate();
	const Signal done(job.ptr(), SNAME("completed"));
	if (p_node == nullptr || !p_node->is_inside_tree()) {
		callable_mp_static(&complete_bake).call_deferred(job, failure("not_in_tree", "Bake a node that's in the scene tree."));
		return done;
	}
	if (DisplayServer::get_singleton() == nullptr || DisplayServer::get_singleton()->get_name() == "headless") {
		callable_mp_static(&complete_bake).call_deferred(job, failure("no_renderer", "Impostors are baked by rendering: not headless."));
		return done;
	}
	// How big the node is: everything visible under it, in its own space.
	AABB bounds;
	bool any = false;
	const Transform3D to_node = p_node->get_global_transform().affine_inverse();
	LocalVector<Node *> pending;
	pending.push_back(p_node);
	while (!pending.is_empty()) {
		Node *at = pending[pending.size() - 1];
		pending.resize(pending.size() - 1);
		if (VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(at)) {
			const AABB box = (to_node * visual->get_global_transform()).xform(visual->get_aabb());
			bounds = any ? bounds.merge(box) : box;
			any = true;
		}
		for (int i = 0; i < at->get_child_count(); i++) {
			pending.push_back(at->get_child(i));
		}
	}
	if (!any) {
		callable_mp_static(&complete_bake).call_deferred(job, failure("nothing_to_bake", "There's nothing visible under the node."));
		return done;
	}
	center = bounds.get_center();
	radius = MAX(bounds.size.length() * 0.5f, 0.001f);
	grid = CLAMP(p_grid, 1, 32);

	job->impostor = this;
	job->running = job;
	job->grid = grid;
	job->tile = CLAMP(p_tile_size, 16, 1024);
	job->atlas = Image::create_empty(job->grid * job->tile, job->grid * job->tile, false, Image::FORMAT_RGBA8);
	// A world of the node's own, drawn without lighting: its colours as they are.
	job->view = memnew(SubViewport);
	job->view->set_size(Size2i(job->tile, job->tile));
	job->view->set_use_own_world_3d(true);
	job->view->set_transparent_background(true);
	job->view->set_debug_draw(Viewport::DEBUG_DRAW_UNSHADED);
	job->view->set_msaa_3d(Viewport::MSAA_4X);
	job->view->set_update_mode(SubViewport::UPDATE_ALWAYS);
	Node3D *copy = Object::cast_to<Node3D>(p_node->duplicate(0));
	copy->set_transform(Transform3D());
	job->view->add_child(copy);
	job->camera = memnew(Camera3D);
	job->camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
	job->camera->set_size(radius * 2.0);
	job->camera->set_near(0.01);
	job->camera->set_far(radius * 6.0);
	job->view->add_child(job->camera);
	job->camera->make_current();
	SceneTree::get_singleton()->get_root()->add_child(job->view, false, Node::INTERNAL_MODE_BACK);
	job->_place_camera();
	RS::get_singleton()->connect(SNAME("frame_post_draw"), callable_mp(job.ptr(), &ImpostorBake::_frame_drawn));
	return done;
}

void ImpostorBake::_place_camera() {
	const Vector3 towards = Impostor::_direction(grid, cell);
	camera->set_global_transform(Transform3D(facing(towards), impostor->center + towards * impostor->radius * 3.0));
}

// Each view takes a frame to be drawn after the camera moves, then is copied into its cell.
void ImpostorBake::_frame_drawn() {
	if (++waited < 2) {
		return;
	}
	waited = 0;
	const Ref<Image> image = view->get_texture()->get_image();
	if (image.is_null()) {
		_finish(failure("not_drawn", "The impostor's views couldn't be read back."));
		return;
	}
	image->convert(Image::FORMAT_RGBA8);
	atlas->blit_rect(image, Rect2i(0, 0, tile, tile), Point2i((cell % grid) * tile, (cell / grid) * tile));
	if (++cell < grid * grid) {
		_place_camera();
		return;
	}
	atlas->generate_mipmaps();
	impostor->texture = ImageTexture::create_from_image(atlas);
	impostor->mesh.unref(); // Made again with the new texture.
	impostor->emit_changed();
	Dictionary result;
	result["ok"] = true;
	_finish(result);
}

void ImpostorBake::_finish(const Dictionary &p_result) {
	RS::get_singleton()->disconnect(SNAME("frame_post_draw"), callable_mp(this, &ImpostorBake::_frame_drawn));
	view->queue_free();
	view = nullptr;
	Ref<ImpostorBake> self = this; // Held until the result is out.
	running.unref();
	callable_mp_static(&complete_bake).call_deferred(self, p_result);
	impostor.unref();
}

void ImpostorBake::_bind_methods() {
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
}

Ref<Mesh> Impostor::get_mesh() const {
	if (mesh.is_null()) {
		material.instantiate();
		material->set_texture(texture);
		material->set_grid(grid);
		material->set_center(center);
		material->set_radius(radius);
		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Size2(2, 2));
		quad->set_material(material);
		// Culled as the node would be: the quad turns, so its bounds are the node's sphere.
		quad->set_custom_aabb(AABB(center - Vector3(radius, radius, radius), Vector3(radius, radius, radius) * 2.0));
		mesh = quad;
	}
	return mesh;
}

void Impostor::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	mesh.unref();
	emit_changed();
}

Ref<Texture2D> Impostor::get_texture() const {
	return texture;
}

void Impostor::set_grid(int p_grid) {
	grid = CLAMP(p_grid, 1, 32);
	mesh.unref();
	emit_changed();
}

int Impostor::get_grid() const {
	return grid;
}

void Impostor::set_center(const Vector3 &p_center) {
	center = p_center;
	mesh.unref();
	emit_changed();
}

Vector3 Impostor::get_center() const {
	return center;
}

void Impostor::set_radius(float p_radius) {
	radius = MAX(p_radius, 0.001f);
	mesh.unref();
	emit_changed();
}

float Impostor::get_radius() const {
	return radius;
}

void Impostor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("bake", "node", "grid", "tile_size"), &Impostor::bake, DEFVAL(8), DEFVAL(128));
	ClassDB::bind_method(D_METHOD("get_mesh"), &Impostor::get_mesh);
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &Impostor::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &Impostor::get_texture);
	ClassDB::bind_method(D_METHOD("set_grid", "grid"), &Impostor::set_grid);
	ClassDB::bind_method(D_METHOD("get_grid"), &Impostor::get_grid);
	ClassDB::bind_method(D_METHOD("set_center", "center"), &Impostor::set_center);
	ClassDB::bind_method(D_METHOD("get_center"), &Impostor::get_center);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &Impostor::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &Impostor::get_radius);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grid", PROPERTY_HINT_RANGE, "1,32,1"), "set_grid", "get_grid");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center", PROPERTY_HINT_NONE, "suffix:m"), "set_center", "get_center");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.001,100,0.001,or_greater,suffix:m"), "set_radius", "get_radius");
}
