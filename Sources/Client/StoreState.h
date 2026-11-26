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
