#include "light_pool_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/templates/sort_array.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"

Vector2i LightPool3D::_square(const Vector3 &p_position) const {
	return Vector2i(Math::floor(p_position.x / GRID_SQUARE), Math::floor(p_position.z / GRID_SQUARE));
}

// Where the observer is, in the pool's frame: the observer set, else the viewport's camera.
Vector3 LightPool3D::_eye() const {
	Node3D *observer = ObjectDB::get_instance<Node3D>(observer_id);
	if (observer == nullptr && is_inside_tree()) {
		observer = get_viewport()->get_camera_3d();
	}
	return observer != nullptr && observer->is_inside_tree() ? to_local(observer->get_global_position()) : Vector3(Math::INF, 0, Math::INF);
}

int LightPool3D::add_point(const Vector3 &p_position, float p_strength, const Color &p_color) {
	Point point;
	point.position = p_position;
	point.strength = MAX(p_strength, 0.0f);
	point.color = p_color;
	const int id = next_id++;
	points.insert(id, point);
	grid[_square(p_position)].push_back(id);
	refresh_in = 0.0;
	return id;
}

void LightPool3D::remove_point(int p_id) {
	const Point *point = points.getptr(p_id);
	ERR_FAIL_NULL_MSG(point, vformat("LightPool3D has no point %d.", p_id));
	if (Vector<int> *ids = grid.getptr(_square(point->position))) {
		ids->erase(p_id);
	}
	points.erase(p_id);
	// Its light lets go at once, fading as any does.
	for (Slot &slot : slots) {
		if (slot.point == p_id) {
			slot.point = -1;
			slot.target = 0.0;
		}
	}
	refresh_in = 0.0;
}

// A point's strength changed (a shop shutting: 0 lets its light go, fading as any does).
void LightPool3D::set_point_strength(int p_id, float p_strength) {
	Point *point = points.getptr(p_id);
	ERR_FAIL_NULL_MSG(point, vformat("LightPool3D has no point %d.", p_id));
	point->strength = MAX(p_strength, 0.0f);
	for (Slot &slot : slots) {
		if (slot.point == p_id) {
			if (point->strength <= 0.0) {
				slot.target = 0.0;
			} else {
				_aim(slot, p_id, 1.0, true);
			}
		}
	}
}

float LightPool3D::get_point_strength(int p_id) const {
	const Point *point = points.getptr(p_id);
	ERR_FAIL_NULL_V_MSG(point, 0.0, vformat("LightPool3D has no point %d.", p_id));
	return point->strength;
}

// A point's colour changed (a video screen's pictures): its light follows at once.
void LightPool3D::set_point_color(int p_id, const Color &p_color) {
	Point *point = points.getptr(p_id);
	ERR_FAIL_NULL_MSG(point, vformat("LightPool3D has no point %d.", p_id));
	point->color = p_color;
	for (const Slot &slot : slots) {
		if (slot.point == p_id) {
			if (Light3D *light = ObjectDB::get_instance<Light3D>(slot.light)) {
				light->set_color(template_color * p_color);
			}
		}
	}
}

Color LightPool3D::get_point_color(int p_id) const {
	const Point *point = points.getptr(p_id);
	ERR_FAIL_NULL_V_MSG(point, Color(), vformat("LightPool3D has no point %d.", p_id));
	return point->color;
}

void LightPool3D::clear_points() {
	points.clear();
	grid.clear();
	for (Slot &slot : slots) {
		slot.point = -1;
		slot.target = 0.0;
	}
}

int LightPool3D::get_point_count() const {
	return points.size();
}

int LightPool3D::get_lit_point(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, slots.size(), -1);
	return slots[p_slot].point;
}

// The lights, copies of the template: the first Light3D child, which stays hidden.
void LightPool3D::_make_lights() {
	_free_lights();
	Light3D *light_template = nullptr;
	for (int i = 0; i < get_child_count(); i++) {
		light_template = Object::cast_to<Light3D>(get_child(i));
		if (light_template != nullptr) {
			break;
		}
	}
	if (light_template == nullptr) {
		return;
	}
	template_id = light_template->get_instance_id();
	light_template->set_visible(false);
	template_energy = light_template->get_param(Light3D::PARAM_ENERGY);
	template_range = light_template->get_param(Light3D::PARAM_RANGE);
	template_color = light_template->get_color();
	for (int i = 0; i < count; i++) {
		Light3D *light = Object::cast_to<Light3D>(light_template->duplicate());
		light->set_visible(false);
		light->set_param(Light3D::PARAM_ENERGY, 0.0);
		add_child(light, false, INTERNAL_MODE_BACK);
		Slot slot;
		slot.light = light->get_instance_id();
		slots.push_back(slot);
	}
	refresh_in = 0.0;
}

void LightPool3D::_free_lights() {
	for (const Slot &slot : slots) {
		if (Node *light = ObjectDB::get_instance<Node>(slot.light)) {
			light->queue_free();
		}
	}
	slots.clear();
}

// A light on a point: there, as strong and in the colour the point says, faded by its weight.
void LightPool3D::_aim(Slot &p_slot, int p_point, float p_weight, bool p_keep_weight) {
	const Point &point = points[p_point];
	Light3D *light = ObjectDB::get_instance<Light3D>(p_slot.light);
	if (light == nullptr) {
		return;
	}
	if (p_slot.point != p_point) {
		light->set_param(Light3D::PARAM_ENERGY, 0.0); // A light moving starts dark.
		light->set_position(point.position);
		light->set_param(Light3D::PARAM_RANGE, template_range * Math::sqrt(point.strength));
		light->set_color(template_color * point.color);
		p_slot.point = p_point;
	}
	if (p_keep_weight) {
		// The same fade weight as before, at the new strength (the range too).
		const float before = p_slot.weight;
		light->set_param(Light3D::PARAM_RANGE, template_range * Math::sqrt(point.strength));
		p_slot.target = template_energy * point.strength * before;
		return;
	}
	p_slot.weight = p_weight;
	p_slot.target = template_energy * point.strength * p_weight;
}

// Ranks the points within reach, and gives the best a light each: one already on a point it keeps
// keeps it, the rest take lights now free, the darkest first (a light still fading moves last).
void LightPool3D::_refresh() {
	const Vector3 eye = _eye();
	if (!Math::is_finite(eye.x) || slots.is_empty()) {
		return;
	}
	struct Ranked {
		float rank = 0.0;
		int id = -1;
		bool operator<(const Ranked &p_other) const { return rank < p_other.rank; }
	};
	Vector<Ranked> ranked;
	const Vector2i from = _square(eye - Vector3(reach, 0, reach));
	const Vector2i to = _square(eye + Vector3(reach, 0, reach));
	for (int x = from.x; x <= to.x; x++) {
		for (int z = from.y; z <= to.y; z++) {
			const Vector<int> *ids = grid.getptr(Vector2i(x, z));
			if (ids == nullptr) {
				continue;
			}
			for (int id : *ids) {
				const Point &point = points[id];
				if (point.strength <= 0.0 || (reach_above > 0.0 && point.position.y - eye.y > reach_above)) {
					continue;
				}
				Vector3 offset = point.position - eye;
				offset.y *= height_weight;
				const float rank = offset.length() / Math::sqrt(point.strength);
				if (rank < reach) {
					ranked.push_back({ rank, id });
				}
			}
		}
	}
	ranked.sort();
	// The cut: the rank of the first point left without a light. Lights fade as they near it.
	const float cut = ranked.size() <= slots.size() ? reach : MIN(ranked[slots.size()].rank, reach);
	HashMap<int, float> picked;
	for (int i = 0; i < MIN(ranked.size(), slots.size()); i++) {
		if (ranked[i].rank < cut) {
			picked.insert(ranked[i].id, CLAMP((cut - ranked[i].rank) / (FADE * cut), 0.0f, 1.0f));
		}
	}
	Vector<int> free;
	for (int i = 0; i < slots.size(); i++) {
		Slot &slot = slots.write[i];
		if (slot.point >= 0 && picked.has(slot.point)) {
			_aim(slot, slot.point, picked[slot.point]);
			picked.erase(slot.point);
		} else {
			slot.target = 0.0;
			free.push_back(i);
		}
	}
	struct Darker {
		const Vector<Slot> *slots;
		bool operator()(int p_a, int p_b) const {
			const Light3D *a = ObjectDB::get_instance<Light3D>((*slots)[p_a].light);
			const Light3D *b = ObjectDB::get_instance<Light3D>((*slots)[p_b].light);
			return (a ? a->get_param(Light3D::PARAM_ENERGY) : 0.0f) < (b ? b->get_param(Light3D::PARAM_ENERGY) : 0.0f);
		}
	};
	SortArray<int, Darker> sorter;
	sorter.compare.slots = &slots;
	sorter.sort(free.ptrw(), free.size());
	int next_free = 0;
	for (int i = 0; i < ranked.size() && next_free < free.size(); i++) {
		const float *weight = picked.getptr(ranked[i].id);
		if (weight == nullptr) {
			continue;
		}
		_aim(slots.write[free[next_free++]], ranked[i].id, *weight);
	}
}

// Each light a step nearer its brightness, hidden once dark.
void LightPool3D::_ease(float p_delta) {
	const float step = MIN(1.0f, EASE * p_delta);
	for (Slot &slot : slots) {
		Light3D *light = ObjectDB::get_instance<Light3D>(slot.light);
		if (light == nullptr) {
			continue;
		}
		const float target = slot.target * brightness;
		const float energy = Math::lerp(light->get_param(Light3D::PARAM_ENERGY), target, step);
		light->set_param(Light3D::PARAM_ENERGY, energy);
		const bool shown = energy > 0.005 || target > 0.0;
		if (light->is_visible() != shown) {
			light->set_visible(shown);
		}
		if (!shown && slot.target <= 0.0) {
			slot.point = -1;
		}
	}
}

void LightPool3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_make_lights();
			set_process_internal(!Engine::get_singleton()->is_editor_hint());
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			const float delta = get_process_delta_time();
			refresh_in -= delta;
			if (refresh_in <= 0.0) {
				refresh_in = REFRESH_EVERY;
				_refresh();
			}
			_ease(delta);
		} break;
	}
}

void LightPool3D::set_count(int p_count) {
	count = MAX(p_count, 0);
	if (is_ready()) {
		_make_lights();
	}
}

int LightPool3D::get_count() const {
	return count;
}

void LightPool3D::set_reach(float p_reach) {
	reach = MAX(p_reach, 0.0f);
}

float LightPool3D::get_reach() const {
	return reach;
}

void LightPool3D::set_height_weight(float p_weight) {
	height_weight = MAX(p_weight, 0.0f);
}

float LightPool3D::get_height_weight() const {
	return height_weight;
}

void LightPool3D::set_reach_above(float p_reach) {
	reach_above = MAX(p_reach, 0.0f);
}

float LightPool3D::get_reach_above() const {
	return reach_above;
}

void LightPool3D::set_brightness(float p_brightness) {
	brightness = MAX(p_brightness, 0.0f);
}

float LightPool3D::get_brightness() const {
	return brightness;
}

void LightPool3D::set_observer(Node3D *p_observer) {
	observer_id = p_observer != nullptr ? p_observer->get_instance_id() : ObjectID();
}

Node3D *LightPool3D::get_observer() const {
	return ObjectDB::get_instance<Node3D>(observer_id);
}

PackedStringArray LightPool3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	bool has_template = false;
	for (int i = 0; i < get_child_count(); i++) {
		has_template = has_template || Object::cast_to<Light3D>(get_child(i)) != nullptr;
	}
	if (!has_template) {
		warnings.push_back(RTR("Add a light as this node's child (a SpotLight3D or OmniLight3D): the pool's lights are copies of it."));
	}
	return warnings;
}

void LightPool3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_point", "position", "strength", "color"), &LightPool3D::add_point, DEFVAL(1.0), DEFVAL(Color(1, 1, 1)));
	ClassDB::bind_method(D_METHOD("remove_point", "id"), &LightPool3D::remove_point);
	ClassDB::bind_method(D_METHOD("set_point_strength", "id", "strength"), &LightPool3D::set_point_strength);
	ClassDB::bind_method(D_METHOD("get_point_strength", "id"), &LightPool3D::get_point_strength);
	ClassDB::bind_method(D_METHOD("set_point_color", "id", "color"), &LightPool3D::set_point_color);
	ClassDB::bind_method(D_METHOD("get_point_color", "id"), &LightPool3D::get_point_color);
	ClassDB::bind_method(D_METHOD("clear_points"), &LightPool3D::clear_points);
	ClassDB::bind_method(D_METHOD("get_point_count"), &LightPool3D::get_point_count);
	ClassDB::bind_method(D_METHOD("get_lit_point", "light"), &LightPool3D::get_lit_point);
	ClassDB::bind_method(D_METHOD("set_count", "count"), &LightPool3D::set_count);
	ClassDB::bind_method(D_METHOD("get_count"), &LightPool3D::get_count);
	ClassDB::bind_method(D_METHOD("set_reach", "metres"), &LightPool3D::set_reach);
	ClassDB::bind_method(D_METHOD("get_reach"), &LightPool3D::get_reach);
	ClassDB::bind_method(D_METHOD("set_height_weight", "weight"), &LightPool3D::set_height_weight);
	ClassDB::bind_method(D_METHOD("get_height_weight"), &LightPool3D::get_height_weight);
	ClassDB::bind_method(D_METHOD("set_reach_above", "metres"), &LightPool3D::set_reach_above);
	ClassDB::bind_method(D_METHOD("get_reach_above"), &LightPool3D::get_reach_above);
	ClassDB::bind_method(D_METHOD("set_brightness", "brightness"), &LightPool3D::set_brightness);
	ClassDB::bind_method(D_METHOD("get_brightness"), &LightPool3D::get_brightness);
	ClassDB::bind_method(D_METHOD("set_observer", "observer"), &LightPool3D::set_observer);
	ClassDB::bind_method(D_METHOD("get_observer"), &LightPool3D::get_observer);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "count", PROPERTY_HINT_RANGE, "0,256,1"), "set_count", "get_count");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:m"), "set_reach", "get_reach");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_weight", PROPERTY_HINT_RANGE, "0,10,0.1"), "set_height_weight", "get_height_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach_above", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), "set_reach_above", "get_reach_above");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brightness", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_brightness", "get_brightness");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "observer", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_observer", "get_observer");
}
