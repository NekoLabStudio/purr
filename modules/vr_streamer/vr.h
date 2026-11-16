#pragma once

// VR: whether the game plays in a headset, the same on every platform. Games use Godot's OpenXR
// and the usual XR nodes; this decides, as the engine starts and before OpenXR does, whether
// OpenXR starts at all: the launch's own choice (-openxr, -novr, --xr-mode), else the player's
// (VR.enabled, saved with the game's data). On a Mac it also points OpenXR at Purr's runtime,
// and holds the player's choices for streaming to a headset from there.

#include "core/object/object.h"
#include "core/variant/dictionary.h"

class VR : public Object {
	GDCLASS(VR, Object);

	static VR *singleton;
	Dictionary choices;
	// The window's V-Sync mode before VR started: Purr's runtime paces the game, so it's off
	// while a session runs (a window held to the display's rate would hold the headset back).
	int vsync_before_vr = -1;
	void watch_xr();
	void xr_session_begun();
	void xr_session_stopping();

	static String data_dir();
	static Dictionary read_choices();
	void save_choices();
	Variant get_choice(const String &p_key, const Variant &p_default) const;
	void set_choice(const String &p_key, const Variant &p_value);

protected:
	static void _bind_methods();

public:
	static VR *get_singleton() { return singleton; }

	// Called as the engine starts, before OpenXR: decides whether OpenXR starts this run, and on
	// a Mac hands it Purr's runtime (unless the player chose a runtime: XR_RUNTIME_JSON).
	static void start_up();

	bool is_available() const;
	bool can_choose() const;
	bool is_running() const;
	void restart();

	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_eye_scale(double p_scale);
	double get_eye_scale() const;
	void set_refresh_rate(int p_hz);
	int get_refresh_rate() const;
	void set_bitrate(int p_mbps);
	int get_bitrate() const;
	void set_mute_computer(bool p_mute);
	bool get_mute_computer() const;

	VR();
	~VR();
};
