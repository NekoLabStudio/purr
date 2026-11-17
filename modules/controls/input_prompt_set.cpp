#include "input_prompt_set.h"

#include "core/object/class_db.h"

void InputPromptSet::set_images(const Dictionary &p_images) {
	images = p_images;
	emit_changed();
}

Ref<Texture2D> InputPromptSet::get_image(const String &p_control) const {
	return images.get(p_control, Ref<Texture2D>());
}

void InputPromptSet::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_images", "images"), &InputPromptSet::set_images);
	ClassDB::bind_method(D_METHOD("get_images"), &InputPromptSet::get_images);
	ClassDB::bind_method(D_METHOD("get_image", "control"), &InputPromptSet::get_image);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "images", PROPERTY_HINT_DICTIONARY_TYPE, "String;Texture2D"), "set_images", "get_images");
}
