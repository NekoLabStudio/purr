#pragma once

// A game's own button prompt images, over Purr's: "family/control" (prompt_images.py's names,
// such as "xbox/a", "keyboard/space" or "mouse/left") to a texture. Anything it leaves out
// shows Purr's own. Set it in Project Settings (input_devices/prompts/custom_set).

#include "core/io/resource.h"
#include "scene/resources/texture.h"

class InputPromptSet : public Resource {
	GDCLASS(InputPromptSet, Resource);

	Dictionary images;

protected:
	static void _bind_methods();

public:
	void set_images(const Dictionary &p_images);
	Dictionary get_images() const { return images; }
	Ref<Texture2D> get_image(const String &p_control) const;
};
