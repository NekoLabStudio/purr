// The tiers adapters start from, by name, including generations that don't exist yet.

#include "adapter_tier.h"

#include <cstdio>

int main() {
	const char *names[] = { "ultra", "high", "medium", "low" };
	struct Case {
		const char *adapter;
		int tier;
	} cases[] = {
		{ "NVIDIA GeForce RTX 5090", 0 },
		{ "NVIDIA GeForce RTX 4070 SUPER", 0 },
		{ "NVIDIA GeForce RTX 4060 Ti", 1 },
		{ "NVIDIA GeForce RTX 4050 Laptop GPU", 2 },
		{ "NVIDIA GeForce RTX 4090 Laptop GPU", 0 },
		{ "NVIDIA GeForce RTX 3080", 1 },
		{ "NVIDIA GeForce RTX 3060 Laptop GPU", 2 },
		{ "NVIDIA GeForce RTX 2060", 2 },
		{ "NVIDIA GeForce RTX 6070", 0 }, // Not out yet.
		{ "NVIDIA GeForce RTX 6050", 2 },
		{ "NVIDIA RTX A4000", 1 },
		{ "NVIDIA RTX 6000 Ada Generation", 1 },
		{ "NVIDIA GeForce GTX 1660 SUPER", 2 },
		{ "AMD Radeon RX 9070 XT", 0 },
		{ "AMD Radeon RX 9060 XT", 1 },
		{ "AMD Radeon RX 7900 XTX", 0 },
		{ "AMD Radeon RX 7600", 1 },
		{ "AMD Radeon RX 7600M XT", 2 },
		{ "AMD Radeon RX 6700 XT", 1 },
		{ "AMD Radeon RX 5700 XT", 1 },
		{ "AMD Radeon RX 10070 XT", 0 }, // Not out yet.
		{ "AMD Radeon RX 580", -1 },
		{ "AMD Radeon(TM) Graphics", -1 },
		{ "Intel(R) Arc(TM) A770 Graphics", 2 },
		{ "Intel(R) Arc(TM) B580 Graphics", 2 },
		{ "Intel(R) Arc(TM) Graphics", -1 },
		{ "Intel(R) UHD Graphics 770", -1 },
		{ "Apple M1", 3 },
		{ "Apple M4 Pro", 2 },
		{ "Apple M4 Max (Apple9)", 1 },
		{ "Apple M2 Ultra", 0 },
		{ "Apple M5", 2 },
		{ "Apple M7 Max", 0 }, // Not out yet.
		{ "Apple A17 Pro GPU", 3 },
		{ "Adreno (TM) 740", -1 },
	};
	int failures = 0;
	for (const Case &c : cases) {
		int tier = adapter_tier(c.adapter);
		if (tier != c.tier) {
			std::printf("%s: %s, expected %s\n", c.adapter, tier < 0 ? "unknown" : names[tier], c.tier < 0 ? "unknown" : names[c.tier]);
			failures++;
		}
	}
	return failures == 0 ? 0 : 1;
}
