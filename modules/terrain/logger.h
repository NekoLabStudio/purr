// Copyright © 2023-2026 Cory Petkovsek, Roope Palmroos, and Contributors.

#ifndef LOGGER_CLASS_H
#define LOGGER_CLASS_H

#include "core/os/os.h"

#include "constants.h"

// Prints errors and warnings through Godot's error macros, and messages up to the current level
// (see TerrainLogLevel). Nothing prints in release builds.

inline TerrainLogLevel terrain_log_level() {
	return OS::get_singleton()->is_stdout_verbose() ? INFO : ERROR;
}

#ifdef DEBUG_ENABLED
#define LOG(level, ...) \
	do { \
		if (level == ERROR) \
			ERR_PRINT(terrain_join(__VA_ARGS__)); \
		else if (level == WARN) \
			WARN_PRINT(terrain_join(__VA_ARGS__)); \
		else if (level <= terrain_log_level()) \
			print_line(terrain_join(__class__, ": ", __VA_ARGS__)); \
	} while (false);
#else
#define LOG(...)
#endif

#endif // LOGGER_CLASS_H
