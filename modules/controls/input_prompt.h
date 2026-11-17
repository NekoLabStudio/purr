#pragma once

// InputPrompt: shows what to press for an action, for a player, on the controller they're
// using: its image, or its name where it has none ("Gear 3"). It follows the player as they
// pick up another controller or change their bindings.

#include "scene/gui/control.h"
#include "scene/resources/texture.h"

class InputPrompt : public Control {
	GDCLASS(InputPrompt, Control);

	StringName action;
	int player = 0;
	Dictionary binding;
	int height = 0;

	Ref<Texture2D> image;
	String text;

	void refresh();
	void player_family_changed(int p_player, int p_family);
	void bindings_changed(int p_player);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_action(const StringName &p_action);
	StringName get_action() const { return action; }
	void set_player(int p_player);
	int get_player() const { return player; }
	void set_binding(const Dictionary &p_binding);
	Dictionary get_binding() const { return binding; }
	void set_height(int p_height);
	int get_height() const { return height; }

	virtual Size2 get_minimum_size() const override;

	InputPrompt();
};
