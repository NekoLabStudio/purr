#include "world_streamer.h"

#include "../graphics/frame_budget.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/packed_scene.h"
#include "servers/display/display_server.h"

namespace {
// Seconds between looks at which cells are wanted.
const float CHECK_EVERY = 0.25;
// A far view's key in the joining queue: the cell's name and this.
const String FAR_KEY = "\n far";
} // namespace

WorldStreamer::WorldStreamer() {
}

WorldStreamer::~WorldStreamer() {
	clear_cells();
}

// Where the observer is, in the streamer's frame (map coordinates, with the streamer at the
// world's origin): the observer set, else the viewport's camera.
Vector3 WorldStreamer::_eye() const {
	Node3D *observer = ObjectDB::get_instance<Node3D>(observer_id);
	if (observer == nullptr && is_inside_tree()) {
		observer = get_viewport()->get_camera_3d();
	}
	return observer != nullptr && observer->is_inside_tree() ? to_local(observer->get_global_position()) : Vector3(Math::INF, 0, Math::INF);
}

// Metres on the ground from the eye to the nearest edge of a cell's area (0 inside it).
float WorldStreamer::_distance(const Cell &p_cell, const Vector3 &p_eye) const {
	const Vector2 eye(p_eye.x, p_eye.z);
	const Vector2 nearest = eye.clamp(p_cell.area.position, p_cell.area.get_end());
	return eye.distance_to(nearest);
}

// The cells within reach of the eye, nearest first. Found from the grid of their middles: a
// large world has thousands of cells, and this runs several times a second.
Vector<String> WorldStreamer::_near(const Vector3 &p_eye, float p_reach) const {
	struct Found {
		String name;
		float distance = 0.0;
		bool operator<(const Found &p_other) const { return distance < p_other.distance; }
	};
	Vector<Found> found;
	if (grid_size <= 0.0 || !Math::is_finite(p_eye.x)) {
		return Vector<String>();
	}
	// A cell's middle is at most half its side from its area, and no side's longer than grid_size.
	const float reach = p_reach + grid_size;
	const Vector2i from((p_eye.x - reach) / grid_size - 1, (p_eye.z - reach) / grid_size - 1);
	const Vector2i to((p_eye.x + reach) / grid_size + 1, (p_eye.z + reach) / grid_size + 1);
	for (int x = from.x; x <= to.x; x++) {
		for (int z = from.y; z <= to.y; z++) {
			const Vector<String> *names = grid.getptr(Vector2i(x, z));
			if (names == nullptr) {
				continue;
			}
			for (const String &name : *names) {
				const float distance = _distance(*cells.getptr(name), p_eye);
				if (distance <= p_reach) {
					found.push_back({ name, distance });
				}
			}
		}
	}
	found.sort();
	Vector<String> names;
	for (const Found &each : found) {
		names.push_back(each.name);
	}
	return names;
}

void WorldStreamer::add_cell(const String &p_name, const Rect2 &p_area, const String &p_scene, const String &p_far_scene) {
	ERR_FAIL_COND_MSG(p_name.is_empty(), "A cell needs a name.");
	ERR_FAIL_COND_MSG(cells.has(p_name), vformat("WorldStreamer already has a cell called \"%s\".", p_name));
	Cell cell;
	cell.name = p_name;
	cell.area = p_area.abs();
	cell.detail.scene = p_scene;
	cell.far.scene = p_far_scene;
	cells.insert(p_name, cell);
	const float side = MAX(cell.area.size.x, cell.area.size.y);
	if (side > grid_size) {
		// The grid's squares grow with the largest cell: everything's put in again.
		grid_size = side;
		grid.clear();
		for (const KeyValue<String, Cell> &each : cells) {
			const Vector2 middle = each.value.area.get_center();
			grid[Vector2i(Math::floor(middle.x / grid_size), Math::floor(middle.y / grid_size))].push_back(each.key);
		}
	} else {
		const Vector2 middle = cell.area.get_center();
		grid[Vector2i(Math::floor(middle.x / grid_size), Math::floor(middle.y / grid_size))].push_back(p_name);
	}
	check_in = 0.0;
}

void WorldStreamer::remove_cell(const String &p_name) {
	Cell *cell = cells.getptr(p_name);
	ERR_FAIL_NULL_MSG(cell, vformat("WorldStreamer has no cell called \"%s\".", p_name));
	for (bool far : { false, true }) {
		Part &part = _part(*cell, far);
		if (part.state == LOADING) {
			ResourceLoader::load_threaded_get(part.scene); // Taken and dropped: the loader keeps it otherwise.
			part.state = OUT;
		} else if (part.state == BUILDING) {
			_settle_build(part);
			memdelete(part.build->result);
			memdelete(part.build);
			part.build = nullptr;
			part.state = OUT;
		}
		_unload(*cell, far);
	}
	const Vector2 middle = cell->area.get_center();
	const Vector2i square(Math::floor(middle.x / grid_size), Math::floor(middle.y / grid_size));
	if (Vector<String> *names = grid.getptr(square)) {
		names->erase(p_name);
	}
	cells.erase(p_name);
}

void WorldStreamer::clear_cells() {
	Vector<String> names;
	for (const KeyValue<String, Cell> &each : cells) {
		names.push_back(each.key);
	}
	for (const String &name : names) {
		remove_cell(name);
	}
	for (const ObjectID &id : leaving) {
		if (Node *node = ObjectDB::get_instance<Node>(id)) {
			if (node->get_parent() != nullptr) {
				node->get_parent()->remove_child(node);
			}
			memdelete(node);
		}
	}
	leaving.clear();
	grid.clear();
	grid_size = 0.0;
}

PackedStringArray WorldStreamer::get_cells() const {
	PackedStringArray names;
	for (const KeyValue<String, Cell> &each : cells) {
		names.push_back(each.key);
	}
	return names;
}

bool WorldStreamer::is_cell_loaded(const String &p_name) const {
	const Cell *cell = cells.getptr(p_name);
	return cell != nullptr && cell->detail.state == IN;
}

Node3D *WorldStreamer::get_cell_node(const String &p_name) const {
	const Cell *cell = cells.getptr(p_name);
	return cell != nullptr ? ObjectDB::get_instance<Node3D>(cell->detail.node) : nullptr;
}

bool WorldStreamer::is_busy() const {
	if (!joining.is_empty() || waiting_count > 0) {
		return true;
	}
	for (const KeyValue<String, Cell> &each : cells) {
		if (each.value.detail.state == LOADING || each.value.detail.state == BUILDING) {
			return true;
		}
	}
	return false;
}

// Loads every cell within p_radius of a point (in map coordinates) at once, before returning: for
// putting the player somewhere, behind a loading screen or a fade.
void WorldStreamer::load_around(const Vector3 &p_position, float p_radius) {
	for (const String &name : _near(p_position, p_radius)) {
		_advance(cells[name], false, true);
	}
}

void WorldStreamer::_check() {
	const Vector3 eye = _eye();
	if (!Math::is_finite(eye.x)) {
		return;
	}
	int loading = 0;
	for (const KeyValue<String, Cell> &each : cells) {
		loading += int(each.value.detail.state == LOADING || each.value.detail.state == BUILDING);
		loading += int(each.value.far.state == LOADING || each.value.far.state == BUILDING);
	}
	// What's gone too far goes; what's near comes, nearest first, a few loading at a time.
	for (KeyValue<String, Cell> &each : cells) {
		Cell &cell = each.value;
		const float distance = (cell.detail.state == JOINING || cell.detail.state == IN || cell.far.state == JOINING || cell.far.state == IN) ? _distance(cell, eye) : 0.0;
		if ((cell.detail.state == JOINING || cell.detail.state == IN) && distance > unload_radius) {
			_unload(cell, false);
		}
		if ((cell.far.state == JOINING || cell.far.state == IN) && distance > MAX(far_unload_radius, far_radius)) {
			_unload(cell, true);
		}
	}
	waiting_count = 0;
	for (const String &name : _near(eye, load_radius)) {
		Cell &cell = cells[name];
		if (cell.detail.state == OUT && !cell.detail.scene.is_empty()) {
			if (loading < loading_at_once) {
				_request(cell, false);
				loading += int(threaded);
			} else {
				waiting_count++;
			}
		}
	}
	if (far_radius > 0.0) {
		for (const String &name : _near(eye, far_radius)) {
			Cell &cell = cells[name];
			if (cell.far.state == OUT && !cell.far.scene.is_empty() && cell.detail.state != IN) {
				if (loading < loading_at_once) {
					_request(cell, true);
					loading += int(threaded);
				} else {
					waiting_count++;
				}
			}
		}
	}
}

// Asks the loader for a part's scene in the background; headless, loads it all at once, so
// tests are the same every run.
void WorldStreamer::_request(Cell &p_cell, bool p_far) {
	Part &part = _part(p_cell, p_far);
	if (!threaded) {
		_advance(p_cell, p_far, true);
		return;
	}
	if (ResourceLoader::load_threaded_request(part.scene, "PackedScene") != OK) {
		ERR_PRINT(vformat("WorldStreamer couldn't load cell \"%s\" from %s.", p_cell.name, part.scene));
		part.scene = String(); // Never asked for again.
		return;
	}
	part.state = LOADING;
}

void WorldStreamer::_build_task(void *p_build) {
	_run_build(static_cast<Build *>(p_build));
}

void WorldStreamer::_run_build(Build *p_build) {
	p_build->result = p_build->scene.is_valid() ? p_build->scene->instantiate() : nullptr;
	if (p_build->result != nullptr && p_build->prepare.is_valid()) {
		p_build->prepare.call(p_build->name, p_build->result, p_build->far);
	}
}

// Waits for a part's build (done already, when called from the frame) and takes its result.
void WorldStreamer::_settle_build(Part &p_part) {
	if (p_part.build != nullptr && p_part.build->task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(p_part.build->task);
		p_part.build->task = WorkerThreadPool::INVALID_TASK_ID;
	}
}

// Takes a part a step on (from the frame), or all the way into the world at once (p_now).
void WorldStreamer::_advance(Cell &p_cell, bool p_far, bool p_now) {
	Part &part = _part(p_cell, p_far);
	if (part.scene.is_empty() && part.state == OUT) {
		return;
	}
	if (part.state == OUT || part.state == LOADING) {
		Ref<PackedScene> scene;
		if (part.state == OUT) {
			if (!p_now) {
				return;
			}
			scene = ResourceLoader::load(part.scene, "PackedScene");
		} else {
			const ResourceLoader::ThreadLoadStatus status = ResourceLoader::load_threaded_get_status(part.scene);
			if (status == ResourceLoader::THREAD_LOAD_IN_PROGRESS && !p_now) {
				return;
			}
			scene = ResourceLoader::load_threaded_get(part.scene);
		}
		if (scene.is_null()) {
			ERR_PRINT(vformat("WorldStreamer couldn't load cell \"%s\" from %s.", p_cell.name, part.scene));
			part.scene = String();
			part.state = OUT;
			return;
		}
		part.build = memnew(Build);
		part.build->scene = scene;
		part.build->prepare = prepare;
		part.build->name = p_cell.name;
		part.build->far = p_far;
		part.state = BUILDING;
		if (p_now || !threaded) {
			_run_build(part.build);
		} else {
			part.build->task = WorkerThreadPool::get_singleton()->add_native_task(&WorldStreamer::_build_task, part.build, false, "WorldStreamer cell");
			return;
		}
	}
	if (part.state == BUILDING) {
		if (!p_now && part.build->task != WorkerThreadPool::INVALID_TASK_ID && !WorkerThreadPool::get_singleton()->is_task_completed(part.build->task)) {
			return;
		}
		_settle_build(part);
		Node *result = part.build->result;
		memdelete(part.build);
		part.build = nullptr;
		part.state = OUT;
		if (result == nullptr) {
			part.scene = String();
			return;
		}
		// Gone too far while it was built: never joins.
		const float distance = _distance(p_cell, _eye());
		if (!p_now && distance > (p_far ? MAX(far_unload_radius, far_radius) : unload_radius)) {
			memdelete(result);
			return;
		}
		// Its top-level children wait their turn to join, the root joining now. The root's not in
		// the tree yet, so they keep their owner (and unique names) as they're taken off it.
		for (int i = 0; i < result->get_child_count(); i++) {
			part.waiting.push_back(result->get_child(i));
		}
		for (Node *child : part.waiting) {
			result->remove_child(child);
		}
		add_child(result);
		part.node = result->get_instance_id();
		part.state = JOINING;
		joining.push_back(p_far ? p_cell.name + FAR_KEY : p_cell.name);
	}
	if (part.state == JOINING && p_now) {
		Node *root = ObjectDB::get_instance<Node>(part.node);
		for (Node *child : part.waiting) {
			_rejoin(root, child);
		}
		part.waiting.clear();
		joining.erase(p_far ? p_cell.name + FAR_KEY : p_cell.name);
		_finish_joining(p_cell, p_far);
	}
}

// A child taken off its cell's root goes back, with its owner (and so its unique name): set
// again once it's under the root, as Godot warns of an owner that isn't yet an ancestor.
void WorldStreamer::_rejoin(Node *p_root, Node *p_child) {
	Node *owner = p_child->get_owner();
	if (owner != nullptr) {
		p_child->set_owner(nullptr);
	}
	p_root->add_child(p_child);
	if (owner != nullptr) {
		p_child->set_owner(owner);
	}
}

// Nodes join the world one at a time in the order their cells came, within the budget (and at
// least one a frame, so a slow frame never stops streaming).
void WorldStreamer::_join(double p_budget_usec) {
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	bool first = true;
	while (!joining.is_empty() && (first || OS::get_singleton()->get_ticks_usec() - started < p_budget_usec)) {
		first = false;
		const String key = joining[0];
		const bool far = key.ends_with(FAR_KEY);
		Cell *cell = cells.getptr(far ? key.trim_suffix(FAR_KEY) : key);
		Part *part = cell != nullptr ? &_part(*cell, far) : nullptr;
		Node *root = part != nullptr ? ObjectDB::get_instance<Node>(part->node) : nullptr;
		if (part == nullptr || part->state != JOINING || root == nullptr) {
			joining.remove_at(0);
			continue;
		}
		if (!part->waiting.is_empty()) {
			Node *child = part->waiting[0];
			part->waiting.remove_at(0);
			_rejoin(root, child);
		}
		if (part->waiting.is_empty()) {
			joining.remove_at(0);
			_finish_joining(*cell, far);
		}
	}
}

// All of it in: a far view shows only where its cell's detail isn't.
void WorldStreamer::_finish_joining(Cell &p_cell, bool p_far) {
	Part &part = _part(p_cell, p_far);
	part.state = IN;
	Node3D *far = ObjectDB::get_instance<Node3D>(p_cell.far.node);
	if (far != nullptr && p_cell.far.state == IN) {
		far->set_visible(p_cell.detail.state != IN);
	}
	emit_signal(SNAME("cell_loaded"), p_cell.name, ObjectDB::get_instance<Node>(part.node), p_far);
}

// Out of the world: hidden at once, freed a few nodes a frame.
void WorldStreamer::_unload(Cell &p_cell, bool p_far) {
	Part &part = _part(p_cell, p_far);
	if (part.state != JOINING && part.state != IN) {
		return;
	}
	Node *root = ObjectDB::get_instance<Node>(part.node);
	if (root != nullptr) {
		emit_signal(SNAME("cell_unloading"), p_cell.name, root, p_far);
	}
	for (Node *child : part.waiting) {
		memdelete(child); // Never joined.
	}
	part.waiting.clear();
	joining.erase(p_far ? p_cell.name + FAR_KEY : p_cell.name);
	if (Node3D *spatial = Object::cast_to<Node3D>(root)) {
		spatial->set_visible(false);
	}
	if (root != nullptr) {
		root->set_process_mode(PROCESS_MODE_DISABLED);
		leaving.push_back(part.node);
	}
	part.node = ObjectID();
	part.state = OUT;
	// The detail gone, its far view shows again.
	Node3D *far = ObjectDB::get_instance<Node3D>(p_cell.far.node);
	if (!p_far && far != nullptr && p_cell.far.state == IN) {
		far->set_visible(true);
	}
}

void WorldStreamer::_free_leaving(double p_budget_usec) {
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	bool first = true;
	while (!leaving.is_empty() && (first || OS::get_singleton()->get_ticks_usec() - started < p_budget_usec)) {
		first = false;
		Node *root = ObjectDB::get_instance<Node>(leaving[0]);
		if (root == nullptr) {
			leaving.remove_at(0);
			continue;
		}
		if (root->get_child_count() > 0) {
			Node *child = root->get_child(root->get_child_count() - 1);
			root->remove_child(child);
			memdelete(child);
			continue;
		}
		if (root->get_parent() != nullptr) {
			root->get_parent()->remove_child(root);
		}
		memdelete(root);
		leaving.remove_at(0);
	}
}

void WorldStreamer::_frame(double p_delta) {
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	check_in -= p_delta;
	if (check_in <= 0.0) {
		check_in = CHECK_EVERY;
		_check();
	}
	for (KeyValue<String, Cell> &each : cells) {
		for (bool far : { false, true }) {
			const State state = _part(each.value, far).state;
			if (state == LOADING || state == BUILDING) {
				_advance(each.value, far, false);
			}
		}
	}
	FrameBudget *frame_budget = FrameBudget::get_singleton();
	const double times = frame_budget != nullptr && frame_budget->is_screen_hidden() ? FrameBudget::HIDDEN_TIMES : 1.0;
	_join(budget_ms * times * 1000.0);
	_free_leaving(budget_ms * times * 500.0);
	if (frame_budget != nullptr) {
		frame_budget->measure("world streaming", started);
	}
}

void WorldStreamer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// Headless, everything loads on the main thread: Godot's stand-in renderer isn't safe to
			// fill from two threads, and tests run the same every time.
			threaded = DisplayServer::get_singleton() == nullptr || DisplayServer::get_singleton()->get_name() != "headless";
			set_process_internal(!Engine::get_singleton()->is_editor_hint());
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_frame(get_process_delta_time());
		} break;
	}
}

void WorldStreamer::set_observer(Node3D *p_observer) {
	observer_id = p_observer != nullptr ? p_observer->get_instance_id() : ObjectID();
	check_in = 0.0;
}

Node3D *WorldStreamer::get_observer() const {
	return ObjectDB::get_instance<Node3D>(observer_id);
}

void WorldStreamer::set_load_radius(float p_radius) {
	load_radius = MAX(p_radius, 0.0f);
}

float WorldStreamer::get_load_radius() const {
	return load_radius;
}

void WorldStreamer::set_unload_radius(float p_radius) {
	unload_radius = MAX(p_radius, 0.0f);
}

float WorldStreamer::get_unload_radius() const {
	return unload_radius;
}

void WorldStreamer::set_far_radius(float p_radius) {
	far_radius = MAX(p_radius, 0.0f);
}

float WorldStreamer::get_far_radius() const {
	return far_radius;
}

void WorldStreamer::set_far_unload_radius(float p_radius) {
	far_unload_radius = MAX(p_radius, 0.0f);
}

float WorldStreamer::get_far_unload_radius() const {
	return far_unload_radius;
}

void WorldStreamer::set_loading_at_once(int p_count) {
	loading_at_once = MAX(p_count, 1);
}

int WorldStreamer::get_loading_at_once() const {
	return loading_at_once;
}

void WorldStreamer::set_budget_ms(float p_ms) {
	budget_ms = MAX(p_ms, 0.1f);
}

float WorldStreamer::get_budget_ms() const {
	return budget_ms;
}

void WorldStreamer::set_prepare(const Callable &p_prepare) {
	prepare = p_prepare;
}

Callable WorldStreamer::get_prepare() const {
	return prepare;
}

void WorldStreamer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_cell", "name", "area", "scene", "far_scene"), &WorldStreamer::add_cell, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("remove_cell", "name"), &WorldStreamer::remove_cell);
	ClassDB::bind_method(D_METHOD("clear_cells"), &WorldStreamer::clear_cells);
	ClassDB::bind_method(D_METHOD("get_cells"), &WorldStreamer::get_cells);
	ClassDB::bind_method(D_METHOD("is_cell_loaded", "name"), &WorldStreamer::is_cell_loaded);
	ClassDB::bind_method(D_METHOD("get_cell_node", "name"), &WorldStreamer::get_cell_node);
	ClassDB::bind_method(D_METHOD("is_busy"), &WorldStreamer::is_busy);
	ClassDB::bind_method(D_METHOD("load_around", "position", "radius"), &WorldStreamer::load_around);
	ClassDB::bind_method(D_METHOD("set_observer", "observer"), &WorldStreamer::set_observer);
	ClassDB::bind_method(D_METHOD("get_observer"), &WorldStreamer::get_observer);
	ClassDB::bind_method(D_METHOD("set_load_radius", "metres"), &WorldStreamer::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"), &WorldStreamer::get_load_radius);
	ClassDB::bind_method(D_METHOD("set_unload_radius", "metres"), &WorldStreamer::set_unload_radius);
	ClassDB::bind_method(D_METHOD("get_unload_radius"), &WorldStreamer::get_unload_radius);
	ClassDB::bind_method(D_METHOD("set_far_radius", "metres"), &WorldStreamer::set_far_radius);
	ClassDB::bind_method(D_METHOD("get_far_radius"), &WorldStreamer::get_far_radius);
	ClassDB::bind_method(D_METHOD("set_far_unload_radius", "metres"), &WorldStreamer::set_far_unload_radius);
	ClassDB::bind_method(D_METHOD("get_far_unload_radius"), &WorldStreamer::get_far_unload_radius);
	ClassDB::bind_method(D_METHOD("set_loading_at_once", "count"), &WorldStreamer::set_loading_at_once);
	ClassDB::bind_method(D_METHOD("get_loading_at_once"), &WorldStreamer::get_loading_at_once);
	ClassDB::bind_method(D_METHOD("set_budget_ms", "milliseconds"), &WorldStreamer::set_budget_ms);
	ClassDB::bind_method(D_METHOD("get_budget_ms"), &WorldStreamer::get_budget_ms);
	ClassDB::bind_method(D_METHOD("set_prepare", "prepare"), &WorldStreamer::set_prepare);
	ClassDB::bind_method(D_METHOD("get_prepare"), &WorldStreamer::get_prepare);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "observer", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_observer", "get_observer");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "load_radius", PROPERTY_HINT_RANGE, "0,100000,1,or_greater,suffix:m"), "set_load_radius", "get_load_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unload_radius", PROPERTY_HINT_RANGE, "0,100000,1,or_greater,suffix:m"), "set_unload_radius", "get_unload_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far_radius", PROPERTY_HINT_RANGE, "0,100000,1,or_greater,suffix:m"), "set_far_radius", "get_far_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far_unload_radius", PROPERTY_HINT_RANGE, "0,100000,1,or_greater,suffix:m"), "set_far_unload_radius", "get_far_unload_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "loading_at_once", PROPERTY_HINT_RANGE, "1,32,1"), "set_loading_at_once", "get_loading_at_once");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "budget_ms", PROPERTY_HINT_RANGE, "0.1,50,0.1,suffix:ms"), "set_budget_ms", "get_budget_ms");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "prepare", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_prepare", "get_prepare");

	ADD_SIGNAL(MethodInfo("cell_loaded", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_NODE_TYPE, "Node"), PropertyInfo(Variant::BOOL, "far")));
	ADD_SIGNAL(MethodInfo("cell_unloading", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_NODE_TYPE, "Node"), PropertyInfo(Variant::BOOL, "far")));
}
