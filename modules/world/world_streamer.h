#pragma once

// WorldStreamer: loads a large world's cells round an observer and drops them behind it. Each cell
// is a scene the game registers with its area on the map; cells near the observer load in the
// background, are prepared on a worker thread, and join the world a few nodes a frame within a
// budget, so moving never waits. Further out, a cell can show a lighter far scene instead. Cell
// scenes sit at the streamer's origin, so they're made in map coordinates.

#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"

class PackedScene;

class WorldStreamer : public Node3D {
	GDCLASS(WorldStreamer, Node3D);

	enum State {
		OUT,
		LOADING, // The scene asked of the loader.
		BUILDING, // Instantiated and prepared on a worker thread.
		JOINING, // Its nodes joining the world, a few a frame.
		IN,
	};

	// A scene instantiated and prepared on a worker thread.
	struct Build {
		Ref<PackedScene> scene;
		Callable prepare;
		String name;
		bool far = false;
		Node *result = nullptr;
		WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	};

	struct Part {
		State state = OUT;
		String scene;
		Build *build = nullptr;
		ObjectID node;
		Vector<Node *> waiting; // Its top-level children still to join.
	};

	struct Cell {
		String name;
		Rect2 area; // On the map: x and z.
		Part detail;
		Part far;
	};

	HashMap<String, Cell> cells;
	HashMap<Vector2i, Vector<String>> grid; // Cells by the grid squares their middles are in.
	float grid_size = 0.0; // The largest cell's side, so a cell's area reaches at most the squares round it.

	ObjectID observer_id;
	float load_radius = 1000.0;
	float unload_radius = 1300.0;
	float far_radius = 0.0;
	float far_unload_radius = 0.0;
	int loading_at_once = 4;
	float budget_ms = 3.0;
	Callable prepare;

	float check_in = 0.0;
	int waiting_count = 0; // Cells (and far views) wanted at the last look, not yet loading.
	Vector<String> joining; // Cells (and far views, as "name|far") joining the world, in order.
	Vector<ObjectID> leaving; // Parts gone from the world, freed a few nodes a frame.
	bool threaded = true;

	Vector3 _eye() const;
	float _distance(const Cell &p_cell, const Vector3 &p_eye) const;
	Vector<String> _near(const Vector3 &p_eye, float p_reach) const;
	void _check();
	void _request(Cell &p_cell, bool p_far);
	void _advance(Cell &p_cell, bool p_far, bool p_now);
	static void _build_task(void *p_build);
	static void _run_build(Build *p_build);
	void _settle_build(Part &p_part);
	static void _rejoin(Node *p_root, Node *p_child);
	void _join(double p_budget_usec);
	void _start_joining(Cell &p_cell, bool p_far);
	void _join_all(Cell &p_cell, bool p_far);
	void _finish_joining(Cell &p_cell, bool p_far);
	void _unload(Cell &p_cell, bool p_far);
	void _leave(double p_budget_usec);
	void _frame(double p_delta);
	Part &_part(Cell &p_cell, bool p_far) { return p_far ? p_cell.far : p_cell.detail; }

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void add_cell(const String &p_name, const Rect2 &p_area, const String &p_scene, const String &p_far_scene = String());
	void remove_cell(const String &p_name);
	void clear_cells();
	PackedStringArray get_cells() const;
	bool is_cell_loaded(const String &p_name) const;
	Node3D *get_cell_node(const String &p_name) const;
	bool is_busy() const;
	void load_around(const Vector3 &p_position, float p_radius);

	void set_observer(Node3D *p_observer);
	Node3D *get_observer() const;
	void set_load_radius(float p_radius);
	float get_load_radius() const;
	void set_unload_radius(float p_radius);
	float get_unload_radius() const;
	void set_far_radius(float p_radius);
	float get_far_radius() const;
	void set_far_unload_radius(float p_radius);
	float get_far_unload_radius() const;
	void set_loading_at_once(int p_count);
	int get_loading_at_once() const;
	void set_budget_ms(float p_ms);
	float get_budget_ms() const;
	void set_prepare(const Callable &p_prepare);
	Callable get_prepare() const;

	WorldStreamer();
	~WorldStreamer();
};
