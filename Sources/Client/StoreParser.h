#pragma once
// The client's reading of the store's own word, in one place.
//
// The Smoke packet carries the Tower Defense store as its sixth sub packet.
// By the time the client gets there, the packet id and the sub packet byte
// have both been eaten already - the dispatcher took the id, the switch
// took the sub - so what is left is the store's body. Reading the whole
// packet there instead made the reader look at the purse's first two bytes
// and call them a packet id: every store arrived and every store was
// dropped, so the store's state was never heard and the store's button
// never appeared.
//
// Kept here, and inline, so the test can read the wire exactly as the
// client does: one reader, one framing, no drift.

#include <cstddef>
#include <cstdint>

#include <Training/Protocol.h>

#include "StoreState.h"

namespace spades {
namespace client {

	/** Reads the body of a Store - the bytes after the packet id and the sub
	 * packet byte - into `state`, and says whether it was one. False when the
	 * bytes are not a store, in which case `state` is untouched. A store that
	 * says more than it means is refused too: this reader knows one shape. */
	inline bool ParseStoreBody(const uint8_t* data, std::size_t len,
	                          StoreState& state) {
		proto::ByteReader br(data, len);
		proto::StoreState s;
		if (!proto::read_store_body(br, s)) return false;
		// Nothing may be left over: the body is exactly as long as it says.
		if (br.can_read(1)) return false;

		state = StoreState{};
		state.zedcreds = s.zedcred;
		state.wave = s.wave;
		state.phase = s.phase;
		state.coreHp = s.core_hp;
		state.coreMaxHp = s.core_max_hp;
		state.weapon = s.weapon;
		state.grenades = s.grenades;
		state.blocks = s.blocks;
		const std::size_t kept = s.owned_count;
		for (std::size_t k = 0; k < kept; ++k) {
			state.owned[k].index = s.owned[k].index;
			state.owned[k].level = s.owned[k].level;
		}
		state.ownedCount = static_cast<std::uint8_t>(kept);
		for (int g = 0; g < 4; ++g)
			state.equip[g] = s.equip[g];
		state.received = true;
		return true;
	}

} // namespace client
} // namespace spades
