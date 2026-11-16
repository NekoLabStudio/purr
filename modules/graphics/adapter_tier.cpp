#include "adapter_tier.h"

#include <cctype>

namespace {

const int ULTRA = 0;
const int HIGH = 1;
const int MEDIUM = 2;
const int LOW = 3;

struct Model {
	bool found = false;
	int number = 0; // The digits after the family's name ("4070").
	int digits = 0;
	std::string rest; // What follows the digits (" Laptop GPU", "M", " Max").
};

// The model number after the family's prefix ("RTX " in "NVIDIA GeForce RTX 4070 Ti").
Model model_after(const std::string &p_name, const std::string &p_prefix) {
	Model model;
	size_t at = p_name.find(p_prefix);
	while (at != std::string::npos) {
		size_t start = at + p_prefix.size();
		size_t end = start;
		while (end < p_name.size() && std::isdigit((unsigned char)p_name[end])) {
			end++;
		}
		if (end > start && end - start <= 5) {
			model.found = true;
			model.number = std::stoi(p_name.substr(start, end - start));
			model.digits = int(end - start);
			model.rest = p_name.substr(end);
			return model;
		}
		at = p_name.find(p_prefix, at + 1);
	}
	return model;
}

bool contains(const std::string &p_text, const std::string &p_part) {
	return p_text.find(p_part) != std::string::npos;
}

// A desktop card's tier from its generation (in NVIDIA's numbering: 20, 30, 40, 50...) and class
// (50 entry, 60 mainstream, 70 upper, 80 and 90 enthusiast). Laptop parts sit a class lower.
int by_generation_and_class(int p_generation, int p_class, bool p_laptop) {
	if (p_laptop) {
		p_class -= 10;
	}
	if (p_class >= 70 && p_generation >= 40) {
		return ULTRA;
	}
	if (p_class >= 80 || (p_class >= 60 && p_generation >= 30) || (p_class >= 70 && p_generation >= 20)) {
		return HIGH;
	}
	if (p_class >= 50 || p_generation >= 40) {
		return MEDIUM;
	}
	return LOW;
}

int nvidia(const std::string &p_name) {
	Model rtx = model_after(p_name, "RTX ");
	if (rtx.found && rtx.digits == 4) {
		int generation = rtx.number / 100;
		int card_class = rtx.number % 100;
		if (card_class == 0) {
			return HIGH; // Workstation cards (RTX 4000, RTX 6000).
		}
		return by_generation_and_class(generation, card_class, contains(rtx.rest, "Laptop"));
	}
	if (contains(p_name, "RTX ")) {
		return HIGH; // Workstation cards named by letter (RTX A4000).
	}
	if (contains(p_name, "GTX ")) {
		return MEDIUM;
	}
	return -1;
}

int amd(const std::string &p_name) {
	Model rx = model_after(p_name, "RX ");
	if (!rx.found || rx.digits < 4) {
		return -1;
	}
	// The first digit is the generation: 5 to 7 (RX 7900, class 90) line up with NVIDIA's 20 to 40;
	// from 9 (RX 9070, class 70) the last two digits are the class, lining up with NVIDIA's 50 on.
	int series = rx.number / 1000;
	int generation = series >= 9 ? (series - 4) * 10 : (series - 3) * 10;
	int card_class = series >= 9 ? rx.number % 100 : (rx.number / 100 % 10) * 10;
	bool laptop = !rx.rest.empty() && (rx.rest[0] == 'M' || rx.rest[0] == 'S');
	return by_generation_and_class(generation, card_class, laptop);
}

int intel(const std::string &p_name) {
	// Discrete Arc cards (A770, B580); integrated "Arc(TM) Graphics" go by the adapter's type.
	size_t at = p_name.find("Arc(TM) ");
	if (at == std::string::npos) {
		at = p_name.find("Arc ");
		at = at == std::string::npos ? at : at - 4;
	}
	if (at == std::string::npos || at + 9 >= p_name.size()) {
		return -1;
	}
	char letter = p_name[at + 8];
	return std::isupper((unsigned char)letter) && std::isdigit((unsigned char)p_name[at + 9]) ? MEDIUM : -1;
}

int apple(const std::string &p_name) {
	Model chip = model_after(p_name, "Apple M");
	if (!chip.found) {
		return contains(p_name, "Apple A") ? LOW : -1;
	}
	// Each step up (Pro, Max, Ultra) is a tier; from the fifth generation the base chips' graphics
	// reach the earlier Pro chips'.
	int steps = 0;
	if (contains(chip.rest, "Ultra")) {
		steps = 3;
	} else if (contains(chip.rest, "Max")) {
		steps = 2;
	} else if (contains(chip.rest, "Pro")) {
		steps = 1;
	}
	if (chip.number >= 5) {
		steps++;
	}
	return steps >= 3 ? ULTRA : LOW - steps;
}

} // namespace

int adapter_tier(const std::string &p_name) {
	for (int (*family)(const std::string &) : { nvidia, amd, intel, apple }) {
		int tier = family(p_name);
		if (tier >= 0) {
			return tier;
		}
	}
	return -1;
}
