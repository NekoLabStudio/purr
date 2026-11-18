// Copyright © 2023-2026 Cory Petkovsek, Roope Palmroos, and Contributors.

#ifndef CONSTANTS_CLASS_H
#define CONSTANTS_CLASS_H

#include "core/error/error_list.h"
#include "core/io/image.h"
#include "core/math/color.h"
#include "core/math/math_funcs.h"
#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "core/math/vector3.h"
#include "core/object/callable_mp.h"
#include "core/object/property_info.h"
#include "core/string/print_string.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"
#include "scene/resources/shader.h"
#include "servers/rendering/rendering_server.h"

#include <functional>

// GDExtension uses the godot namespace, custom modules do not
#if defined(GDEXTENSION) && !defined(GODOT_MODULE)
#endif

// Joins values into one message, as GDScript's print() does.
template <typename... Args>
String terrain_join(Args... p_args) {
	const Variant values[] = { Variant(p_args)... };
	String joined;
	for (const Variant &value : values) {
		joined += value.stringify();
	}
	return joined;
}

// Shader parameters as GDScript sees them: an array of property dictionaries.
inline Array terrain_shader_parameters(const RID &p_shader) {
	List<PropertyInfo> list;
	RenderingServer::get_singleton()->get_shader_parameter_list(p_shader, &list);
	return convert_property_list(&list);
}

inline Array terrain_shader_uniforms(const Ref<Shader> &p_shader) {
	List<PropertyInfo> list;
	p_shader->get_shader_uniform_list(&list, true);
	return convert_property_list(&list);
}

template <typename T>
Vector<Ref<T>> terrain_refs(const TypedArray<T> &p_array) {
	Vector<Ref<T>> refs;
	for (int i = 0; i < p_array.size(); i++) {
		refs.push_back(p_array[i]);
	}
	return refs;
}

inline int64_t terrain_floori(double p_value) {
	return int64_t(Math::floor(p_value));
}

// How much the terrain logs. Errors, warnings and messages always print in debug builds, INFO
// with --verbose. DEBUG and EXTREME are for working on the module itself.
enum TerrainLogLevel {
	MESG = -2,
	WARN = -1,
	ERROR = 0,
	INFO = 1, // Every function call and important entries.
	DEBUG = 2, // Details within functions.
	EXTREME = 3, // Continuous operations, like snapping.
};

// Engine Shortcuts
#define IS_EDITOR Engine::get_singleton()->is_editor_hint()

// Constants
static const Color COLOR_NAN{ NAN, NAN, NAN, NAN };
static const Color COLOR_WHITE{ 1.0f, 1.0f, 1.0f, 1.0f };
#define COLOR_BLACK Color(0.0f, 0.0f, 0.0f, 1.0f)
#define COLOR_ROUGHNESS Color(1.0f, 1.0f, 1.0f, 0.5f)
#define COLOR_CHECKED Color(1.f, 1.f, 1.0f, -1.0f)
#define COLOR_NORMAL Color(0.5f, 0.5f, 1.0f, 1.0f)
#define COLOR_CONTROL Color(as_float(enc_auto(true)), 0.f, 0.f, 1.0f)

// Enums

enum CompressMode {
	COMPRESS_NONE = Image::COMPRESS_MAX,
	COMPRESS_S3TC = Image::COMPRESS_S3TC,
	COMPRESS_BPTC = Image::COMPRESS_BPTC,
	COMPRESS_ETC = Image::COMPRESS_ETC,
	COMPRESS_ETC2 = Image::COMPRESS_ETC2,
	COMPRESS_ASTC = Image::COMPRESS_ASTC,
};
VARIANT_ENUM_CAST(CompressMode);

// For consistency between MSVC, gcc, clang
#ifndef FLT_MAX
#define FLT_MAX __FLT_MAX__
#endif

// Terrain3D::_warnings is uint8_t
#define WARN_MISMATCHED_SIZE 0x01
#define WARN_MISMATCHED_FORMAT 0x02
#define WARN_MISMATCHED_MIPMAPS 0x04
#define WARN_ALL 0xFF

// Global Types

#define V2(x) Vector2(x, x)
#define V2I(x) Vector2i(x, x)
#define V3(x) Vector3(x, x, x)
static const Vector2 V2_ZERO{ 0.f, 0.f };
static const Vector2 V2_MAX{ FLT_MAX, FLT_MAX };
static const Vector2i V2I_ZERO{ 0, 0 };
static const Vector2i V2I_MAX{ INT32_MAX, INT32_MAX };
static const Vector2i V2I_MIN{ INT32_MIN, INT32_MIN };
static const Vector3 V3_ZERO{ 0.f, 0.f, 0.f };
static const Vector3 V3_MAX{ FLT_MAX, FLT_MAX, FLT_MAX };
static const Vector3 V3_NAN{ NAN, NAN, NAN };
static const Vector3 V3_UP{ 0.f, 1.f, 0.f };

// Set class name for logger.h

#define CLASS_NAME() const String __class__ = String(get_class_static()) + \
		String("#") + String::num_uint64(get_instance_id()).right(4);

#define CLASS_NAME_STATIC(p_name) static inline const char *__class__ = p_name;

// Validation macros

#define ASSERT(cond, ret) \
	if (!(cond)) { \
		ERR_PRINT(terrain_join("Assertion '", #cond, "' failed at ", __FILE__, ":", __LINE__)); \
		return ret; \
	}

#define VOID // a return value for void, to avoid compiler warnings

#define IS_INIT(ret) \
	if (!_terrain) { \
		return ret; \
	}

#define IS_INIT_MESG(mesg, ret) \
	if (!_terrain) { \
		LOG(ERROR, mesg); \
		return ret; \
	}

#define IS_INIT_COND(cond, ret) \
	if (!_terrain || cond) { \
		return ret; \
	}

#define IS_INIT_COND_MESG(cond, mesg, ret) \
	if (!_terrain || cond) { \
		LOG(ERROR, mesg); \
		return ret; \
	}

#define IS_INSTANCER_INIT(ret) \
	if (!_terrain || !_terrain->get_instancer()) { \
		return ret; \
	}

#define IS_INSTANCER_INIT_MESG(mesg, ret) \
	if (!_terrain || !_terrain->get_instancer()) { \
		LOG(ERROR, mesg); \
		return ret; \
	}

#define IS_DATA_INIT(ret) \
	if (!_terrain || !_terrain->get_data()) { \
		return ret; \
	}

#define IS_DATA_INIT_MESG(mesg, ret) \
	if (!_terrain || !_terrain->get_data()) { \
		LOG(ERROR, mesg); \
		return ret; \
	}

#endif // CONSTANTS_CLASS_H