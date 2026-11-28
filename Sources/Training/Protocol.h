#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct Vec3;

namespace proto {

constexpr uint8_t PROTOCOL_VERSION = 3; // AoS 0.75
constexpr size_t NAME_LEN = 16;

// No player. Ids are a byte and this one is reserved, which is what makes the
// server's get_anon_pid() work - so it is never a real player and never
// something to send. Code that finds it in self_id_ has got ahead of the
// StateData that hands the id out.
constexpr uint8_t kInvalidPlayerId = 255;

enum class PacketId : uint8_t {
    PositionData      = 0,
    OrientationData   = 1,
    WorldUpdate       = 2,
    InputData         = 3,
    WeaponInput       = 4,
    SetHP             = 5, // server -> client
    HitPacket         = 5, // client -> server
    GrenadePacket     = 6,
    SetTool           = 7,
    SetColor          = 8,
    ExistingPlayer    = 9,
    ShortPlayerData   = 10,
    MoveObject        = 11,
    CreatePlayer      = 12,
    BlockAction       = 13,
    BlockLine         = 14,
    StateData         = 15,
    KillAction        = 16,
    ChatMessage       = 17,
    MapStart          = 18,
    MapChunk          = 19,
    PlayerLeft        = 20,
    TerritoryCapture  = 21,
    ProgressBar       = 22,
    IntelCapture      = 23,
    IntelPickup       = 24,
    IntelDrop         = 25,
    Restock           = 26,
    FogColor          = 27,
    WeaponReload      = 28,
    ChangeTeam        = 29,
    ChangeWeapon      = 30,
    MapCached         = 31, // 0.76
    VersionHandshakeInit = 31,
    VersionHandshakeResponse = 32,
    VersionGet        = 33,
    VersionResponse   = 34,
    ExtensionInfo     = 60,
    // An extension's packets are numbered 64 + its extension id, so the
    // Teamplay extension (48, below) owns 112. Only ever sent to a connection
    // that claimed the extension; see read_teamplay_ping().
    Teamplay          = 112,
    // Pie Menu (extension 0x31): the quick menu served whole, server to
    // client. Only ever sent to a connection that claimed the extension.
    PieMenu           = 113,
    // Flashlight (extension 0x32): who has a lit lamp. See the section below.
    Flashlight        = 114,
    // Daytime and Weather (extension 0x33): the time of day, server to client.
    DaytimeWeather    = 115,
    // Player Outfit (extension 0x35): what a body is drawn from. Only ever
    // sent to a connection that claimed the extension.
    PlayerOutfit      = 117,
    // Smoke (extension 0x3F): a smoke the server puts in the world. The
    // way in is said in it while the dead walk; see the section below.
    Smoke            = 127,
};

// The three guns. The protocol calls this field the gun and the packets that
// carry it Weapon*, which is the protocol's own inconsistency, not ours - code
// here says gun for the thing and weapon only where it is naming a packet.
enum class Gun : uint8_t { Rifle = 0, SMG = 1, Shotgun = 2 };

// The tools, as SetTool carries them. The server puts the gun in a player's
// hands on every spawn, so that is what a round starts from.
enum class Tool : uint8_t { Spade = 0, Block = 1, Gun = 2, Grenade = 3 };

// Where a HitPacket says it landed. Torso and head are what a gun reports;
// melee takes a different path through the server - no ammunition test, no aim
// ray, just a distance check.
enum class HitType : uint8_t { Torso = 0, Head = 1, Arms = 2, Legs = 3, Melee = 4 };

// Entity ids in MoveObject and in the CTF state block. In TC every object is a
// territory numbered from zero instead, which is why the two cannot share a
// table.
enum class Entity : uint8_t { BlueFlag = 0, GreenFlag = 1, BlueBase = 2, GreenBase = 3 };

// Teams. 255 is spectator; 2 is TC's neutral, which is not a team anyone is on.
// Block coordinates. Three signed 32-bit integers, which is how BlockAction
// and BlockLine carry a position - not the three floats a Vec3 is. Reading one
// as a Vec3 is the mistake this type exists to make impossible.
struct IVec3 {
    int x, y, z;
};

constexpr uint8_t kTeamBlue = 0;
constexpr uint8_t kTeamGreen = 1;
constexpr uint8_t kTeamNeutral = 2;
constexpr uint8_t kTeamSpectator = 255;

struct ByteWriter {
    std::vector<uint8_t> data;

    // Room for most packets up front, rather than growing a byte at a time.
    ByteWriter() { data.reserve(64); }

    void write_u8(uint8_t v);
    void write_i8(int8_t v);
    void write_u16(uint16_t v);
    void write_u32(uint32_t v);
    void write_i32(int32_t v);
    void write_f32(float v);
    // Three floats, which is how every position, orientation and velocity in
    // this protocol is carried.
    void write_vec(const struct Vec3& v);
    // Three signed i32s - a block position. See IVec3.
    void write_ivec(int x, int y, int z);
    void write_bytes(const uint8_t* buf, size_t len);
    void write_fixed_string(const std::string& s, size_t len);
    void write_cstring(const std::string& s);
};

struct ByteReader {
    const uint8_t* data;
    size_t size;
    size_t offset;

    ByteReader(const uint8_t* d, size_t s);
    bool can_read(size_t n) const;
    uint8_t read_u8();
    int8_t read_i8();
    uint16_t read_u16();
    uint32_t read_u32();
    int32_t read_i32();
    float read_f32();
    struct Vec3 read_vec();
    // Twelve bytes. Bounds-check with can_read() before calling: like every
    // other reader here it returns zeroes rather than reading past the end.
    IVec3 read_ivec();
    std::string read_fixed_string(size_t len);
    std::string read_cstring();
    const uint8_t* read_bytes(size_t len);
};

// The chat types, as pyspades/constants.py numbers them.
enum class ChatType : uint8_t {
    All = 0, Team = 1, System = 2, Big = 3, Info = 4, Warning = 5, Error = 6
};

// The protocol extensions, numbered as pyspades/constants.py numbers them.
// ExtensionInfo (60) is an exchange: the server offers its list once it has
// the client's VersionResponse, and the client answers with what it claims.
enum class Extension : uint8_t {
    // Pings between teammates, relayed by the server.
    Teamplay = 48,
    // The pie menu a server serves whole: its rings and their slices, said
    // on the wire for the middle button wheel to open on.
    PieMenu = 0x31,
    // Lamps everybody sees.
    Flashlight = 0x32,
    // The time of day: a client that claimed it draws a night at midnight.
    DaytimeWeather = 0x33,
    // What a team is drawn as, aosprotocol's player-outfit extension.
    PlayerOutfit = 0x35,
    // A smoke the server puts in the world, in a colour, for a time.
    Smoke = 0x3F,
    // Player ids past 31.
    PlayerLimit = 192,
    // Chat types sent as they are, for the centre-screen lines and banners.
    ChatType = 193,
    // The reason for a kick, as a CHAT_SYSTEM message before it.
    KickReason = 194,
};

// ExtensionInfo (60), from pyspades/contained.pyx, ProtocolExtensionInfo:
//
//     count(1) then count x ( id(1) version(1) )
//
// A count that overruns the packet is a short read, not a parse failure:
// whatever entries did arrive are kept.
struct ExtensionInfoMsg {
    struct Entry { uint8_t id = 0; uint8_t version = 0; };
    std::vector<Entry> entries;

    // The version offered for `id`, or 0 if it was not offered at all.
    uint8_t version_of(Extension e) const;
    bool offers(Extension e) const { return version_of(e) != 0; }
};
bool read_extension_info(ByteReader& br, ExtensionInfoMsg& out);

// Teamplay (extension 48, packet 112): a ping's reason is capped here.
constexpr size_t kTeamplayReasonMax = 64;

// --- Flashlight (extension 0x32, packet 114) ------------------------------
//
// `[114][sub][...]`:
//
//   0  Light        C->S->C  player(1) on(1). A client sends its own switch
//                            (the player byte is ignored and filled in by the
//                            server); the server relays every granted switch
//                            to everyone who claimed the extension, the
//                            switcher included - that echo is what lights it.
//   1  Light State  S->C     a bitset to the end of the packet, bit i of byte
//                            i/8 for player i: every lamp lit, for a newcomer.
//   2  Light Config S->C     player(1) reach(1) cone(1) rgb(3); 255 is the
//                            server's default for everyone. Not read here.
enum class FlashlightSub : uint8_t { Light = 0, LightState = 1, LightConfig = 2 };

struct FlashlightMsg {
    FlashlightSub sub = FlashlightSub::Light;
    uint8_t player = kInvalidPlayerId;   // Light
    bool on = false;                     // Light
    std::vector<uint8_t> state;          // LightState's bitset
    bool lit(uint8_t id) const {         // LightState: is player `id` lit?
        return id / 8u < state.size() && ((state[id / 8u] >> (id % 8u)) & 1u) != 0;
    }
};
// After the packet id. False for a short packet or a sub this does not read
// (Light Config, and anything a later version adds).
bool read_flashlight(ByteReader& br, FlashlightMsg& out);
// The whole packet, id included: switch `player`'s lamp.
void write_flashlight_light(ByteWriter& bw, uint8_t player, bool on);
// The whole packet, id included: Light State for the ids lit in `lit`.
void write_flashlight_state(ByteWriter& bw, const std::vector<bool>& lit);

// --- Daytime and Weather (extension 0x33, packet 115) ---------------------
//
// `[115][sub][...]`. Version 1 has one sub packet, Sky (0): time(u16 LE,
// minutes since midnight) reserved(u16) weather(u16). Any time but 0 is the day.
constexpr uint16_t kMidnight = 0;
constexpr uint16_t kNoon = 720;
// The whole packet, id included.
void write_daytime_sky(ByteWriter& bw, uint16_t time);

// --- Player Outfit (extension 0x35, packet 117) ----------------------------
//
// `[117][sub][...]`, from aosprotocol extensions/player-outfit.md. Version 1
// has the sub packets 0 Set Outfit (player(1) outfit(1)) and 1 Team Outfits
// ((team(1) outfit(1))* to the end of the packet; a team not listed wears 0).
// An outfit only shows at its player's next spawn, so a wave counts: every
// zombie it raises is its next spawn.
//
// What the walking dead wear, aosprotocol #14's zombie theme: the plain
// Zombie, whose number it gives as 64. A client that claims the extension
// draws it from Models/Player/Custom as ZombieGrunt1 to 3 by player id, so a
// horde has three faces. The theme counts the brutes up from there: the
// Spitter, the Leaper, the Caller (whose purple face the healer wears), the
// Breaker, and the Toppler, whose plain Zombie set dresses the tank until
// its own model stands.
constexpr uint8_t kOutfitZombie = 64;
constexpr uint8_t kOutfitSpitter = 65;
constexpr uint8_t kOutfitLeaper = 66;
constexpr uint8_t kOutfitCaller = 67;
constexpr uint8_t kOutfitBreaker = 68;
constexpr uint8_t kOutfitToppler = 69;
enum class OutfitSub : uint8_t { SetOutfit = 0, TeamOutfits = 1 };
// The whole packet, id included: one team's outfit.
void write_team_outfit(ByteWriter& bw, uint8_t team, uint8_t outfit);
// The whole packet, id included: the outfit of one player, worn at once.
void write_set_outfit(ByteWriter& bw, uint8_t player, uint8_t outfit);

// --- Smoke (extension 0x3F, packet 127) ------------------------------------
//
// `[127][sub][...]`, docs/extensions/smoke.md:
//
//   0  Smoke        S->C  id(1) style(1) x y z(4 each) radius(1) r g b
//                           opacity(1 each) duration(2) fade(1) flags(1)
//   1  Clear Smoke  S->C  id(1)   255: every smoke
//   2  Path         S->C  id(1) r g b(1 each) count(1) then count points:
//                     the first x/y/z as x(2) y(2) z(1), each later one as
//                     dx dy dz(1 each, a byte of delta)
//   3  Turrets      S->C  count(1) then count entries: id(1) level(1)
//                     x(2) y(2) z(1) locked(1) yaw(1) pitch(1). The whole
//                     set of placed sentry guns in one packet: the list
//                     replaces whatever was said before it, and a gun no
//                     longer listed is a gun no longer there.
//   4  Turret Shot  S->C  id(1): this gun fired, the moment its damage
//                     lands, so the flash and the fall are one thing
//
// A smoke the server puts in the world, in a colour, for a time: a wide
// cloud on the ground, a thin rising column, or a heavy vapour that pools
// at the floor and renews rather than climbs - the style the way in's
// trail is told in, so it reads as a bed of fog a block deep, not a
// plume over it. A smoke's id is the
// server's: sending a smoke on an id in use stops the old one, so a
// resent smoke replaces its bed in place, and a smoke the server
// simply stops sending burns out on its own. Style 0 grows as a cloud,
// style 1 rises as a column, style 2 gathers at the ground, style 4
// throws drops of the colour up off the origin to fall again; radius
// is in blocks (1 is one bed of the way in's trail), r g b opacity
// are bytes of 255ths, duration and fade are tenths of a second, and
// flags' bit 0 carries the hiss - a trail carries none, or the way in
// is a racket rather than a marker. Only ever sent to a connection
// that claimed the extension.
enum class SmokeSub : uint8_t {
    Smoke = 0, Clear = 1, Path = 2, Turret = 3, TurretShot = 4, Core = 5,
    Store = 6
};
enum class SmokeStyle : uint8_t { Cloud = 0, Column = 1, Ground = 2,
                                  Blast = 3, Spray = 4 };
// The whole packet, id included: one smoke started.
void write_smoke(ByteWriter& bw, uint8_t id, SmokeStyle style, const struct Vec3& origin,
                 uint8_t radius, uint8_t r, uint8_t g, uint8_t b, uint8_t opacity,
                 uint16_t duration_tenths, uint8_t fade_tenths, uint8_t flags);
// The whole packet, id included: 255 clears every smoke at once.
void write_smoke_clear(ByteWriter& bw, uint8_t id);

// One waypoint of a Path: a cell of the map and the surface the line runs
// on it at. The first of a path is sent as its own x and y, each later one
// as a byte of difference from the point before it, so a path is a handful
// of bytes however long the way it traces - the points are the turns, not
// every tread.
struct PathPoint {
    int16_t x = 0, y = 0;
    uint8_t z = 0;
};
// The whole packet, id included: one path replaced. A caller keeps every
// delta within a byte (-128..127, which it does by cutting a long
// straight run into spans) and the point count within 255, or the path is
// sent clipped - the line still runs to wherever its count ran out.
// Empty points: the id's path taken away.
void write_path(ByteWriter& bw, uint8_t id, uint8_t r, uint8_t g, uint8_t b,
                const std::vector<PathPoint>& points);

// One placed sentry gun on the wire: its own id, its level, the lower
// pillar block it is anchored on (the upper pillar block is that z - 1),
// whether it is firing at something, and the aim the server holds over it.
// The aim is quantized as the smoke buries it here so a client needs no
// other word to draw its works: yaw_pack[1] counts 128 steps round the
// circle from +Y, positive toward +X (round((atan2(dx, dy) degrees +
// 180) * 128 / 360) and 127), pitch[1] counts 127 steps over the -80 of
// full climb to the +80 of full descent, dz = the target minus the works
// and smaller z the higher, the same as everywhere here (round((pitch
// degrees + 80) * 127 / 160), clamped to 0..127).
struct TurretState {
    uint8_t id = 0;              // the wire's own name for it, 1..255
    uint8_t level = 0;           // 1..3
    int16_t x = 0, y = 0;        // the pillar's LOWER block
    uint8_t z = 0;               // its z (smaller z = higher, as everywhere here)
    uint8_t locked = 0;          // 1 while the gun is firing at a target
    uint8_t yaw_pack = 0;        // the aim, quantized as above
    uint8_t pitch_pack = 0;
};
// The whole packet, id included: the whole set of guns replaced. Entries
// past 255 are clipped, which no world of guns a server means to defend
// its core with is going to reach; a count of 0 simply says none stand.
void write_turrets(ByteWriter& bw, const std::vector<TurretState>& turrets);
// The whole packet, id included: one gun's shot, sent the tick its damage
// lands. Only ids that were in the last Turrets list, ever.
void write_turret_shot(ByteWriter& bw, uint8_t turret_id);

// One placed selected cell of the map and the plane of the column standing
// on it (as write_turrets carries a gun's lower block, smaller z the
// higher, and this is the plane of the core's own top): the smoke carries
// moves of the core, which no vanilla packet can say - every one of them
// speaks of a pickup, a drop or a capture that nobody here made. The z is
// the same cell StateData would name, so a client that heard it and a
// client that joined after draw the one flag.
void write_core(ByteWriter& bw, uint16_t x, uint16_t y, uint8_t z);

// --- Store state (Smoke sub 6) ---------------------------------------------
// One player's view of the Tower Defense store. Sent to EACH player on its
// own (the credits differ), on change and every five seconds so a client
// that finishes loading mid-game recovers. The owned levels are indexed by
// StoreCatalog::kItems; the wire carries only as many as the writer knows.
struct StoreState {
    struct Owned {
        uint8_t index = 0;     // index into StoreCatalog::kItems
        uint8_t level = 0;     // times bought (0 = not owned)
    };
    bool operator==(const StoreState& o) const {
        if (zedcred != o.zedcred || wave != o.wave || phase != o.phase)
            return false;
        if (core_hp != o.core_hp || core_max_hp != o.core_max_hp) return false;
        if (weapon != o.weapon || grenades != o.grenades ||
            blocks != o.blocks) return false;
        if (owned_count != o.owned_count) return false;
        for (uint8_t k = 0; k < owned_count; ++k)
            if (owned[k].index != o.owned[k].index ||
                owned[k].level != o.owned[k].level)
                return false;
        if (price_count != o.price_count) return false;
        for (uint8_t k = 0; k < price_count; ++k)
            if (priced[k].index != o.priced[k].index ||
                priced[k].price != o.priced[k].price)
                return false;
        if (core_x != o.core_x || core_y != o.core_y ||
            core_top_z != o.core_top_z ||
            core_ground_z != o.core_ground_z) return false;
        return true;
    }
    uint16_t zedcred = 0;      // the player's Zedcred purse (capped 65535)
    uint8_t wave = 0;          // the wave the horde stands at
    uint8_t phase = 0;         // horde::Phase as an integer
    uint16_t core_hp = 0;      // the core's health, for the status line
    uint16_t core_max_hp = 0;
    uint8_t weapon = 0;        // the current gun, 0 rifle 1 SMG 2 shotgun
    uint8_t grenades = 0;      // carried, for the consumable cards
    uint8_t blocks = 0;        // building blocks carried (capped at 50)
    uint8_t owned_count = 0;   // number of owned level pairs following
    Owned owned[64] = {};      // capped to 64, which exceeds the catalogue
    struct Priced {
        uint8_t index = 0;     // index into StoreCatalog::kItems
        uint16_t price = 0;    // the Zedcreds the server charges it
    };
    uint8_t price_count = 0;   // number of priced pairs following
    Priced priced[64] = {};    // capped to 64, which exceeds the catalogue
    uint16_t core_x = 0;       // the core's own cell, that the client
    uint16_t core_y = 0;       // may hold its ground against its own
    uint8_t core_top_z = 0;    // predictions: the column's top layer,
    uint8_t core_ground_z = 0; // and the ground it stands on (0 = none)
};
// The whole packet, id included. `count` says how many of `owned` are real.
void write_store(ByteWriter& bw, const StoreState& state);
// The same, read back: the whole packet, id included, into `state`. False
// when the packet is not one the store recognises, having read nothing.
bool read_store(ByteReader& br, StoreState& state);
// The body alone, with neither the packet id nor the sub packet byte: for
// the reader that has already been told what it is reading - the client's
// Smoke dispatcher has eaten both by the time the store's turn comes, so
// this is the one it needs. The two cannot drift: the whole-packet reader
// above is this one with the two bytes in front of it.
bool read_store_body(ByteReader& br, StoreState& state);

} // namespace proto
