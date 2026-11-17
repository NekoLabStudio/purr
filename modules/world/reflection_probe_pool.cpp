#include "reflection_probe_pool.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"

Vector2i ReflectionProbePool::_square(const Vector3 &p_position) const {
	return Vector2i(Math::floor(p_position.x / GRID_SQUARE), Math::floor(p_position.z / GRID_SQUARE));
}

Vector3 ReflectionProbePool::_eye() const {
	Node3D *observer = ObjectDB::get_instance<Node3D>(observer_id);
	if (observer == nullptr && is_inside_tree()) {
		observer = get_viewport()->get_camera_3d();
	}
	return observer != nullptr && observer->is_inside_tree() ? to_local(observer->get_global_position()) : Vector3(Math::INF, 0, Math::INF);
}

// The probe every placed one copies: the first ReflectionProbe child, kept hidden.
ReflectionProbe *ReflectionProbePool::_template() const {
	if (ReflectionProbe *probe = ObjectDB::get_instance<ReflectionProbe>(template_id)) {
		return probe;
	}
	for (int i = 0; i < get_child_count(); i++) {
		if (ReflectionProbe *probe = Object::cast_to<ReflectionProbe>(get_child(i))) {
			return probe;
		}
	}
	return nullptr;
}

int ReflectionProbePool::add_site(const Transform3D &p_transform, const Vector3 &p_size, const Vector3 &p_origin_offset) {
	Site site;
	site.transform = p_transform;
	site.size = p_size.abs();
	site.origin_offset = p_origin_offset;
	const int id = next_id++;
	sites.insert(id, site);
	grid[_square(p_transform.origin)].push_back(id);
	look_in = 0.0;
	return id;
}

void ReflectionProbePool::remove_site(int p_id) {
	Site *site = sites.getptr(p_id);
	ERR_FAIL_NULL_MSG(site, vformat("ReflectionProbePool has no site %d.", p_id));
	_drop(p_id);
	if (Vector<int> *ids = grid.getptr(_square(site->transform.origin))) {
		ids->erase(p_id);
	}
	waiting.erase(p_id);
	stale.erase(p_id);
	sites.erase(p_id);
}

void ReflectionProbePool::clear_sites() {
	for (KeyValue<int, Site> &each : sites) {
		_drop(each.key);
	}
	sites.clear();
	grid.clear();
	waiting.clear();
	stale.clear();
}

int ReflectionProbePool::get_site_count() const {
	return sites.size();
}

PackedInt32Array ReflectionProbePool::get_placed_sites() const {
	PackedInt32Array placed;
	for (const KeyValue<int, Site> &each : sites) {
		if (each.value.probe.is_valid()) {
			placed.push_back(each.key);
		}
	}
	placed.sort();
	return placed;
}

// Every probe is captured again, one a frame: for a change of light (the hour, the weather) that
// what they reflect should show.
void ReflectionProbePool::recapture() {
	stale.clear();
	for (const KeyValue<int, Site> &each : sites) {
		if (each.value.probe.is_valid()) {
			stale.push_back(each.key);
		}
	}
}

// The nearest sites within reach get probes; the rest lose theirs.
void ReflectionProbePool::_look() {
	const Vector3 eye = _eye();
	if (!Math::is_finite(eye.x)) {
		return;
	}
	struct Found {
		float distance = 0.0;
		int id = -1;
		bool operator<(const Found &p_other) const { return distance < p_other.distance; }
	};
	Vector<Found> found;
	const Vector2i from = _square(eye - Vector3(reach, 0, reach));
	const Vector2i to = _square(eye + Vector3(reach, 0, reach));
	for (int x = from.x; x <= to.x; x++) {
		for (int z = from.y; z <= to.y; z++) {
			const Vector<int> *ids = grid.getptr(Vector2i(x, z));
			if (ids == nullptr) {
				continue;
			}
			for (int id : *ids) {
				const Vector3 at = sites[id].transform.origin;
				const float distance = at.distance_to(eye);
				if (distance <= reach && (height_reach <= 0.0 || Math::abs(at.y - eye.y) <= height_reach)) {
					found.push_back({ distance, id });
				}
			}
		}
	}
	found.sort();
	HashMap<int, bool> wanted;
	waiting.clear();
	for (int i = 0; i < MIN(count, found.size()); i++) {
		wanted.insert(found[i].id, true);
		if (!sites[found[i].id].probe.is_valid()) {
			waiting.push_back(found[i].id);
		}
	}
	for (KeyValue<int, Site> &each : sites) {
		if (each.value.probe.is_valid() && !wanted.has(each.key)) {
			_drop(each.key);
			stale.erase(each.key);
		}
	}
}

// A probe at a site, a copy of the template: Godot captures it as it joins the scene.
void ReflectionProbePool::_place(int p_id) {
	ReflectionProbe *probe_template = _template();
	Site *site = sites.getptr(p_id);
	if (probe_template == nullptr || site == nullptr) {
		return;
	}
	_drop(p_id);
	ReflectionProbe *probe = Object::cast_to<ReflectionProbe>(probe_template->duplicate());
	probe->set_transform(site->transform);
	probe->set_size(site->size);
	probe->set_origin_offset(site->origin_offset);
	probe->set_visible(true);
	add_child(probe, false, INTERNAL_MODE_BACK);
	site->probe = probe->get_instance_id();
}

void ReflectionProbePool::_drop(int p_id) {
	Site *site = sites.getptr(p_id);
	if (site == nullptr) {
		return;
	}
	if (Node *probe = ObjectDB::get_instance<Node>(site->probe)) {
		probe->queue_free();
	}
	site->probe = ObjectID();
}

void ReflectionProbePool::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (ReflectionProbe *probe_template = _template()) {
				template_id = probe_template->get_instance_id();
				probe_template->set_visible(false);
			}
			set_process_internal(!Engine::get_singleton()->is_editor_hint());
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			look_in -= get_process_delta_time();
			if (look_in <= 0.0) {
				look_in = LOOK_EVERY;
				_look();
			}
			// One capture a frame at most: a probe still to place, else one to capture again.
			if (!waiting.is_empty()) {
				const int id = waiting[0];
				waiting.remove_at(0);
				_place(id);
			} else if (!stale.is_empty()) {
				const int id = stale[0];
				stale.remove_at(0);
				_place(id);
			}
		} break;
	}
}

void ReflectionProbePool::set_count(int p_count) {
	count = MAX(p_count, 0);
	look_in = 0.0;
}

int ReflectionProbePool::get_count() const {
	return count;
}

void ReflectionProbePool::set_reach(float p_reach) {
	reach = MAX(p_reach, 0.0f);
}

float ReflectionProbePool::get_reach() const {
	return reach;
}

void ReflectionProbePool::set_height_reach(float p_reach) {
	height_reach = MAX(p_reach, 0.0f);
	look_in = 0.0;
}

float ReflectionProbePool::get_height_reach() const {
	return height_reach;
}

void ReflectionProbePool::set_observer(Node3D *p_observer) {
	observer_id = p_observer != nullptr ? p_observer->get_instance_id() : ObjectID();
	look_in = 0.0;
}

Node3D *ReflectionProbePool::get_observer() const {
	return ObjectDB::get_instance<Node3D>(observer_id);
}

PackedStringArray ReflectionProbePool::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (_template() == nullptr) {
		warnings.push_back(RTR("Add a ReflectionProbe as this node's child: the pool's probes are copies of it."));
	}
	return warnings;
}

void ReflectionProbePool::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_site", "transform", "size", "origin_offset"), &ReflectionProbePool::add_site, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("remove_site", "id"), &ReflectionProbePool::remove_site);
	ClassDB::bind_method(D_METHOD("clear_sites"), &ReflectionProbePool::clear_sites);
	ClassDB::bind_method(D_METHOD("get_site_count"), &ReflectionProbePool::get_site_count);
	ClassDB::bind_method(D_METHOD("get_placed_sites"), &ReflectionProbePool::get_placed_sites);
	ClassDB::bind_method(D_METHOD("recapture"), &ReflectionProbePool::recapture);
	ClassDB::bind_method(D_METHOD("set_count", "count"), &ReflectionProbePool::set_count);
	ClassDB::bind_method(D_METHOD("get_count"), &ReflectionProbePool::get_count);
	ClassDB::bind_method(D_METHOD("set_reach", "metres"), &ReflectionProbePool::set_reach);
	ClassDB::bind_method(D_METHOD("get_reach"), &ReflectionProbePool::get_reach);
	ClassDB::bind_method(D_METHOD("set_height_reach", "metres"), &ReflectionProbePool::set_height_reach);
	ClassDB::bind_method(D_METHOD("get_height_reach"), &ReflectionProbePool::get_height_reach);
	ClassDB::bind_method(D_METHOD("set_observer", "observer"), &ReflectionProbePool::set_observer);
	ClassDB::bind_method(D_METHOD("get_observer"), &ReflectionProbePool::get_observer);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "count", PROPERTY_HINT_RANGE, "0,64,1"), "set_count", "get_count");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:m"), "set_reach", "get_reach");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_reach", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), "set_height_reach", "get_height_reach");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "observer", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_observer", "get_observer");
}
