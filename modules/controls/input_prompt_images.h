#pragma once

// Button prompt images: the game's own (an InputPromptSet in Project Settings), else Purr's,
// in the project's style: Kenney's vector prompts drawn at the size asked for, or Kenney's
// 16 by 16 pixel ones (for pixel art, drawn with nearest filtering). Made once, then kept.

#include "input_prompt_set.h"

#include "core/templates/hash_map.h"
#include "scene/resources/texture.h"

class InputPromptImages {
	HashMap<String, Ref<Texture2D>> made;
	Ref<Image> pixel_sheet;
	Ref<InputPromptSet> custom;
	bool custom_loaded = false;

	Ref<Texture2D> standard(const String &p_control, int p_size);
	Ref<Texture2D> pixel(const String &p_control);
	void load_custom();

public:
	enum Style {
		STYLE_STANDARD,
		STYLE_PIXEL,
	};

	Style style() const;
	// The image for "family/control" ("xbox/a"), at p_size pixels high in the standard style
	// (the pixel style is always 16); null if neither the game nor Purr has one.
	Ref<Texture2D> get(const String &p_control, int p_size);
	bool has(const String &p_control);
	void clear() { made.clear(); }
};
