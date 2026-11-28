#pragma once
// The client's copy of the Tower Defense store, as the Smoke extension's
// sixth sub packet carries it. Kept here, header-only and free of server
// types, so Client.h and the store UI can both name it. The byte layout must
// stay in step with proto::write_store() in Training/Protocol.cpp.

#include <cstdint>

namespace spades {
namespace client {

struct StoreState {
	std::uint16_t zedcreds = 0;
	std::uint8_t wave = 0;
	std::uint8_t phase = 0;
	std::uint16_t coreHp = 0;
	std::uint16_t coreMaxHp = 0;
	std::uint8_t weapon = 0;
	std::uint8_t grenades = 0;
	std::uint8_t blocks = 0;

	std::uint8_t ownedCount = 0;
	struct Owned {
		std::uint8_t index = 0;
		std::uint8_t level = 0;
	};
	Owned owned[64] = {};

	std::uint8_t priceCount = 0;
	struct Priced {
		std::uint8_t index = 0;
		std::uint16_t price = 0;
	} priced[64] = {};

	/** The price the server charges an item, by its StoreCatalog index,
	 * or -1 when the wire has said none of it and the shelf falls back
	 * to its own words. */
	std::int32_t price_of(int index) const {
		for (std::uint8_t k = 0; k < priceCount; ++k)
			if (priced[k].index == static_cast<std::uint8_t>(index))
				return static_cast<std::int32_t>(priced[k].price);
		return -1;
	}

	/** The core's own cell, as the store's word names it: a ground of
	 * zero is a world with no core to guard. */
	std::uint16_t coreX = 0;
	std::uint16_t coreY = 0;
	std::uint8_t coreTopZ = 0;
	std::uint8_t coreGroundZ = 0;

	/** The marker's ground: the sphere of five the server holds off
	 * every hand - the same zone the server's kCoreZoneRadius draws.
	 * True only in a Tower Defense world whose word named the core,
	 * and only for a cell the server would refuse to spend. */
	bool core_zone(int x, int y, int z) const {
		if (coreGroundZ == 0) return false;
		const int dx = x - coreX;
		const int dy = y - coreY;
		int dz = 0;
		if (z < coreTopZ) dz = coreTopZ - z;
		const int bottom = coreGroundZ - 1;
		if (z > bottom) dz = z - bottom;
		return dx * dx + dy * dy + dz * dz <= 25;
	}

	/** Whether a server that speaks the store has been heard from at all. */
	bool received = false;

	/** Times bought of an item, by its StoreCatalog index. */
	std::uint8_t level_of(int index) const {
		for (std::uint8_t k = 0; k < ownedCount; ++k)
			if (owned[k].index == static_cast<std::uint8_t>(index))
				return owned[k].level;
		return 0;
	}
};

} // namespace client
} // namespace spades
