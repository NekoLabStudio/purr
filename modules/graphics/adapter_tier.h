#pragma once

// The tier a graphics adapter starts from, read from its name: the family's generation and class
// (an RTX 4070's generation 40 and class 70, an M4 Max's generation 4 and Max), so generations
// that come out later fall into place without a new list. Plain C++, tested on its own.

#include <string>

// The tier for the adapter's name: 0 ultra, 1 high, 2 medium, 3 low; -1 when it isn't a family
// this knows (the caller then goes by the adapter's type).
int adapter_tier(const std::string &p_name);
