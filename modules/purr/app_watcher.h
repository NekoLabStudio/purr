#pragma once

// PurrAppWatcher: a node that hears the game go to the background, come back and close, for
// LiveConfig (fresh values on coming back) and Stats (changes sent before the game goes). Added
// once, to the root, by whichever needs it first.

#include "scene/main/node.h"

class PurrAppWatcher : public Node {
	GDCLASS(PurrAppWatcher, Node);

	static bool added;

protected:
	void _notification(int p_what);

public:
	static void ensure();
};
