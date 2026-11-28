#include "Protocol.h"
#include "Vec3.h"
#include <cmath>
#include <cstring>

namespace proto {

void ByteWriter::write_u8(uint8_t v) { data.push_back(v); }
void ByteWriter::write_i8(int8_t v) { data.push_back(static_cast<uint8_t>(v)); }

void ByteWriter::write_u16(uint16_t v) {
    data.push_back(static_cast<uint8_t>(v & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void ByteWriter::write_u32(uint32_t v) {
    data.push_back(static_cast<uint8_t>(v & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
// Block coordinates are signed - lsd reads them as ivec3p and the protocol has
// always carried them that way. Reading them unsigned and casting worked only
// because nothing in a valid map is negative.
void ByteWriter::write_i32(int32_t v) {
    write_u32(static_cast<uint32_t>(v));
}


void ByteWriter::write_f32(float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(float));
    write_u32(u);
}
void ByteWriter::write_vec(const Vec3& v) {
    write_f32(v.x);
    write_f32(v.y);
    write_f32(v.z);
}
void ByteWriter::write_ivec(int x, int y, int z) {
    write_i32(x);
    write_i32(y);
    write_i32(z);
}


void ByteWriter::write_bytes(const uint8_t* buf, size_t len) {
    data.insert(data.end(), buf, buf + len);
}

void ByteWriter::write_fixed_string(const std::string& s, size_t len) {
    size_t n = s.size() < len ? s.size() : len;
    for (size_t i = 0; i < n; ++i) {
        data.push_back(static_cast<uint8_t>(s[i]));
    }
    for (size_t i = n; i < len; ++i) {
        data.push_back(0);
    }
}

void ByteWriter::write_cstring(const std::string& s) {
    for (char c : s) {
        data.push_back(static_cast<uint8_t>(c));
    }
    data.push_back(0);
}

ByteReader::ByteReader(const uint8_t* d, size_t s) : data(d), size(s), offset(0) {}

bool ByteReader::can_read(size_t n) const { return offset + n <= size; }

uint8_t ByteReader::read_u8() {
    if (!can_read(1)) return 0;
    return data[offset++];
}

int8_t ByteReader::read_i8() {
    return static_cast<int8_t>(read_u8());
}

uint16_t ByteReader::read_u16() {
    if (!can_read(2)) return 0;
    uint16_t v = data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return v;
}

uint32_t ByteReader::read_u32() {
    if (!can_read(4)) return 0;
    uint32_t v = static_cast<uint32_t>(data[offset]) |
                 (static_cast<uint32_t>(data[offset + 1]) << 8) |
                 (static_cast<uint32_t>(data[offset + 2]) << 16) |
                 (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return v;
}
int32_t ByteReader::read_i32() {
    return static_cast<int32_t>(read_u32());
}


float ByteReader::read_f32() {
    uint32_t u = read_u32();
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}
Vec3 ByteReader::read_vec() {
    // Left to right, which is not guaranteed inside a braced initialiser for
    // three separate calls - hence the temporaries.
    const float x = read_f32();
    const float y = read_f32();
    const float z = read_f32();
    return Vec3{x, y, z};
}


IVec3 ByteReader::read_ivec() {
    // Same reason for the temporaries as read_vec().
    const int32_t x = read_i32();
    const int32_t y = read_i32();
    const int32_t z = read_i32();
    return IVec3{x, y, z};
}


std::string ByteReader::read_fixed_string(size_t len) {
    if (!can_read(len)) return "";
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[offset++]);
        if (c == '\0') {
            // consume remaining but stop string
            for (size_t j = i + 1; j < len; ++j) {
                offset++;
            }
            break;
        }
        s.push_back(c);
    }
    return s;
}

std::string ByteReader::read_cstring() {
    std::string s;
    while (offset < size) {
        char c = static_cast<char>(data[offset++]);
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

const uint8_t* ByteReader::read_bytes(size_t len) {
    if (!can_read(len)) return nullptr;
    const uint8_t* p = data + offset;
    offset += len;
    return p;
}

uint8_t ExtensionInfoMsg::version_of(Extension e) const {
    for (const Entry& x : entries) {
        if (x.id == static_cast<uint8_t>(e)) return x.version;
    }
    return 0;
}

bool read_extension_info(ByteReader& br, ExtensionInfoMsg& out) {
    out.entries.clear();
    if (!br.can_read(1)) return false;
    const uint8_t count = br.read_u8();
    for (uint8_t i = 0; i < count; ++i) {
        // A count larger than the packet is not a reason to throw away the
        // entries that did arrive: a truncated list simply claims less.
        if (!br.can_read(2)) break;
        ExtensionInfoMsg::Entry e;
        e.id = br.read_u8();
        e.version = br.read_u8();
        out.entries.push_back(e);
    }
    return true;
}

bool read_flashlight(ByteReader& br, FlashlightMsg& out) {
    if (!br.can_read(1)) return false;
    const uint8_t sub = br.read_u8();
    switch (sub) {
        case static_cast<uint8_t>(FlashlightSub::Light):
            if (!br.can_read(2)) return false;
            out.sub = FlashlightSub::Light;
            out.player = br.read_u8();
            out.on = br.read_u8() != 0;
            return true;
        case static_cast<uint8_t>(FlashlightSub::LightState):
            out.sub = FlashlightSub::LightState;
            out.state.clear();
            while (br.can_read(1)) out.state.push_back(br.read_u8());
            return true;
        default:
            return false;
    }
}

void write_flashlight_light(ByteWriter& bw, uint8_t player, bool on) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Flashlight));
    bw.write_u8(static_cast<uint8_t>(FlashlightSub::Light));
    bw.write_u8(player);
    bw.write_u8(on ? 1 : 0);
}

void write_flashlight_state(ByteWriter& bw, const std::vector<bool>& lit) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Flashlight));
    bw.write_u8(static_cast<uint8_t>(FlashlightSub::LightState));
    // ZeroSpades reads one bit per player slot it has, 256; send them all.
    for (size_t byte = 0; byte < 32; ++byte) {
        uint8_t v = 0;
        for (size_t bit = 0; bit < 8; ++bit) {
            const size_t id = byte * 8 + bit;
            if (id < lit.size() && lit[id]) v = static_cast<uint8_t>(v | (1u << bit));
        }
        bw.write_u8(v);
    }
}

void write_daytime_sky(ByteWriter& bw, uint16_t time) {
    bw.write_u8(static_cast<uint8_t>(PacketId::DaytimeWeather));
    bw.write_u8(0);                        // Sky
    bw.write_u8(static_cast<uint8_t>(time & 0xff));
    bw.write_u8(static_cast<uint8_t>(time >> 8));
    bw.write_u8(0); bw.write_u8(0);        // reserved
    bw.write_u8(0); bw.write_u8(0);        // weather: none in version 1
}

void write_team_outfit(ByteWriter& bw, uint8_t team, uint8_t outfit) {
    bw.write_u8(static_cast<uint8_t>(PacketId::PlayerOutfit));
    bw.write_u8(static_cast<uint8_t>(OutfitSub::TeamOutfits));
    bw.write_u8(team);
    bw.write_u8(outfit);
}

void write_set_outfit(ByteWriter& bw, uint8_t player, uint8_t outfit) {
    bw.write_u8(static_cast<uint8_t>(PacketId::PlayerOutfit));
    bw.write_u8(static_cast<uint8_t>(OutfitSub::SetOutfit));
    bw.write_u8(player);
    bw.write_u8(outfit);
}

void write_smoke(ByteWriter& bw, uint8_t id, SmokeStyle style, const Vec3& origin,
                 uint8_t radius, uint8_t r, uint8_t g, uint8_t b, uint8_t opacity,
                 uint16_t duration_tenths, uint8_t fade_tenths, uint8_t flags) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Smoke));
    bw.write_u8(static_cast<uint8_t>(SmokeSub::Smoke));
    bw.write_u8(id);
    bw.write_u8(static_cast<uint8_t>(style));
    bw.write_vec(origin);
    bw.write_u8(radius);
    bw.write_u8(r);
    bw.write_u8(g);
    bw.write_u8(b);
    bw.write_u8(opacity);
    bw.write_u16(duration_tenths);
    bw.write_u8(fade_tenths);
    bw.write_u8(flags);
}

void write_smoke_clear(ByteWriter& bw, uint8_t id) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Smoke));
    bw.write_u8(static_cast<uint8_t>(SmokeSub::Clear));
    bw.write_u8(id);
}

void write_path(ByteWriter& bw, uint8_t id, uint8_t r, uint8_t g, uint8_t b,
                const std::vector<PathPoint>& points) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Smoke));
    bw.write_u8(static_cast<uint8_t>(SmokeSub::Path));
    bw.write_u8(id);
    bw.write_u8(r);
    bw.write_u8(g);
    bw.write_u8(b);
    // A count past the client's byte is the count it is sent as: the path
    // simply ends where its points ran out, still a line to somewhere.
    const uint8_t count = static_cast<uint8_t>(
        points.size() > 255 ? 255 : points.size());
    bw.write_u8(count);
    if (count == 0) return;
    bw.write_u16(static_cast<uint16_t>(points[0].x));
    bw.write_u16(static_cast<uint16_t>(points[0].y));
    bw.write_u8(points[0].z);
    for (uint8_t k = 1; k < count; ++k) {
        // The deltas are signed in the wire's byte; the caller cuts its
        // runs to spans, and a span that slips past the byte is clamped
        // here rather than wrapped: the line bends to where it can be
        // said, not somewhere a wrap would put it.
        auto cut = [](int d) -> int8_t {
            if (d > 127) d = 127;
            if (d < -128) d = -128;
            return static_cast<int8_t>(d);
        };
        bw.write_i8(cut(points[k].x - points[k - 1].x));
        bw.write_i8(cut(points[k].y - points[k - 1].y));
        bw.write_i8(cut(points[k].z - points[k - 1].z));
    }
}

void write_turrets(ByteWriter& bw, const std::vector<TurretState>& turrets) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Smoke));
    bw.write_u8(static_cast<uint8_t>(SmokeSub::Turret));
    // A pack past the byte is the pack it is sent as: a world with that
    // many guns defending one core has other problems first.
    const uint8_t count = static_cast<uint8_t>(turrets.size() > 255 ? 255 : turrets.size());
    bw.write_u8(count);
    for (uint8_t k = 0; k < count; ++k) {
        const TurretState& t = turrets[k];
        bw.write_u8(t.id);
        bw.write_u8(t.level);
        bw.write_u16(static_cast<uint16_t>(t.x));
        bw.write_u16(static_cast<uint16_t>(t.y));
        bw.write_u8(t.z);
        bw.write_u8(t.locked);
        bw.write_u8(t.yaw_pack);
        bw.write_u8(t.pitch_pack);
    }
}

void write_turret_shot(ByteWriter& bw, uint8_t turret_id) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Smoke));
    bw.write_u8(static_cast<uint8_t>(SmokeSub::TurretShot));
    bw.write_u8(turret_id);
}

void write_core(ByteWriter& bw, uint16_t x, uint16_t y, uint8_t z) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Smoke));
    bw.write_u8(static_cast<uint8_t>(SmokeSub::Core));
    bw.write_u16(x);
    bw.write_u16(y);
    bw.write_u8(z);
}

void write_store(ByteWriter& bw, const StoreState& state) {
    bw.write_u8(static_cast<uint8_t>(PacketId::Smoke));
    bw.write_u8(static_cast<uint8_t>(SmokeSub::Store));
    bw.write_u16(state.zedcred);
    bw.write_u8(state.wave);
    bw.write_u8(state.phase);
    bw.write_u16(state.core_hp);
    bw.write_u16(state.core_max_hp);
    bw.write_u8(state.weapon);
    bw.write_u8(state.grenades);
    bw.write_u8(state.blocks);
    const uint8_t count = state.owned_count > 64 ? 64 : state.owned_count;
    bw.write_u8(count);
    for (uint8_t k = 0; k < count; ++k) {
        bw.write_u8(state.owned[k].index);
        bw.write_u8(state.owned[k].level);
    }
    // The prices the purse pays, one pair a card: the host can reprice
    // the catalogue in its own towerdefense.toml, so the wire says what
    // the server charges and the shelf can never show another number.
    // The count rides even when it is none, that the word may never be
    // read two ways - and the core's own cell rides behind it, that
    // the client may hold the marker's ground against its predictions.
    bw.write_u8(state.price_count);
    for (uint8_t k = 0; k < state.price_count; ++k) {
        bw.write_u8(state.priced[k].index);
        bw.write_u16(state.priced[k].price);
    }
    bw.write_u16(state.core_x);
    bw.write_u16(state.core_y);
    bw.write_u8(state.core_top_z);
    bw.write_u8(state.core_ground_z);
}

bool read_store(ByteReader& br, StoreState& state) {
    // The whole packet as write_store wrote it: the Smoke id and its Store
    // sub, then the body. The client has already eaten the id and the sub
    // byte by the time it reaches the store, which is why the body has a
    // reader of its own - read_store_body, below.
    if (!br.can_read(2)) return false;
    if (br.read_u8() != static_cast<uint8_t>(PacketId::Smoke)) return false;
    if (br.read_u8() != static_cast<uint8_t>(SmokeSub::Store)) return false;
    return read_store_body(br, state);
}

bool read_store_body(ByteReader& br, StoreState& state) {
    // zedcred, wave, phase, core hp/max, gun, grenades, blocks, then the
    // count: twelve bytes, and every one of them asked for before it is
    // read rather than read and hoped. Twelve bytes with no pairs, the
    // prices' own count and the core's own cell behind them, so a fresh
    // player who owns nothing is a Store of twenty, and it is the
    // first thing a new world ever says.
    if (!br.can_read(12)) return false;
    const uint16_t zedcred = br.read_u16();
    const uint8_t wave = br.read_u8();
    const uint8_t phase = br.read_u8();
    const uint16_t core_hp = br.read_u16();
    const uint16_t core_max = br.read_u16();
    const uint8_t weapon = br.read_u8();
    const uint8_t grenades = br.read_u8();
    const uint8_t blocks = br.read_u8();
    const int count = br.read_u8();
    // Two bytes a pair, and only as many as the table can name: a longer
    // list is a truncation, and the reader says so rather than overrunning.
    if (count > 64) return false;
    if (!br.can_read(2 * static_cast<size_t>(count))) return false;
    state = StoreState{};
    state.zedcred = zedcred;
    state.wave = wave;
    state.phase = phase;
    state.core_hp = core_hp;
    state.core_max_hp = core_max;
    state.weapon = weapon;
    state.grenades = grenades;
    state.blocks = blocks;
    state.owned_count = static_cast<uint8_t>(count);
    for (int k = 0; k < count; ++k) {
        state.owned[k].index = br.read_u8();
        state.owned[k].level = br.read_u8();
    }
    // The prices and the core's cell, asked for whole the same
    // discipline as the pairs: the word is one shape, and a body that
    // ends before any of it is refused rather than guessed at. The
    // price count rides even when it is none; a ground of zero names
    // a world with no core to guard.
    if (!br.can_read(1)) return false;
    const int prices = br.read_u8();
    if (prices > 64) return false;
    if (!br.can_read(3 * static_cast<size_t>(prices))) return false;
    state.price_count = static_cast<uint8_t>(prices);
    for (int k = 0; k < prices; ++k) {
        state.priced[k].index = br.read_u8();
        state.priced[k].price = br.read_u16();
    }
    if (!br.can_read(6)) return false;
    state.core_x = br.read_u16();
    state.core_y = br.read_u16();
    state.core_top_z = br.read_u8();
    state.core_ground_z = br.read_u8();
    return true;
}

} // namespace proto
