// The Tower Defense store, as a real player meets it: starts the server in
// tower defense mode, watches it tick with nobody on it, then joins as a
// player and asks it to sell a gun. Built by CMake as `store_server_test`;
// or by hand from the repository root against an ENet and a zlib.
//
// The regression this exists for: the store's state writer once read a
// player slot that had never been filled, and the server died on its first
// tick. An empty world is the ordinary case before anybody spawns, so that
// is the first thing checked here.
#include <Training/Protocol.h>
#include <Training/Server.h>
#include <Training/StoreCatalog.h>
#include <Client/StoreParser.h>
#include <Training/TrainingModes.h>

#include <enet/enet.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

namespace {
	constexpr uint16_t kPort = 39188;
	enum : uint8_t {
		kStateData = 15, kChatMessage = 17, kMapStart = 18, kMapChunk = 19,
		kHandshakeInit = 31, kHandshakeResponse = 32, kVersionGet = 33,
		kVersionResponse = 34, kExtensionInfo = 60
	};

	struct Client {
		ENetHost* host = nullptr;
		ENetPeer* peer = nullptr;
		bool connected = false;
		std::vector<std::vector<uint8_t>> received;

		void Send(const std::vector<uint8_t>& d) {
			ENetPacket* p =
			    enet_packet_create(d.data(), d.size(), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(peer, 0, p);
		}
		void Service() {
			ENetEvent ev;
			while (enet_host_service(host, &ev, 0) > 0) {
				if (ev.type == ENET_EVENT_TYPE_CONNECT) connected = true;
				else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
					received.emplace_back(ev.packet->data,
					                         ev.packet->data + ev.packet->dataLength);
					enet_packet_destroy(ev.packet);
				}
			}
		}
		const std::vector<uint8_t>* Find(uint8_t id, size_t from = 0) const {
			for (size_t i = from; i < received.size(); i++)
				if (!received[i].empty() && received[i][0] == id) return &received[i];
			return nullptr;
		}
		void Clear() { received.clear(); }
	};

	template <class F> bool Run(host::Server& s, Client& c, int ms, F done) {
		const auto until = std::chrono::steady_clock::now() +
		                   std::chrono::milliseconds(ms);
		while (std::chrono::steady_clock::now() < until) {
			s.pump(1);
			c.Service();
			if (done()) return true;
		}
		return false;
	}

	// The store's own packet: Smoke (127) carrying sub 6. The last one wins,
	// since each says the whole purse afresh.
	struct StoreRead {
		bool seen = false;
		std::uint16_t zedcred = 0;
		std::uint8_t wave = 0;
		std::vector<std::pair<std::uint8_t, std::uint8_t>> owned;
	};

	StoreRead ReadStore(const Client& c) {
		StoreRead out;
		for (size_t i = 0; i < c.received.size(); ++i) {
			const std::vector<std::uint8_t>& p = c.received[i];
			if (p.size() < 3 || p[0] != 127 || p[1] != 6) continue;
			// The packet id and the sub packet byte are the dispatcher's and
			// the switch's; what is left is the store's body - read with the
			// CLIENT'S OWN reader, the same function the game calls when a
			// store arrives. A test that parsed the bytes with a reader of its
			// own would have passed happily while the game dropped every
			// store it was sent.
			spades::client::StoreState cs;
			if (!spades::client::ParseStoreBody(p.data() + 2, p.size() - 2, cs))
				continue;
			if (!cs.received) continue;   // the flag the Store button waits for
			out.zedcred = cs.zedcreds;
			out.wave = cs.wave;
			out.owned.clear();
			for (std::uint8_t k = 0; k < cs.ownedCount; ++k)
				out.owned.emplace_back(cs.owned[k].index, cs.owned[k].level);
			out.seen = true;
		}
		return out;
	}

	/** Times a card has been bought, from the state the server last said. */
	int LevelOf(const StoreRead& s, const char* sku) {
		const int want = training::store::find_item(sku);
		if (want < 0) return -1;
		const auto want8 = static_cast<std::uint8_t>(want);
		for (const auto& o : s.owned)
			if (o.first == want8) return o.second;
		return 0;
	}
} // namespace

int main() {
	if (enet_initialize() != 0) {
		std::printf("FAIL: no ENet\n");
		return 1;
	}

	host::HostConfig cfg;
	cfg.port = kPort;
	cfg.mode = training::Mode::TowerDefense;
	host::Server s(cfg);
	std::string err;
	CHECK(s.start(err));
	if (!err.empty())
		std::printf("start: %s\n", err.c_str());
	CHECK(s.player_count() == 0);   // no lanes to walk: nobody is on it yet

	// The regression: an empty world, ticking. The store's writer runs every
	// tick for every slot, and an empty slot is not a player.
	{
		Client empty;
		empty.host = enet_host_create(nullptr, 1, 1, 0, 0);
		Run(s, empty, 1200, [] { return false; });
		enet_host_destroy(empty.host);
		CHECK(s.player_count() == 0);
	}

	// --- One client, and the store it is told -------------------------------
	Client c;
	c.host = enet_host_create(nullptr, 1, 1, 0, 0);
	ENetAddress addr;
	enet_address_set_host(&addr, "127.0.0.1");
	addr.port = kPort;
	c.peer = enet_host_connect(c.host, &addr, 1, 3);
	CHECK(Run(s, c, 3000, [&] { return c.connected && c.Find(kHandshakeInit); }));

	if (const auto* init = c.Find(kHandshakeInit)) {
		std::vector<uint8_t> resp{kHandshakeResponse};
		resp.insert(resp.end(), init->begin() + 1, init->end());
		c.Send(resp);
	}
	CHECK(Run(s, c, 3000, [&] { return c.Find(kVersionGet) != nullptr; }));
	c.Send({kVersionResponse, 'o', 0, 1, 5, 't', 'e', 's', 't', 0});
	// Claim Smoke v3, which is what the store's state rides on.
	CHECK(Run(s, c, 3000, [&] { return c.Find(kExtensionInfo) != nullptr; }));
	c.Send({kExtensionInfo, 1, static_cast<uint8_t>(proto::Extension::Smoke), 3});

	// Join, so the purse and the word are this player's.
	std::vector<uint8_t> join{9 /*ExistingPlayer*/, 0, 0 /*blue*/, 0, 2 /*gun*/,
	                          0, 0, 0, 0, 0x80, 0x80, 0x80};
	for (char ch : std::string("Buyer"))
	    join.push_back(static_cast<std::uint8_t>(ch));
	join.push_back(0);
	c.Send(join);
	CHECK(Run(s, c, 5000, [&] { return c.Find(kStateData) != nullptr; }));

	// The whole map: the store's state is held for a client still loading,
	// exactly as the guns' own state is, so wait for the last chunk.
	CHECK(Run(s, c, 20000, [&] {
		const auto* start = c.Find(kMapStart);
		if (start == nullptr || start->size() < 5) return false;
		std::uint32_t announced = 0;
		for (int i = 0; i < 4; ++i)
			announced |= std::uint32_t((*start)[1 + i]) << (8 * i);
		std::size_t bytes = 0;
		for (const auto& p : c.received)
			if (!p.empty() && p[0] == kMapChunk) bytes += p.size() - 1;
		return bytes >= announced;
	}));
	CHECK(Run(s, c, 2000, [&] { return ReadStore(c).seen; }));

	// The purse opens at the first spawn, and the store is told it.
	const StoreRead start = ReadStore(c);
	CHECK(start.seen);
	// The purse opens at the first spawn: the economy's own opening hand, and
	// nothing bought yet.
	CHECK(start.zedcred > 0);
	CHECK(LevelOf(start, "smg") == 0);

	const std::uint16_t purse0 = start.zedcred;

	// The classes are the mode's own now: every defender may stand in
	// any of them, the change costing only the respawn into it, so the
	// unlock cards are refused without a word - the verdict takes back
	// the ownership and the purse pays nothing. The state is only said
	// again on the five-second refresh, since nothing changed, so that
	// is what is waited for.
	c.Clear();
	c.Send({kChatMessage, 0, 0, '/', 'b', 'u', 'y', ' ', 's', 'm', 'g', 0});
	CHECK(Run(s, c, 8000, [&] { return ReadStore(c).seen; }));
	CHECK(LevelOf(ReadStore(c), "smg") == 0);
	CHECK(ReadStore(c).zedcred == purse0);

	// Ask for one the purse can cover: it is sold, and the purse falls
	// by it.
	c.Clear();
	c.Send({kChatMessage, 0, 0, '/', 'b', 'u', 'y', ' ', 'h', 'p', '1', 0});
	CHECK(Run(s, c, 6000, [&] { return ReadStore(c).seen; }));
	const int hp1_price =
	  training::store::kItems[training::store::find_item("hp1")].price;
	const StoreRead bought = ReadStore(c);
	// The word says what it sold, and the purse says the same as the wire.
	CHECK(LevelOf(bought, "hp1") == 1);
	CHECK(bought.zedcred == purse0 - static_cast<std::uint16_t>(hp1_price));

	// Asked for twice, the second ask is refused: a cap is a cap, and a
	// refusal takes no coin. The state is only said again on the five-second
	// refresh, since nothing changed, so that is what is waited for.
	c.Clear();
	c.Send({kChatMessage, 0, 0, '/', 'b', 'u', 'y', ' ', 'h', 'p', '1', 0});
	CHECK(Run(s, c, 8000, [&] { return ReadStore(c).seen; }));
	CHECK(LevelOf(ReadStore(c), "hp1") == 1);
	CHECK(ReadStore(c).zedcred == bought.zedcred);

	// One the purse cannot reach: nothing is sold, and no coin moves.
	c.Clear();
	c.Send({kChatMessage, 0, 0, '/', 'b', 'u', 'y', ' ', 's', 'p', 'e', 'e',
	        'd', '1', 0});
	CHECK(Run(s, c, 8000, [&] { return ReadStore(c).seen; }));
	CHECK(LevelOf(ReadStore(c), "speed1") == 0);
	CHECK(ReadStore(c).zedcred == bought.zedcred);

	// The word says the shelves when it names none, so a player can find
	// what stands in the store without leaving the game.
	c.Clear();
	c.Send({kChatMessage, 0, 0, '/', 's', 't', 'o', 'r', 'e', 0});
	CHECK(Run(s, c, 3000, [&] {
		for (const auto& p : c.received)
			if (!p.empty() && p[0] == kChatMessage && p.size() > 20) return true;
		return false;
	}));

	enet_peer_disconnect_now(c.peer, 0);
	enet_host_destroy(c.host);
	enet_deinitialize();
	if (failures == 0)
		std::printf("store_server_test: all passed\n");
	return failures == 0 ? 0 : 1;
}