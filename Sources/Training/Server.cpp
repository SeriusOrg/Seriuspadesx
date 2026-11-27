#include "Server.h"

#include "Grenade.h"
#include "Horde.h"
#include "MapGen.h"
#include "Physics.h"
#include "Protocol.h"
#include "SolidMap.h"
#include "StoreCatalog.h"
#include "TrainingCourse.h"
#include "TrainingModes.h"
#include "Vec3.h"
#include "VoxelMap.h"
#include "WeaponTiers.h"

#include <enet/enet.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

namespace host {

// The store's own table, in the names its callers already use.
namespace store = training::store;

namespace {

using proto::ByteReader;
using proto::ByteWriter;
using proto::PacketId;

// pyspades/constants.py, the ones this file uses and their neighbours (kept
// whole so a row reads like the original).
[[maybe_unused]] constexpr int kWeaponKill = 0, kHeadshotKill = 1, kMeleeKill = 2, kGrenadeKill = 3,
                               kFallKill = 4, kTeamChangeKill = 5, kClassChangeKill = 6;
[[maybe_unused]] constexpr int kBuildBlock = 0, kDestroyBlock = 1, kSpadeDestroy = 2, kGrenadeDestroy = 3;
constexpr int kToolSpade = 0, kToolBlock = 1, kToolWeapon = 2;
[[maybe_unused]] constexpr int kTorso = 0, kHead = 1, kArms = 2, kLegs = 3, kMelee = 4;
constexpr int kErrorWrongVersion = 3, kErrorFull = 4;
constexpr float kMaxBlockDistance = 6.0f;
constexpr float kMaxDigDistance = 6.0f;
constexpr float kMeleeDistance = 3.0f;
constexpr int kBuildTolerance = 5;
// Player ids are a byte and 255 means nobody.
constexpr int kSlots = 255;
constexpr double kTick = 1.0 / 60.0;
constexpr size_t kMapChunk = 8192;
// Teamplay: how long a relayed ping lasts, and how often one player may send.
constexpr float kPingDuration = 10.0f;
constexpr double kPingInterval = 1.0;
// A target back on its lane after it falls.
constexpr float kTargetRespawn = 2.0f;
// On the course a person is always blue; the targets are green.
constexpr int kPeople = 0, kTargets = 1;
// A zombie fresh out of the pile walks through its own for this long,
// until the wave has settled into single file. Two is enough: they are
// born a cell apart in the clearing, and the longer window only ever
// bought the pile its twenty seconds of standing still.
constexpr double kZombieSettle = 2.0;
// The dead's own answer to the walk's two-block step - a crossing the
// field prices as one jump, though the hop it gives on its own rises a
// block and a half and dies pressed against the face it meant to clear.
// A stuck walker flings itself high enough to stand on what it could
// not climb: the vault the reading promised it, paid for in one lunge.
constexpr float kDeadPounce = -0.45f;
constexpr double kPounceWindup = 1.0;    // this long stuck before the first fling
constexpr double kPounceAgain = 1.2;     // and this long between two of them
// Close enough under the crossing for the hop: any farther off, and
// the arc falls back before the lip of the tread is reached.
constexpr float kVaultReach = 1.5f;
// Half-second windows a leader may pace without reaching a lower floor of
// the walk before the pacing itself is read as stuck. The yo-yo moves -
// a half-second's worth in every window - so the moved measure never
// calls it; the walk's distance read at the feet does. Three
// seconds of going nowhere by the field's own count, and the hop and
// the fling the standstill gets are let loose again.
constexpr int kZombieYoyoWindows = 6;
// The stuck head's licence: the road is the road, and the spade is a
// rescue only - a wall that can be walked around is walked around for
// however long the detour runs, and a maze the living raise is
// honoured to the last of its turns, for the pack that faces one paces
// and hops and never draws the spade at all. A head stuck past its
// windup, and not resting on the spade's own cadence at a wall it is
// already opening, owes the
// licence anyway: the gate opens, and the crossing the spade's field
// names is opened in turn, wall or ceiling, top down at the dig's own
// pace.
constexpr double kStuckDigWindup = 2.0;
// The milling licence: a pack pinned far from the stairs shuffles a
// little every window of its watch, and a shuffler never stands still
// long enough for the stuck windows to catch it - the moved-window
// keeps it honest, the yo-yo its own. So the dig keeps a second clock
// that movement alone cannot reset: the leader holds a milestone of
// its score, and only a reading lower by kDigWatchGain winds the
// watch again. kDigWatchSeconds of shuffling without a net gain of
// ground, and the licence is owed all the same - the wall a pack
// mills at is never far from where it mills.
constexpr double kDigWatchSeconds = 8.0;
constexpr int kDigWatchGain = 3;
// The detour licence: a road the walk can hold is a road the walker
// walks - twenty counts of detour beat a hole in a wall of four, and
// the walls a maze turns against the wave were raised on purpose.
// But a town sealed to one gate prices its detours in hundreds, and
// the head packed at a walled gate reads a road it will never finish:
// the shuffle hides it from the stuck clock, the fluctuation of the
// shuffling pack winds the watch again, and the gate stays sealed for
// as long as the wave is polite about it. So the two fields are
// weighed head to head at the feet: a walk held kDigDetour above the
// crossing the spade prices from the same floor is a detour the
// honesty of the walk cannot pay for, and the stuck head owed the
// spade all along is granted it - the dig walk names the crossing,
// and the wall it names comes down where it says, be the detour a
// hundred counts or a maze turn the better answer. The condition of
// the weighing: the leg field must read at the feet, and the stuck
// clock must have caught the head besides, for the ratio alone is a
// coin the plain flips on every march under a town it cannot judge -
// the walker that walks prices no spade, whatever the fields say.
constexpr int kDigDetour = 50;
// The half second churn of a field recomputed makes brief readings
// flicker, and a licence that flips with the flicker is a stroke, a
// step away, a stroke again. The weighing of the two fields is held
// against time: only a ratio held this long is a detour at all, and
// the moment the reading breaks - the breach opens, the march prices
// fair again - the clock stands down and the road is the road once
// more.
constexpr double kDigDetourSeconds = 2.0;
// The flight a licensed stroke cuts into a wall: three strips into
// the face, each strip standing its room one step higher than the
// strip before it, the whole lane three wide. A wall one stroke
// cannot finish is climbed a flight at a time - a staircase the
// legs walk as the spade goes on.
constexpr int kDigFlightDepth = 3;
// The shoulder the lane keeps: a trench pressed to a wall serves
// its bodies one at a time and queues the wave behind them, and
// the pack that cannot pass its own stalled head fills the lane
// the way the dead filled the gallery. So the flight stroke clears
// this many columns past each edge of the lane, at the height of
// the rooms it cuts: whatever wall stands closer than three blocks
// to the walking line comes down with the stroke.
constexpr int kDigLaneMargin = 3;
// The work holds the worker: a face a stroke has touched within this
// window is a face the worker stays on. The licence gate and the
// advice are read anew every tick, on a field recomputed every half second -
// a reading that flips sends the leader off on the legs' own way the
// very moment it flips back, and a dig that begins every other minute
// is a dig that never ends: a stroke, a walk away, a return, a stroke,
// the mill again with a spade in it. So a recent stroke buys the gate:
// while the work feeds itself, the leader keeps it, and the moment
// the strokes stop - the crossing open, the face gone - the window
// passes and the walker walks whatever the fields say again.
constexpr double kDigStaySeconds = 2.0;
// A leader stalled this long - stuck past the windup, and not already
// swinging at a wall it is opening for the line - loses its line for
// a while: a follower owed prints it cannot walk reads the field for
// itself and walks its own way down, maze street or loop, whatever the
// walk prices cheapest, so one stuck head cannot stack the wave behind
// its standstill. It is the way down the field, never the air's own
// bearing, that the strayed walk - so a walk through a maze loop moves
// away from the core in steps and nearer in the only count that
// matters, the walk's own. The line knits itself again where the
// prints and the chase resume: only the head keeps the spade, one
// breach a crossing as it always was.
constexpr double kStuckHeadDisband = 6.0;
// The pack the terrain pins past everything it has been owed - the hop,
// the fling, the licence, the line come apart - has one mercy left: the
// head and its whole line are carried down the road they read, onto a
// floor the legs count and under the room a wave is born in. Slow to
// arm and slower to re-arm, that the road, more than the mercy, stays
// the way home.
constexpr double kStuckTeleport = 15.0;
constexpr double kTeleportAgain = 20.0;
constexpr int kTeleportAhead = 12;
// The beat the probe speaks on: a walker the map asked to hear about
// says its piece on this clock and no sooner, that the log the doctor
// reads stays a line, not a scream.
constexpr double kProbeAgain = 2.0;
// The beat the way in's vapour is laid afresh on: the reading re-read,
// the beds re-said, an obstacle answered within the beat - the trail
// follows the ground, and the ground is moved on while the walk is on.
constexpr double kRouteBlink = 0.8;
// The beat every path is said again whole, changed or not: the lines are
// a few bytes the dozen, so saying them all over costs nothing next to a
// client that finished loading mid-wave and otherwise stared at a map
// its ways in were drawn on somewhere else.
constexpr double kTrailResend = 5.0;
// A colour a spawn point: one fixed colour held solid along its whole
// way in - the trail of it and its ping alike - so a wave of many
// fronts is read as many ways in, each named by its own colour; the
// living's own smoke never comes in any of them. Orange, rose, blue,
// yellow, red, green, cyan, purple, lime, teal, one a corner.
constexpr uint32_t kFrontColour[horde::Config::kSpawnSites] = {
    0xFF8000, 0xFF38B8, 0x3098FF, 0xFFE530, 0xFF2828,
    0x40FF40, 0x00F0E8, 0xB050FF, 0xC8FF40, 0x20D0A0};

// The sentry guns' three levels, a colour a level: a family of greens the
// client learns for its own works, the brightness counting from the top
// tier down. Red, yellow and blue stay held for the flamethrower, grenade
// and electric families to come.
constexpr int kTurretLevels = 3;
constexpr uint32_t kTurretColour[kTurretLevels] = {0x00BE00, 0x008C00, 0x005000};
// The blocks the server's guns are placed by: a nobody, whose colour the
// clients take from the last colour the nobody was said to wear, so the
// level's own colour goes out before the first block that wears it.
constexpr uint8_t kTurretBlockId = 255;

// The colour of an acid pool, and the nobody its blocks are painted by:
// the other free server id, its own word, that the sentry colours never
// wear.
constexpr uint32_t kAcidColour = 0x7F000000u | 0x5AFF3Cu;   // acid green
constexpr uint8_t kAcidBlockId = 254;
// The pool is a peril while it holds: the paint burns at whoever stands
// their ground inside it, a share of a grenade hurt a beat, until the
// melt takes the blocks out from under them - and the cells come down
// one a crumble apart once the melt clock runs out, the ground saying
// so a cell at a time rather than one lone collapse of the paint.
constexpr float kAcidBeat = 0.5f;
constexpr float kAcidCrumble = 0.12f;
// The acid's own beds of vapour, ids held off the top of the bed
// budget the way in splits: a landing claims a pair, one bed the
// burst and one the pool, and the pairs are handed out round the
// band, so a landing lays its vapour over a bed an older landing
// wore and no band of the way in ever wears a splash for its own.
constexpr uint8_t kAcidBeds = 10;
constexpr uint8_t kAcidBedFirst = 255 - kAcidBeds;
// The healer's vapour beside the acid's: beds of pale blue laid at the
// feet of the walking dead its reach holds, thinned a blink after the
// beat refreshes them. Their band sits under the acid's own ten, clear
// of the ids the route and the landings share.
constexpr float kHealBeat = 0.5f;
constexpr uint8_t kHealBeds = 30;
constexpr uint8_t kHealBedFirst = kAcidBedFirst - kHealBeds;
constexpr uint8_t kHealRed = 0xA8, kHealGreen = 0xC8, kHealBlue = 0xFF;

// The catch of the pack: a follower runs once its leader stands more than
// this far ahead. Every heel of the line walks at the same pace as the one
// it holds, so the ground never closes a gap on its own - whatever a hop, a
// fling or a wave's first streaming out of the pile put between two walkers
// stays between them, and the column rides strung out at whatever spacing
// history happened to leave it. The run is the physics' own sprint, a third
// again the walk: the leeway closes at that rate while the column keeps its
// pace, and inside the leeway the follower walks the trail as it always has.
// A run cannot climb - the sprint spends the step a walker otherwise takes
// in its stride - so it is kept for the floor its ahead stands on as well:
// at a stair, a bank, a sill, the pack crosses the way it always did.
constexpr float kPackCatch = 0.8f;

// The hunt and the charge, the measures of the brute minds: the run a
// hunter runs while the hunted is a long way off, and the distance that
// turns the charge back to a walk. The hop and the fling in between stay
// the common answer every walker already owes.
constexpr float kHuntSprint = 6.0f;
constexpr float kBreakerCharge = 24.0f;
// The span a shadow keeps: a healer with nobody to follow takes its
// place behind the walker nearest it inside this many blocks.
constexpr float kHealJoin = 24.0f;
// The stand of the Spitter: the acid works from its own ground, and the
// walker keeps this stand off the hunted, in and out of the field,
// while the slow arc does the closing - a ride the hunted watches come
// and steps aside from.
constexpr float kSpitHold = 30.0f;
// How much afoot a landing reads as a breath shut out: the arc went at
// its person and never arrived, so a wall stood on the way, and the
// stand is worth nothing until the walker has moved to a line the
// acid can ride again.
constexpr float kSpitOffShut = 3.0f;
// The bounds of the ride itself, in flight seconds: nothing asked
// quicker than a hop off the lip, and none longer than the ceiling
// of fuse a client flies - a bolt crosses three seconds of sky and
// no more, so a ride asked longer never comes, and the pace of the
// ride is what the tuning slows.
constexpr float kSpitMinFlight = 0.6f;
constexpr float kSpitMaxFlight = nade::kMaxFuse;

// A print stepped past at this width of its side is trodden too. The
// half-pace disc catches a walker that comes to its print - but a heel at
// a run carries its own turning: it can cross the trail's end skimming
// past the disc without entering it, and a print it has already overtaken,
// standing behind it now, is owed a step of doubling back - a zigzag in
// the open that spends the whole run's gain and more. A print the walker
// has passed inside this width of its plane is spent like one stood on.
// The footsteps keep their exactness where it matters: a wall is met
// before its print's plane is crossed, so the print on the doorway's far
// side stays owed until the door is truly walked through.
constexpr float kPrintShoulder = 1.0f;

// The reach a print is spent within - the width of the miss a walker may
// make and still have paid for it. A trail bends, and the one that takes
// a bend at a run carries its turn wide of the very print the bend stood
// on: within half a pace of it the leg never came, and outside the
// shoulder's plane the leg never crossed, so the print stood owed
// behind a walker already past it - a step of doubling back, then
// written into that walker's own prints for every heel behind it to
// walk over again, the one zigzag become the whole line's. So the
// half-pace disc was a stamp collector's: exact, and exacting. The
// print is spent within this reach instead, and the miss a bend made
// costs the walk nothing. The prints keep their exactness where it
// counts: the trail still falls every half a pace, and the doorway's
// print stands a pace past the wall's far face, so a door is walked
// through still, and not crossed at the wall.
constexpr float kPrintTouch = 0.7f;

// A print owed abreast of the walker, or behind it, is the print of a
// miss - a bend carried wide, a bank that threw the leg off the trail -
// and paying it in full costs the line the walk back and the walk again
// over the same ground, both printed for every heel behind to replay:
// one miss become the whole line's zigzag. So the print is aimed at
// only while it stands ahead of the walker by this much of the bearing
// to the one it follows; owed at less, the walker has strayed, and it
// is the field that leads it until the line takes it back.
constexpr float kPrintAside = 0.5f;

// The heel's own sight: a follower this close to the one it follows is
// not stuck - it is parked. The line's heel holds its place by stirring
// on the spot, a print owed it every half a pace, and the held heel is
// the shape the line itself has settled into. The window, reading that
// stirring as a standstill, hopped it on the spot and flung it through
// the backs of its own - the vault that clears a wall shoving aside a
// walker that was standing where it belonged - and the line zigged and
// zagged at the price of all the run had gained on it. So within this
// sight of the one ahead the window is shelved: the follower parks, and
// its ahead walks on past the sight the window takes it back - the first
// hop and then the vault, for a wall behind a heel still gives way.
constexpr float kHeelSight = 3.0f;

// The number of the next heel print: from one, so a zero marks an empty
// slot and never a print. The count outlives every walk the world keeps.
uint32_t heel_stamp() {
    static uint32_t n = 0;
    if (++n == 0) n = 1;
    return n;
}

// towerdefense.toml: its own few mercies - the whitespace of a line
// off both ends, and a word folded to lower case, so a file the host
// writes reads the same whatever hand it was written with.
static std::string trimmed(std::string s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}
static std::string lowered(std::string s) {
    for (char& c : s)
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}

double now_sec() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

bool finite3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool near3(const Vec3& a, const Vec3& b, float d) {
    return std::fabs(a.x - b.x) < d && std::fabs(a.y - b.y) < d && std::fabs(a.z - b.z) < d;
}

// --- the guns: pyspades/weapon.py -------------------------------------------
//
// The magazine is modelled because the replies depend on it: a reload that
// finishes is announced to its owner with the new clip and reserve, and that
// packet is how the client learns the reload is over. The numbers live in
// WeaponTiers.h - one row a rung of the ladder the store sells, rung 0 the
// classic table - so the client that aims and reloads by the same table can
// never disagree with the server that pays the damage out of it.
constexpr int kClipTolerance = 10;

struct Gun {
    int type = 0;
    int tier = 0;   // the ladder's rung: 0 the stock gun, 1-3 the store's
    int ammo = 10, stock = 50;
    // Rounds fired, ever, for the aim scenarios' accuracy (see fired_by).
    int fired = 0;
    bool shoot = false, reloading = false;
    double shoot_time = 0, next_shot = 0, reload_at = 0;

    const training::tiers::GunSpec& spec() const {
        return training::tiers::kGuns[type][tier];
    }
    void reset() {
        shoot = false;
        reloading = false;
        ammo = spec().clip;
        stock = spec().stock;
    }
    void restock() { stock = spec().stock; }
    int get_ammo(double now, bool no_max = false) const {
        int a = ammo;
        if (shoot) a = ammo - std::max(0, static_cast<int>(std::ceil((now - shoot_time) / spec().delay)));
        return no_max ? a : std::max(0, a);
    }
    bool is_empty(double now) const { return get_ammo(now, true) < -kClipTolerance || !shoot; }
    void set_shoot(bool v, double now) {
        if (v == shoot) return;
        if (v) {
            if (ammo <= 0) return;
            if (reloading && !spec().slow) return;
            shoot_time = std::max(now, next_shot);
            reloading = false;
        } else {
            const int before = ammo;
            ammo = get_ammo(now, true);
            next_shot = shoot_time + spec().delay * (before - ammo);
            // No more than the magazine held: a trigger held on an empty one
            // fires nothing.
            fired += std::min(std::max(0, before), std::max(0, before - ammo));
        }
        shoot = v;
    }
    // Rounds fired by `now`, those of a trigger still held included.
    int fired_by(double now) const {
        if (!shoot) return fired;
        return fired + std::min(std::max(0, ammo), std::max(0, ammo - get_ammo(now, true)));
    }
    void reload(double now) {
        if (reloading) return;
        const int a = get_ammo(now);
        if (!stock || a >= spec().clip) return;
        if (spec().slow && shoot && a) return;
        set_shoot(false, now);
        reloading = true;
        ammo = a;
        reload_at = now + spec().reload_time;
    }
    // True when a reload step finished just now: the owner is told.
    bool update(double now) {
        if (!reloading || now < reload_at) return false;
        reloading = false;
        if (spec().slow) {
            ammo += 1;
            stock -= 1;
            reload(now);
        } else {
            const int left = std::max(0, stock - (spec().clip - ammo));
            ammo += stock - left;
            stock = left;
        }
        return true;
    }
};

// world_c.cpp cube_line: the block line every client draws, voxel for voxel.
std::vector<Cell> cube_line(int x1, int y1, int z1, int x2, int y2, int z2) {
    std::vector<Cell> out;
    Cell c{x1, y1, z1};
    const int ddx = x2 - x1, ddy = y2 - y1, ddz = z2 - z1;
    const int sx = ddx < 0 ? -1 : 1, sy = ddy < 0 ? -1 : 1, sz = ddz < 0 ? -1 : 1;
    const long kFar = 0x3fffffff / 512;
    long dx, dy, dz, dxi, dyi, dzi;
    auto la = [](long v) { return v < 0 ? -v : v; };
    if (la(ddx) >= la(ddy) && la(ddx) >= la(ddz)) {
        dxi = 1024; dx = 512;
        dyi = !ddy ? kFar : la(ddx * 1024L / ddy); dy = dyi / 2;
        dzi = !ddz ? kFar : la(ddx * 1024L / ddz); dz = dzi / 2;
    } else if (la(ddy) >= la(ddz)) {
        dyi = 1024; dy = 512;
        dxi = !ddx ? kFar : la(ddy * 1024L / ddx); dx = dxi / 2;
        dzi = !ddz ? kFar : la(ddy * 1024L / ddz); dz = dzi / 2;
    } else {
        dzi = 1024; dz = 512;
        dxi = !ddx ? kFar : la(ddz * 1024L / ddx); dx = dxi / 2;
        dyi = !ddy ? kFar : la(ddz * 1024L / ddy); dy = dyi / 2;
    }
    if (sx >= 0) dx = dxi - dx;
    if (sy >= 0) dy = dyi - dy;
    if (sz >= 0) dz = dzi - dz;
    for (;;) {
        out.push_back(c);
        if (out.size() >= 64) return out;
        if (c.x == x2 && c.y == y2 && c.z == z2) return out;
        if (dz <= dx && dz <= dy) {
            c.z += sz;
            if (c.z < 0 || c.z >= 64) return out;
            dz += dzi;
        } else if (dx < dy) {
            c.x += sx;
            if (c.x < 0 || c.x >= 512) return out;
            dx += dxi;
        } else {
            c.y += sy;
            if (c.y < 0 || c.y >= 512) return out;
            dy += dyi;
        }
    }
}

std::string sanitise_name(std::string s) {
    std::string out;
    for (char ch : s) {
        if (ch == '%' || ch == '#' || ch == '\r' || ch == '\n' || ch == 0) continue;
        out.push_back(ch);
    }
    if (out.size() > 15) out.resize(15);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

struct Player {
    ENetPeer* peer = nullptr;
    int id = -1;
    // Map transfer: the compressed map, how much is sent, and what was
    // broadcast meanwhile that this player must see after it.
    std::shared_ptr<const std::vector<uint8_t>> map_data;
    // The map revision this player needs at least: edits after it reach them
    // through `saved`, edits before it must be in the snapshot they are sent.
    uint64_t need_rev = 0;
    size_t map_off = 0;
    bool loading = false;
    std::vector<std::vector<uint8_t>> saved;

    bool named = false;
    std::string name;
    int team = -2;          // -2 not chosen yet, -1 spectator, 0 blue, 1 green
    int weapon = 0;
    int tool = kToolWeapon;
    uint32_t color = 0x707070;   // 0xRRGGBB
    int kills = 0;
    // The coin of the horde, the Zedcred: what a kill of the walking dead
    // pays, what a sentry or its raising costs, kept plain on the player.
    int zc = 0;                  // Zedcreds in hand
    bool zc_staked = false;      // the purse opened once, at the first rise
    // The store's own account: which gun a player has bought open, the
    // damage bands they have paid for, and the perks and plating that
    // stand. Levels are times bought, indexed by StoreCatalog::kItems.
    // They survive death, and are never given back.
    uint8_t store_level[store::kItemCount] = {};
    int gun_damage_bonus[3] = {};  // paid damage bands, by gun
    bool weapon_open[3] = {true, false, false};  // the rifle is always yours
    int max_hp_bonus = 0;          // Tough skin's standing
    int speed_bonus_pct = 0;       // Light feet's standing
    // One of the walking dead worth more than a grunt wears its own outfit
    // and pays its own bounty; a grunt wears 64 and pays the rate.
    uint8_t outfit = 64;         // the outfit the client dresses it with
    int bounty = 0;              // a special asks it; a grunt pays the rate

    bool alive = false;
    int hp = 0;
    // The dust of the hurts a tank stands: the share a volley pays in
    // rounds, come whole a whole at a time.
    double hide_dust = 0.0;
    int grenades = 3, blocks = 50;
    Gun gun;
    physics::Body body;
    physics::Input input;
    bool primary = false, secondary = false;
    Vec3 line_start{};
    bool line_started = false;
    double respawn_at = -1;
    double last_ping = -1e9;

    bool client_info = false;
    // A target the server owns and walks along this lane of the range itself
    // (no connection behind it); -1 for everybody else.
    int lane = -1;
    bool to_far = true;      // which end of the lane it is walking to
    bool ext_teamplay = false;
    bool ext_flashlight = false;   // claimed Flashlight (0x32)
    bool ext_outfit = false;      // claimed Player Outfit (0x35)
    bool ext_smoke = false;       // claimed Smoke (0x3F), version whatever
    bool ext_trail = false;       // claimed Smoke v2: the ways in as paths, not beds
    bool ext_turrets = false;     // claimed Smoke v3: the sentry guns, drawn over their blocks
    bool ext_piemenu = false;    // claimed Pie Menu (0x31): the wheel served whole
    // The walk and jump flags the zombies' clients were last told, and the
    // bite whose swing packet has yet to be closed. Real players tell the
    // clients these things themselves; the horde is told for them.
    uint8_t net_input = 0;
    bool swing_shown = false;
    double swing_until = 0.0;

    // Aim: a target of the scenario, which the server moves itself.
    bool aim_bot = false;
    int far_lane = -1;             // Long range's lane
    bool has_spawn_at = false;     // where it comes back: a ground voxel
    Vec3 spawn_at{};
    double appeared_at = 0;        // when it last spawned, for the time to kill
    double respawn_delay = 0;
    int strafe = 0;                // -1, 0 or 1: which way it is stepping
    double strafe_until = 0, next_jump = 0, recover_at = 0;
    float home_y = 0;
    // A person: when they were last restocked, and the shot their last hit
    // belonged to (a shotgun's pellets arrive together).
    double last_restock = -1e9;
    double last_hit_at = -1e9;
    bool head_this_shot = false;
    // Tutorial: the steps done (bits of training::Step) and the one pinged.
    int steps_done = 0;
    int pinged_step = -1;
    bool lamp = false;             // its flashlight is lit

    // Horde: one of the wave's walkers. Horde.h holds the brain, this holds
    // its legs and its teeth.
    bool zombie = false;
    double born_at = -1e9;       // when it rose, for the pile's settle window
    double next_bite = 0.0;      // a bite a second once it has one to bite
    // Which brute this walker stands as, one of SpecialKind, or -1 for a
    // plain grunt: the wave dressed it, the behaviour reads it.
    int brute = -1;
    // The leapers are two minds under one skin: most walk with the
    // pack like any grunt, a rare few hunt the living on their own -
    // which one it is, the spawner draws at the turn of the roll.
    bool hunts = false;
    // The hunter's leap: the leg it stands on - none, the flight out to
    // the perch, the coil upon it, the flight of the pounce itself -
    // and, for a flight, the place it lands, the seconds it owes, and
    // the earliest the chase may ask for its perch again.
    int leap_stage = 0;
    double leap_until = -1.0;
    double leap_due = 1.0;
    double leap_ready = 0.0;
    Vec3 leap_to{0.0f, 0.0f, 0.0f};
    // The boomer's wick: when it burns out - -1 keeps it unlit - the
    // beat it last blinked on, and the shade it wears this blink.
    double bomb_at = -1.0;
    double bomb_beat_at = 0.0;
    bool bomb_red = false;
    // A brute stood by the word of a person, not by the wave: it walks
    // and bites all the same, yet the wave keeps its ledger without it.
    bool off_ledger = false;
    // The Spitter clock: when its next breath of acid may leave it.
    double next_spit = 0.0;
    // The last breath gone afoot: how much its landing fell from the
    // aim the arc was owed. Zero says nothing yet - a clean line, the
    // stand holds - and past kSpitOffShut says the wall on the way,
    // which hands the stand back to the road until another line rides.
    float spit_off = 0.0f;
    // The breath told before it flies: the green plume at the head was
    // lit for the spit about to leave, and one window lights it once.
    bool spit_warned = false;
    Vec3 walk_ref{};             // where it last stood, to catch a stuck one
    double walk_ref_at = 0.0;
    double free_slot_at = 0.0;   // dead: when its slot goes back to the wave
    // Its pack: the zombie it follows in single file, or -1 when it reads
    // the field itself - the pack's leader, or the last of a broken line.
    int ahead_id = -1;
    // When it stopped making progress: a stuck one jumps for a way out;
    // the spade comes out only where the dig walk says it pays.
    double stuck_at = -1.0;
    // The walk's distance read at its feet, kept window by window: the
    // moving-stuck's tell. The score is the legs' read where the legs
    // lead, and the spade walk's own where they fall silent and the
    // spade is the one leading. A leader that has not reached a lower
    // reading for kZombieYoyoWindows is pacing - the yo-yo, and the
    // same when neither walk reads anything underfoot - and is owed the
    // standstill's way out. Followers keep no score: a heel that holds
    // its place in the line stirs on the spot by design.
    int foot_dist = -1;
    int stall_windows = 0;
    // The dig watch: the absolute clock the licence reads as its
    // second opinion. stuck_at is stillness, and a milling pack is
    // never still; the watch measures ground instead - dig_watch_dist
    // is the last milestone of a score, and only a reading kDigWatchGain
    // lower than the milestone winds dig_watch_at again. A leader whose
    // watch runs out has milled a whole kDigWatchSeconds without gaining
    // a step of ground, however busy its feet were, and the spade is
    // owed. A leader off every walk - pacing where neither field
    // reads - holds no milestone, and the watch stands down: nowhere
    // to dig, no licence owed for pacing alone.
    double dig_watch_at = -1.0;
    int dig_watch_dist = 0;
    // The detour clock, leader only: the walk and the spade weighed
    // at the feet. A walk held kDigDetour above the crossing the
    // spade reads from the same floor, held for kDigDetourSeconds at
    // a stretch, is a detour the stuck head at the wall owes its
    // spade. The clock runs only while the ratio holds: a march that
    // prices fair, a leg field gone silent, a leader off the walk or
    // a pack of followers stands the clock down, and the road is
    // the road again.
    double detour_at = -1.0;
    // When it last flung itself at the step it could not climb.
    double pounce_at = -1e9;
    // When it last spoke its probe line: the debug a map asks for
    // keeps its peace until this clock comes round again.
    double probe_at = -1e9;
    // When its hunt last spoke its probe line: the chasers carry
    // their own clock, for their beat shares no slot with the
    // breath probe of the spit.
    double hunt_probe_at = -1e9;
    // When its line last rode the last resort down the road: the mercy
    // re-arms slower than anything else the stuck are owed, that the
    // road, more than the carrying, stays the way home.
    double teleport_at = -1e9;
    // When its spade last took a block off what it faces: the pace of the
    // dig, one voxel a dig_seconds, and the tell that tells the unstick
    // its windows apart - a digger stands by design, and the hop and the
    // fling would only spoil its work.
    double spade_at = -1e9;
    // The spawn point it walked in on: the corner its pack belongs to, so
    // a front's trail says itself from its own pack's head, in its own
    // colour. A person's is nobody's.
    int spawn_corner = -1;
    // Its heel prints: the places its feet were set, half a pace apart,
    // kept in a ring in the order they were walked - the pack's memory
    // of the way it holds. The line does not follow the body in front;
    // it follows the ground that body has walked over, so the file
    // crosses a threshold where its leader crossed it and turns where
    // its leader turned, and never takes the open air's straight line
    // across a bend no leg ever made. No print is set aloft: a fling
    // crosses ground no following foot may tread.
    static constexpr int kHeelN = 64;   // paces of prints, in a ring
    float heel_x[kHeelN] = {};
    float heel_y[kHeelN] = {};
    uint32_t heel_seq[kHeelN] = {};    // the print's number, 0 = an empty slot
    int heel_head = 0;                  // where the next print is set
    float heel_ref_x = 0.0f, heel_ref_y = 0.0f;   // the last print's place

    bool spectator() const { return team == -1; }
    bool playing() const { return named && (team == 0 || team == 1); }
};

// The tuning of the sentry guns, as the towerdefense.toml of the host
// says it: the sight of a gun, how often it looks around, the distance
// off the eye the ground may be to found a gun on, and for each level
// the damage a shot does and the seconds between shots; the defaults
// are those of the machine gun family.
// The sentry family walks the classes of the real guns: the light SMG
// that bodies the base gun, its raising to the MG, and at last the
// spinning Minigun - a level buys weight of round or rate of fire,
// never both at once, and no level outclasses a whole park of
// guns standing below it.
struct TurretTuning {
    float range = 48.0f;
    double scan = 0.25;
    float place_range = 8.0f;
    int damage[kTurretLevels] = {2, 5, 5};
    double interval[kTurretLevels] = {0.17, 0.17, 0.066};
};

// The coin of the horde, the Zedcred: a kill pays it, a wave cleared adds
// a bonus on top, and a sentry is bought and raised with it. A new gun
// climbs a flat step by each pillar standing already - the park stays
// buyable as long as the game runs - and a raising is a fixed price the
// park never touches. towerdefense.toml says all of it again, by the same word
// as the tables above.
struct EconomyTuning {
    int per_kill = 1;        // the bounty of a grunt; specials ask more
    int start = 24;          // the purse a first rise: three guns deep
    int wave_bonus = 10;     // plus the number of the wave, on a clear
    int price_base = 5;      // a first gun, before the step
    int price_step = 2;      // by each pillar the builder already stands
    int upgrade[kTurretLevels - 1] = {10, 30};
    int refund = 70;         // percent back at the word that takes down
};

// The brutes of the horde, six specials, each its own face and its own
// toll. A special with a chance set rolls its percentile once a wave for
// a place - the chance itself climbing a notch a wave - and the rolls
// grow by the wave the tuning says. One with no chance stands by count:
// one wave each every_wave, and every wave from the wave its growth
// starts, when one more stands each so many waves, held at a cap. The
// tank keeps a share of each hurt standing; the healer holds the tank's
// waves until the wave the tuning names and walks beside it from there.
// towerdefense.toml says all of it again, by the same word as the table
// here.
enum SpecialKind { kSpitter = 0, kTank, kHealer, kLeaper, kBreaker, kBoomer,
                   kSpecialCount };
struct SpecialTuning {
    const char* name = "";
    int outfit = 64;         // the number the client dresses it with
    int bounty = 0;          // the Zedcreds its kill pays over the grunt rate
    int first_wave = 0;      // the first wave it may stand in
    int chance = 0;          // percent by the wave, when it has a chance
    int chance_grow = 0;     // the chance climbs this much a wave
    int rolls_wave = 0;      // two chances a wave from this wave on
    int rolls_every = 0;     // and one chance more each so many waves
    int every_wave = 0;      // 0: every wave; n: one wave each n
    int grow_wave = 0;       // every wave from here, the count climbing
    int grow_every = 0;      // one more standing each so many waves
    int grows_to = 0;        // the growth holds at this count
    int keeps = 100;         // the share of a hurt it stands, its hide
};

// The breath of the Spitter, as towerdefense.toml says it: how often a
// spit may leave it, the span it picks its person from and the floor
// inside it no arc owes its rider, the lag its aim is read through -
// where a body stood, not where it stands - the pace the arc rides at
// (the slower the ride, the higher the climb its landing owes), the
// share of a grenade hurt its acid does to people, how long the ground
// it lands on is held before it melts, and the reach of the pool
// painted where it lands.
struct SpitterTuning {
    double every = 2.5;    // seconds between its spits
    int range = 30;        // blocks it picks its person from
    int min_range = 3;     // under this the teeth answer, not the pool
    double lag = 0.5;      // seconds back its aim is read at
    double speed = 10.0;   // blocks a second the arc rides at
    double to_people = 0.15;   // of a grenade hurt on people
    double melt = 5.0;     // seconds the paint holds before the melt
    int pool = 1;          // blocks around the landing the pool holds
};

// One placed gun: a pillar of two real blocks the server aims and fires.
// The blocks are the game's own map - walked on, shot at, torn down like
// any wall - and the entity is only the server's own play upon them: what
// it has found to shoot, when it looks next, and the aim as the wire
// quantizes it for the clients that draw the works over the blocks.
struct Turret {
    uint8_t id = 0;              // the wire's own name for it, 1..255
    int level = 1;               // 1..3: an index of the tuning tables
    int x = 0, y = 0;            // the pillar's own column
    int z = 0;                   // the pillar's LOWER block; the upper is z - 1
    int owner = -1;               // the builder, whose the kills are
    int aim_id = -1;              // the walker it is on, when it is on one
    bool locked = false;
    uint8_t yaw_pack = 0, pitch_pack = 0;   // the aim, quantized for the wire
    double next_scan = 0.0, next_fire = 0.0;
    // What its buyer sank into it, base and raisings alike: the word that
    // takes it back down returns part of this to the builder.
    int spent = 0;
};

struct Grenade {
    int owner;
    int team;
    Vec3 pos;
    double explode_at;
    bool acid = false;   // the breath of a Spitter: a pool, not a crater
};

// One price the store's host changed from the default, by the item's
// index in StoreCatalog::kItems.
struct StorePrice {
    int index = -1;
    int price = 0;
};

struct Server::Impl {
    HostConfig cfg;
    ENetHost* host = nullptr;
    bool enet_up = false;
    VoxelMap map;
    SolidMap phys_map;
    std::mt19937 rng{std::random_device{}()};
    std::array<std::unique_ptr<Player>, kSlots> players{};
    std::vector<Grenade> grenades;
    // The melts owed: every cell an acid pool holds, booked to come down
    // when its clock runs out. A fresh world tears the whole host down,
    // so the queue never outlives the map it was booked against.
    struct AcidMelt {
        int x, y, z;
        double at;
    };
    std::vector<AcidMelt> melts;
    // The paint the floor of the world wears, booked to come off: a
    // bedrock cell is painted like any other, but it is never taken,
    // for the floor is not rebuilt - the paint is owed its own colour
    // back when the clock runs out. A fresh world tears the whole host
    // down with the melts, so neither queue outlives its map.
    struct AcidRestore {
        int x, y, z;
        double at;
        uint32_t colour;
    };
    std::vector<AcidRestore> restores;
    // The clock of the pool's burn: the beat the acid next works on
    // whoever stands their ground inside the paint.
    double acid_beat_at = 0.0;
    // The pair of vapour beds the next landing wears, walked round the
    // band each time a spit breaks open.
    uint8_t acid_smoke_next = 0;
    // The healer's own clocks and words: the beat its mend and its beds
    // ride, the beds walked a body a beat, the reach it reads the hurt
    // in; and the breaker's own pace of stroke, apart from the grunt
    // spade its errands ride; and the band around the core, in blocks,
    // the breaker sieges in - its line begins only inside it.
    double heal_beat_at = 0.0;
    uint8_t heal_bed_next = 0;
    float healer_reach = 20.0f;
    double breaker_stroke = 1.5;
    double breaker_dig_range = 50.0;
    // Every leaper is born a hunter of the living; the share in a
    // hundred may soften it back for a gentler chase.
    int leaper_hunt_chance = 100;
    // The hunter's own arc: how long between leaps, how long the
    // coil holds its pounce - the ceiling of a wait held inside a
    // second, that the person earns their beat to read the squat
    // - how distant a perch it aims from the person when the
    // chase reads the ring around them, and how distant the person
    // stands before the two flights begin at all: the chase walks
    // the map until then. The lift is the rise a flight owes the
    // sky, and the rise a perch must stand above the person's own
    // ground.
    double leap_every = 8.0;
    double leap_hold = 1.0;
    int leap_perch = 30;
    int leap_country = 50;
    static constexpr float kLeapLift = 0.25f;
    static constexpr int kPerchRise = 4;
    // The boomer's rounds: the first wave of them, how often they come,
    // and the share of the ramp's dead one stands; then the wick and
    // its reach of the trigger, the last stretch of the wick that
    // blinks plain red, and what the burst takes from the core when
    // it reaches it. The burst itself is the grenade's own, drawn
    // whole from nade::blast_damage: the same box of reach, the same
    // fall with distance, the same wall between the blast and the
    // body.
    int boomer_wave_first = 6;
    int boomer_wave_every = 5;
    int boomer_wave_scale = 60;
    double boomer_fuse = 8.0;
    double boomer_arm = 10.0;
    double boomer_red_lead = 5.0;
    int boomer_core_hurt = 30;
    // The boomer's own short chase: inside this many blocks of a
    // living person, the pack's road to the core ends and the
    // person's own ground begins - the burst is a tooth of contact,
    // and the chase is the road that runs it home.
    double boomer_chase = 30.0;
    // The colours of its flare: the dark orange it walks under, and
    // the plain red its final stretch blinks.
    static constexpr uint32_t kBoomOrange = 0xE06000;
    static constexpr uint32_t kBoomRed = 0xFF2020;
    // The dim mate of each: the blink flips a shade and its mate,
    // orange on orange while the wick runs and red on red once its
    // last stretch begins, that the colour says the phase and the
    // blink says the beat.
    static constexpr uint32_t kBoomOrangeDim = 0x7A3000;
    static constexpr uint32_t kBoomRedDim = 0x8F1010;
    // The ESP Mark flag that says the head itself: MarkFlagPulseHead
    // on a client that knows it, the 1 << 2 bit of the mark word, the
    // marked head growing and returning at the beat the mark keeps.
    // The wire word this rides answers the client enum, not its own
    // count: a 2 here lit the name, the head never heard it.
    static constexpr uint8_t kMarkHeadPulse = 4;
    // A wick pressed against a wall lights after this long a stand.
    static constexpr double kBoomStuck = 1.0;
    // Where the people were, a ring per slot: a sample every kPastBeat,
    // held kPastKeep seconds. The lag of a spit reads a place a body
    // stood this long back, and only the still are caught where they
    // stood.
    static constexpr double kPastBeat = 0.1;
    static constexpr double kPastKeep = 2.0;
    static constexpr int kPastN = 20;
    struct PastPlace {
        double t[kPastN] = {};
        float x[kPastN] = {}, y[kPastN] = {}, z[kPastN] = {};
        int head = 0, n = 0;
    };
    PastPlace past[kSlots];
    double world_time = 0;      // the physics clock: advanced in fixed ticks
    // When the next world update is due: a deadline, not "a period since the
    // last", so a 60Hz update on a 120Hz loop goes out every second pass.
    double next_network = 0;
    // The map as sent to joiners, and the revision it was taken at. map_rev
    // counts edits. Re-encoding the map takes a tenth of a second or more, so
    // it is done on a worker from a snapshot, never on this thread.
    std::shared_ptr<const std::vector<uint8_t>> map_cache;
    uint64_t map_rev = 1, cache_rev = 0;
    std::unique_ptr<VoxelMap> snapshot;
    std::thread map_job;
    std::atomic<bool> job_done{false};
    bool job_running = false;
    uint64_t job_rev = 0;
    std::shared_ptr<const std::vector<uint8_t>> job_result;
    // The dead side wears a darker green, a shade off the grass they
    // walk on, that what moves on the field stays easy to tell apart.
    const uint32_t team_color[2] = {0x0000C4, 0x1B5E20};

    // Aim: the run as the server keeps it; published to cfg.aim every tick.
    training::AimStatus aim;
    double phase_until = 0;
    int shots_base = 0;          // the person's rounds fired when the run began
    int bot_count = 0;
    double next_tutorial_check = 0;
    const uint32_t fog = 0x80E8FF;

    void log(const char* fmt, ...) {
        if (!cfg.log) return;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        cfg.log(buf);
    }

    // --- sending ----------------------------------------------------------
    void send(Player& p, const ByteWriter& bw, bool reliable = true) {
        if (!p.peer) return;
        ENetPacket* pkt = make_packet(bw, reliable);
        if (enet_peer_send(p.peer, 0, pkt) < 0) enet_packet_destroy(pkt);
    }
    static ENetPacket* make_packet(const ByteWriter& bw, bool reliable) {
        return enet_packet_create(bw.data.data(), bw.data.size(),
                                  reliable ? ENET_PACKET_FLAG_RELIABLE
                                           : (ENET_PACKET_FLAG_UNSEQUENCED |
                                              ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT));
    }
    // pyspades broadcast_contained: everybody with an id, except `sender`, on
    // `team` if given. A player still loading the map gets it after the map if
    // `save`, and never otherwise. One packet for everybody: ENet counts its
    // references, so a relay is not a copy per recipient.
    void broadcast(const ByteWriter& bw, bool save, int sender = -1, int team = -2,
                   bool reliable = true) {
        ENetPacket* pkt = nullptr;
        for (auto& pp : players) {
            if (!pp || pp->id == sender) continue;
            if (team != -2 && pp->team != team) continue;
            if (pp->loading) {
                if (save) pp->saved.push_back(bw.data);
            } else if (pp->peer) {
                if (!pkt) pkt = make_packet(bw, reliable);
                enet_peer_send(pp->peer, 0, pkt);
            }
        }
        if (pkt && pkt->referenceCount == 0) enet_packet_destroy(pkt);
    }
    void system_chat(const std::string& text, int to = -1) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::ChatMessage));
        bw.write_u8(255);
        bw.write_u8(static_cast<uint8_t>(proto::ChatType::System));
        bw.write_cstring(text);
        if (to >= 0 && players[to]) send(*players[to], bw);
        else broadcast(bw, false);
    }

    static void write_color(ByteWriter& bw, uint32_t rgb) {
        bw.write_u8(static_cast<uint8_t>(rgb));
        bw.write_u8(static_cast<uint8_t>(rgb >> 8));
        bw.write_u8(static_cast<uint8_t>(rgb >> 16));
    }

    // --- the map ----------------------------------------------------------
    // The map of the tower defense: the .vxl the settings name, the one
    // the game itself carries, or the built course. The settings .txt a
    // map calls its own is read beside whichever of the two it was;
    // the maps of the mode ship their parameters in it.
    bool load_map(std::string& err) {
        if (tower_defense_mode() && !cfg.map_data.empty()) {
            if (!map.load_vxl(reinterpret_cast<const uint8_t*>(cfg.map_data.data()),
                              cfg.map_data.size(), err))
                return false;
            if (!cfg.map_name.empty())
                log("the named map %s rides the game files",
                    cfg.map_name.c_str());
            read_map_settings(cfg.map_settings);
            map.to_solid_map(phys_map);
            return true;
        }
        if (tower_defense_mode() && !cfg.map_file.empty()) {
            std::ifstream in(cfg.map_file, std::ios::binary);
            if (!in) {
                err = "cannot open the map file: " + cfg.map_file;
                return false;
            }
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            if (data.empty()) {
                err = "the map file is empty: " + cfg.map_file;
                return false;
            }
            if (!map.load_vxl(data.data(), data.size(), err)) return false;
            const std::string side = sibling_settings_file(cfg.map_file);
            // The map says its own address, and its settings file theirs:
            // a path the eye believes in and a path the game really opens
            // are two different things, and a settings file the game cannot
            // see reads like settings that were never there.
            log("the tower defense plays the map at <%s>, the settings "
                "beside it at <%s>",
                cfg.map_file.c_str(), side.c_str());
            std::ifstream txt(side, std::ios::binary);
            if (txt) {
                const std::string settings((std::istreambuf_iterator<char>(txt)),
                                           std::istreambuf_iterator<char>());
                read_map_settings(settings);
            } else {
                log("no settings file beside the map: the map stands on "
                    "its own");
                read_map_settings("");
            }
            map.to_solid_map(phys_map);
            return true;
        }
        generate_training_map(map);
        generate_aim_arena(map, static_cast<int>(cfg.scenario));
        map.to_solid_map(phys_map);
        return true;
    }

    // The .txt that stands beside a .vxl from disk, when there is one: the
    // same name with the settings extension on it.
    static std::string sibling_settings_file(const std::string& vxl) {
        std::string side = vxl;
        const size_t dot = side.rfind('.');
        if (dot != std::string::npos) side.resize(dot);
        side += ".txt";
        return side;
    }

    // The settings the mode's own maps carry, the pyspades flavour: a
    // Python-flavoured literal whose extensions dict is the one part read.
    // Keys are quoted words or bare words - a hand that skips the quotes
    // loses nothing, Python reads a bare word the same - and every value
    // a number, a bare true or false, a quoted word, or a list of numbers;
    // the name and the description of the file itself pass over unread (a
    // quote inside a quoted word would lie to a simpler reader), and so is
    // any key the host does not owe. Nothing is owed: a map without the
    // file, or without a key, keeps what it already stood on. A dict that
    // closes early on a sign the reader cannot take says so, for a quiet
    // half read in a field log reads like settings that were never there.
    // The core_position the vocabulary knows is a cell of the map, and it
    // may name the core's foot besides: a third number is the layer the
    // bottom block of its column stands in (61, say, on the bedrock of a
    // room inside the hill), and where the core stands at the map's own
    // cell its column is two blocks from that layer - get_z cannot read
    // a room under a roof, it would name the hill above the core's head.
    void read_map_settings(const std::string& text) {
        map_core_x = map_core_y = map_core_z = -1;
        map_probe = 0;
        map_spawn_x.clear();
        map_spawn_y.clear();
        map_spawn_z.clear();
        if (text.empty()) return;
        auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        auto word_char = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        };
        auto skip_quoted = [&](size_t& i) {
            const char quote = text[i++];
            while (i < text.size() && text[i] != quote) {
                if (text[i] == '\\' && i + 1 < text.size()) ++i;
                ++i;
            }
            if (i < text.size()) ++i;
        };
        size_t at = 0, dict = std::string::npos;
        while ((at = text.find("extensions", at)) != std::string::npos) {
            size_t eq = at + 10;
            if (at > 0 && word_char(text[at - 1])) {
                ++at;
                continue;
            }
            while (eq < text.size() && blank(text[eq])) ++eq;
            if (eq >= text.size() || text[eq] != '=') {
                at += 10;
                continue;
            }
            size_t open = eq + 1;
            while (open < text.size() && blank(text[open])) ++open;
            if (open >= text.size() || text[open] != '{') {
                at += 10;
                continue;
            }
            dict = open;
            break;
        }
        if (dict == std::string::npos) {
            log("map settings: no extensions read");
            return;
        }
        size_t i = dict + 1;
        int keys_read = 0;
        for (;;) {
            while (i < text.size() && (blank(text[i]) || text[i] == ',')) ++i;
            if (i >= text.size() || text[i] == '}') break;
            // A comment the map maker left inside speaks Python: the
            // rest of its own line stays unread, the keys around it live.
            if (text[i] == '#') {
                while (i < text.size() && text[i] != '\n') ++i;
                continue;
            }
            std::string key;
            if (text[i] == '\'' || text[i] == '"') {
                const char quote = text[i++];
                while (i < text.size() && text[i] != quote) {
                    if (text[i] == '\\' && i + 1 < text.size()) {
                        ++i;
                        key += text[i];
                    } else {
                        key += text[i];
                    }
                    ++i;
                }
                if (i >= text.size()) break;
                ++i;
            } else if (word_char(text[i])) {
                // A bare word stands where Python needs no quote: the
                // hand that writes the settings this way loses nothing.
                while (i < text.size() && word_char(text[i])) {
                    key += text[i];
                    ++i;
                }
            } else {
                break;
            }
            while (i < text.size() && blank(text[i])) ++i;
            if (i >= text.size() || (text[i] != ':' && text[i] != '='))
                break;
            ++i;
            while (i < text.size() && blank(text[i])) ++i;
            std::vector<double> ns;
            std::vector<std::vector<double>> rows;
            if (i < text.size() && text[i] == '[') {
                // A list of lists - the sites a map would name its
                // zombies at, each its own list - reads its own outer
                // bracket to the last, for the flat read would stop at
                // the first closing bracket it meets.
                size_t ditto = i + 1;
                while (ditto < text.size() && blank(text[ditto])) ++ditto;
                if (ditto < text.size() && text[ditto] == '[') {
                    size_t shut = i;
                    size_t depth = 0;
                    while (shut < text.size()) {
                        if (text[shut] == '[') ++depth;
                        else if (text[shut] == ']') {
                            if (--depth == 0) break;
                        }
                        ++shut;
                    }
                    if (shut >= text.size()) break;
                    size_t at = ditto;
                    for (;;) {
                        while (at < text.size() &&
                               (blank(text[at]) || text[at] == ','))
                            ++at;
                        if (at >= text.size() || at > shut || text[at] != '[')
                            break;
                        size_t close = at + 1;
                        while (close < shut && text[close] != ']') ++close;
                        if (close >= shut) break;
                        std::vector<double> one;
                        size_t from = at + 1;
                        for (;;) {
                            const size_t comma = text.find(',', from);
                            const std::string row =
                                comma == std::string::npos || comma > close
                                    ? text.substr(from, close - from)
                                    : text.substr(from, comma - from);
                            const std::string cut = trimmed(row);
                            if (!cut.empty()) one.push_back(
                                std::strtod(cut.c_str(), nullptr));
                            if (comma == std::string::npos || comma > close)
                                break;
                            from = comma + 1;
                        }
                        rows.push_back(one);
                        at = close + 1;
                    }
                    i = shut + 1;
                } else {
                    size_t shut = i + 1;
                    while (shut < text.size() && text[shut] != ']') ++shut;
                    if (shut >= text.size()) break;
                    size_t from = i + 1;
                    for (;;) {
                        const size_t comma = text.find(',', from);
                        const std::string one =
                            comma == std::string::npos || comma > shut
                                ? text.substr(from, shut - from)
                                : text.substr(from, comma - from);
                        const std::string cut = trimmed(one);
                        if (!cut.empty())
                            ns.push_back(std::strtod(cut.c_str(), nullptr));
                        if (comma == std::string::npos || comma > shut) break;
                        from = comma + 1;
                    }
                    i = shut + 1;
                }
            } else if (i < text.size() && (text[i] == '\'' || text[i] == '"')) {
                skip_quoted(i);
            } else {
                size_t end = i;
                while (end < text.size() && text[end] != ',' && text[end] != '}' &&
                       !blank(text[end]))
                    ++end;
                const std::string cut = trimmed(text.substr(i, end - i));
                // A bare true or false - the plain words a hand would
                // sooner write than a number - stand for one and nought.
                const std::string cut_l = lowered(cut);
                if (cut_l == "true")
                    ns.push_back(1.0);
                else if (cut_l == "false")
                    ns.push_back(0.0);
                else if (!cut.empty())
                    ns.push_back(std::strtod(cut.c_str(), nullptr));
                i = end;
            }
            ++keys_read;
            const std::string key_l = lowered(key);
            // The sites the map names its zombie fronts at, site by
            // site: a pair names a cell, a triple names the ground
            // layer the site stands on besides. The world holds ten
            // sites at most - the first ten stand.
            if (key_l == "zombie_spawn_position" && !rows.empty()) {
                for (const std::vector<double>& row : rows) {
                    if (row.size() < 2) continue;
                    if (map_spawn_x.size() >=
                        static_cast<size_t>(horde::Config::kSpawnSites))
                        break;
                    map_spawn_x.push_back(static_cast<int>(row[0]));
                    map_spawn_y.push_back(static_cast<int>(row[1]));
                    map_spawn_z.push_back(row.size() >= 3
                                             ? static_cast<int>(row[2]) : -1);
                }
            } else if ((key_l == "core_position" || key_l == "core") &&
                       ns.size() >= 2) {
                map_core_x = static_cast<int>(ns[0]);
                map_core_y = static_cast<int>(ns[1]);
                if (ns.size() >= 3) map_core_z = static_cast<int>(ns[2]);
            } else if (key_l == "zombie_debug" && !ns.empty()) {
                map_probe = ns[0] >= 0.5 ? 1 : 0;
            }
        }
        // A dict that closes early - one stray sign and the rest of it
        // dies unread - says so here, loud: the half the reader kept
        // would pass for settings otherwise, and the quiet loss takes a
        // whole session of field logs to find.
        const bool closed_early = i < text.size() && text[i] != '}';
        if (closed_early)
            log("map settings: the extensions dict closed early: "
                "%d keys stood",
                keys_read);
        if (!map_spawn_x.empty())
            log("map settings: %d zombie spawn sites named",
                static_cast<int>(map_spawn_x.size()));
        if (map_core_x >= 0 && map_core_z >= 0)
            log("map settings: core at %d, %d, foot layer %d",
                map_core_x, map_core_y, map_core_z);
        else if (map_core_x >= 0)
            log("map settings: core at %d, %d", map_core_x, map_core_y);
        else
            log("map settings: no core_position; the settings' own cell stands");
        if (map_probe)
            log("map settings: the zombie debug probe speaks for the pinned");
    }
    // The nearest land column to (x, y): the one asked for when its bedrock
    // layer is solid, and the first land found scanning rings out otherwise
    // - a core or a spawn offshore would stand in the water.
    void nearest_land(int& x, int& y) const {
        const auto land = [this](int cx, int cy) { return map.solid(cx, cy, 62); };
        const int cx = std::min(std::max(x, 2), 509), cy = std::min(std::max(y, 2), 509);
        x = cx;
        y = cy;
        if (land(cx, cy)) return;
        for (int r = 1; r <= 509; ++r) {
            for (int dx = -r; dx <= r; ++dx) {
                const int px = cx + dx;
                if (px < 0 || px > 511 || cx != px && false) continue;
                if (cy - r >= 0 && land(px, cy - r)) { x = px; y = cy - r; return; }
                if (cy + r <= 511 && land(px, cy + r)) { x = px; y = cy + r; return; }
            }
            for (int dy = -r + 1; dy <= r - 1; ++dy) {
                const int py = cy + dy;
                if (py < 0 || py > 511) continue;
                if (cx - r >= 0 && land(cx - r, py)) { x = cx - r; y = py; return; }
                if (cx + r <= 511 && land(cx + r, py)) { x = cx + r; y = py; return; }
            }
        }
    }
    bool aim_mode() const { return cfg.mode == training::Mode::Aim; }
    bool tower_defense_mode() const {
        return cfg.mode == training::Mode::TowerDefense;
    }
    // The core's column, built at the start: the cells out of the ground it
    // stands on, and the ground cell it stands over, which all belong to it.
    int core_top_z = 0, core_ground_z = 0, core_height = 3;
    bool core_column_cell(int x, int y, int z) const {
        if (!tower_defense_mode()) return false;
        if (x != horde_cfg.core_x || y != horde_cfg.core_y) return false;
        return z >= core_top_z && z <= core_ground_z - 1;
    }
    // The core stands out of the ground in the colour of its team, a
    // column three blocks tall read off the real ground at its cell:
    // everything a client needs to see where to defend. The intel drawn
    // on top of it, and the blue tent the client draws for the base at
    // the core's own cell, both come from StateData, and the dig and
    // blast vetoes below keep the column standing.
    //
    // Where the foot is named, the column stands on the layer beneath it
    // instead and two blocks tall: the map's own z at the map's own cell
    // (a room inside the hill, whose floor no get_z can read - it names
    // the first solid from above, the hill itself), or the third number
    // of a /core word. The layer the word names is the core's own foot
    // block, the bottom of its column; the rock below is its ground.
    void build_core_column(int foot_z = -1) {
        const int x = horde_cfg.core_x, y = horde_cfg.core_y;
        int ground = 0;
        if (foot_z >= 1 && foot_z <= 62) {
            ground = foot_z + 1;
        } else if (x == map_core_x && y == map_core_y && map_core_z >= 1 && map_core_z <= 62) {
            ground = map_core_z + 1;
        }
        core_height = ground ? 2 : 3;
        core_ground_z = ground ? ground : map.get_z(x, y);
        for (int k = 1; k <= core_height; ++k)
            map.set(x, y, core_ground_z - k, 0x7F3080FFu);
        core_top_z = core_ground_z - core_height;
        horde_cfg.core_top_z = core_top_z;
    }

    // The column of the old column: whatever of it stands is taken down and
    // the wire told, so a core that moved mid game leaves no stub behind.
    // The where of a fresh one is named by the caller; this only erases.
    void erase_core_column() {
        const int x = horde_cfg.core_x, y = horde_cfg.core_y;
        std::vector<Cell> removed;
        auto unstand = [&](int bx, int by, int bz) {
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
            bw.write_u8(kTurretBlockId);
            bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
            bw.write_i32(bx);
            bw.write_i32(by);
            bw.write_i32(bz);
            broadcast(bw, true);
            map.clear(bx, by, bz);
            removed.push_back(Cell{bx, by, bz});
        };
        for (int k = 1; k <= core_height; ++k) {
            const int z = core_ground_z - k;
            if (map.solid(x, y, z)) unstand(x, y, z);
        }
        touched(removed, false);
    }
    // The same, said on the wire: the blocks the column is, worn in the
    // core's colour, the works placed as the guns are placed - in front of
    // the eyes a SetColor, so the colour is said before the blocks that
    // wear it, and a client still loading keeps both.
    void air_core_column() {
        turret_colour_said(0x3080FFu);
        std::vector<Cell> built;
        const int x = horde_cfg.core_x, y = horde_cfg.core_y;
        for (int k = 1; k <= core_height; ++k) {
            const int z = core_ground_z - k;
            if (!map.solid(x, y, z)) continue;
            turret_block_built(x, y, z);
            built.push_back(Cell{x, y, z});
        }
        touched(built, true);
    }
    // The horde's brain, and what its host remembers between its ticks.
    horde::Config horde_cfg;
    // The cell the map's own settings .txt named, or -1 when it did not:
    // a map that knows where its towns stand overrides the cell the
    // settings would have it at - and the layer it may name besides is
    // the one the core's foot block stands in at that place, above the
    // bedrock of the room its town hides in, named so the core can stand
    // inside and not on the hill above it.
    int map_core_x = -1, map_core_y = -1, map_core_z = -1;
    // The map asking for the pinned walkers to say their piece in the
    // log, one ladder a walker a probe beat: the settings of the map
    // carry the switch, for the stuck a map cannot see stays stuck
    // until the one reading it learns why.
    int map_probe = 0;
    // The spawn sites the map settings list, when they list any (the
    // zombie_spawn_position key): the fronts come from these cells, in
    // the order the file lists them, and no site is added beyond them.
    std::vector<int> map_spawn_x, map_spawn_y, map_spawn_z;
    // The person the hunters all walk to, the one the hunt field floods
    // from - named afresh by the hunting horde each tick it ticks.
    Player* hunt_focus_ = nullptr;
    std::unique_ptr<horde::Horde> horde;
    horde::Phase horde_told = horde::Phase::Prep;
    int horde_wave_told = 0;
    // The record book: the best wave a map has stood, kept in
    // a file beside the server - one line a map, the same place
    // the tuning file stands - and read again at every start of
    // a world. A bare page holds no line: the first wave that
    // rises writes the first one.
    std::vector<std::pair<std::string, int>> score_book;
    std::string score_own;   // the name this page owns its line
    int score_best = -1;
    int score_stand = -1; // the best this run began against
    bool score_told = false; // the fall of the old best, said once
    bool score_fouled = false; // the cheat words shut the run out
    double core_alarm_at = 0.0;   // "under attack": one line every few seconds
    // The packs: one per spawn point, a single path each - the last joined
    // zombie of a point is the one the next of its pack follows.
    int pack_tail[horde::Config::kSpawnSites] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // When each spawn point's ping next goes out, while that point still
    // streams its wave: one marker every few seconds survives a client's
    // map load, which a single early ping never did.
    double corner_ping_next[horde::Config::kSpawnSites] = {};
    // When the way in's vapour is next laid again: the trail is said in
    // beds of ground vapour, one a blink, and the world is built on
    // while the dead walk it - so the reading is traced over and over
    // and the vapour never says a way in the walk has stopped walking.
    double route_smoke_next = 0.0;
    // The ways in, said as paths: the turns each front's way was last
    // told as, so a reading that changed is the only one sent again -
    // the lines are cheap enough to repeat, but not worth saying a dozen
    // times a second. A front that walks out of the reading is the one
    // owed a clear. Nothing sent yet anywhere: nothing is owed at all.
    std::vector<proto::PathPoint> trail_sent[horde::Config::kSpawnSites];
    bool trail_sent_any[horde::Config::kSpawnSites] = {};
    // When every path is said again whole, whatever happened to them.
    double trail_resend_next = 0.0;
    // The sentry guns: the ones standing, the host's own tuning read for
    // them, the next of the wire's names, and the wire's memory of what it
    // last said - a set that changed is the only one worth saying, the
    // trail's own economy, and a client that finishes loading mid-game is
    // owed it by the same five-second refresh.
    std::vector<Turret> turrets;
    TurretTuning turret_cfg;
    EconomyTuning economy;
    // The six brutes, as the table above reads them; the waves draw the
    // specials of a common wave from it. The boomer owns no place in
    // that plan - it walks only the rounds the tuning gives it whole.
    // The tank wears the plain Zombie set as a placeholder, until its
    // own model stands.
    SpecialTuning specials[kSpecialCount] = {
        {"Spitter", 65, 5, 3, 0, 0, 0, 0, 0, 4, 1, 0, 100},
        {"Tank", 69, 10, 3, 0, 0, 0, 0, 0, 3, 1, 0, 10},
        {"Healer", 67, 8, 10, 0, 0, 0, 0, 0, 10, 3, 0, 100},
        {"Leaper", 66, 6, 7, 40, 4, 10, 3, 0, 0, 0, 0, 100},
        {"Breaker", 68, 7, 8, 60, 0, 0, 0, 0, 0, 0, 0, 100},
        {"Boomer", 70, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    int healer_tank_together = 20;  // both bullies stand from this wave on
    // The store's prices the host changed by word, and what the wire last
    // said each player's purse was, so a change is the only thing sent.
    std::vector<StorePrice> store_prices;
    std::vector<proto::StoreState> store_sent;
    bool store_sent_any = false;
    double store_resend_next = 0.0;
    // The specials the wave rides: one takes the tread of the grunt it
    // stands for, at the place the tread names, until the queue is
    // spent - the dead of a wave walk in single file, and the tread
    // holds a walker either way, whatever its skin.
    std::vector<int> special_due;
    SpitterTuning spitter;
    uint8_t turret_next_id = 1;
    double turret_resend_next = 0.0;
    std::vector<proto::TurretState> turrets_sent;
    bool turrets_sent_any = false;
    // The core as the wire last said it, and when its word is owed whole
    // again: the flag the clients draw as the core moves with it (/core),
    // and the smoke's fifth word is the only packet that can say a move
    // without telling of a game of capture nobody played.
    int core_sent_x = -1, core_sent_y = -1, core_sent_z = -1;
    double core_resend_next = 0.0;
    void serve_maps() {
        if (job_running && job_done.load()) {
            map_job.join();
            job_running = false;
            map_cache = job_result;
            cache_rev = job_rev;
        }
        bool waiting = false;
        for (auto& pp : players) {
            if (!pp || !pp->loading || pp->map_data) continue;
            if (map_cache && cache_rev >= pp->need_rev) start_map(*pp);
            else waiting = true;
        }
        if (!waiting || job_running) return;
        if (!snapshot) snapshot = std::make_unique<VoxelMap>();
        *snapshot = map;
        job_rev = map_rev;
        job_done = false;
        job_running = true;
        map_job = std::thread([this]() {
            job_result = std::make_shared<const std::vector<uint8_t>>(snapshot->save_compressed());
            job_done = true;
        });
    }
    void start_map(Player& p) {
        p.map_data = map_cache;
        p.map_off = 0;
        ByteWriter start;
        start.write_u8(static_cast<uint8_t>(PacketId::MapStart));
        start.write_u32(static_cast<uint32_t>(p.map_data->size()));
        send(p, start);
    }
    void touched(const std::vector<Cell>& cells, bool solid) {
        for (const Cell& c : cells) phys_map.set_block(c.x, c.y, c.z, solid);
        if (cells.empty()) return;
        ++map_rev;
        if (horde) horde->on_map_edited();   // the field follows every edit
    }

    // pyspades get_random_location: a land column in the box, on its top.
    Vec3 random_location(int x1, int y1, int x2, int y2) {
        int x, y;
        map.random_point(x1, y1, x2, y2, rng, x, y);
        return Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(map.get_z(x, y))};
    }

    // --- StateData --------------------------------------------------------
    // Territory control with no territories: the course has nothing to
    // capture, so it is never won. The horde is capture the flag, with
    // nothing to capture: team 0's flag stands for the core, and the
    // server never picks an intel up, so it stands there the whole game.
    // The client draws an intel - Models/MapObjects/Intel.kv6 - at the
    // flag's place, which is what the core is drawn as.
    Vec3 core_intel_pos() const {
        // The plane the column was built to the top of, read off the
        // built thing and not off the ground: get_z would survey the
        // hill above the room a core stands in and plant the flag on the
        // roof.
        return Vec3{static_cast<float>(horde_cfg.core_x) + 0.5f,
                    static_cast<float>(horde_cfg.core_y) + 0.5f,
                    static_cast<float>(core_top_z)};
    }
    // The blue tent the client draws for team 0's base: the checkpoint
    // model its own scene stands at the base's cell, one plane beneath
    // the column's top - the bottom block of the core, whose feet share
    // the ground it stands on. The zombies' side keeps its buried
    // nothing: only the dress of the defence is drawn.
    Vec3 core_base_pos() const {
        return Vec3{static_cast<float>(horde_cfg.core_x) + 0.5f,
                    static_cast<float>(horde_cfg.core_y) + 0.5f,
                    static_cast<float>(core_top_z + 1)};
    }
    Vec3 under_core() const {
        // Inside the bedrock the core stands on, where nothing sees it: the
        // rest of the mode's list, which a client would draw at its zero.
        return Vec3{static_cast<float>(horde_cfg.core_x) + 0.5f,
                    static_cast<float>(horde_cfg.core_y) + 0.5f, 62.9f};
    }
    ByteWriter state_data(int player_id) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::StateData));
        bw.write_u8(static_cast<uint8_t>(player_id));
        write_color(bw, fog);
        write_color(bw, team_color[0]);
        write_color(bw, team_color[1]);
        bw.write_fixed_string("Blue", 10);
        bw.write_fixed_string(tower_defense_mode() ? "Zombies" : "Green", 10);
        if (tower_defense_mode()) {
            bw.write_u8(0);                    // Capture the Flag
            bw.write_u8(0);                    // captures: team 0
            bw.write_u8(0);                    // captures: the horde
            bw.write_u8(10);                   // captures to win: never reached
            bw.write_u8(0);                    // nobody holds an intel
            bw.write_vec(core_intel_pos());    // team 0's flag: the core
            bw.write_vec(under_core());        // team 1's flag, buried
            bw.write_vec(core_base_pos());    // team 0's base: the tent
            bw.write_vec(under_core());        // team 1's base, buried
        } else {
            bw.write_u8(1);
            bw.write_u8(0);
        }
        for (size_t i = 0; i < 16; ++i)
            for (int k = 0; k < 13; ++k) bw.write_u8(0);
        return bw;
    }

    // --- connecting -------------------------------------------------------
    void on_connect(ENetPeer* peer, uint32_t version) {
        if (version != proto::PROTOCOL_VERSION) {
            enet_peer_disconnect(peer, kErrorWrongVersion);
            return;
        }
        int id = -1;
        int count = 0;
        for (int i = 0; i < kSlots; ++i) {
            if (players[i]) ++count;
            else if (id < 0) id = i;
        }
        if (id < 0 || count >= cfg.max_players + bot_count) {
            enet_peer_disconnect(peer, kErrorFull);
            return;
        }
        // ENet's packet throttle drops unreliable packets when the round trip
        // grows, which is a guess about a congested internet link. On
        // loopback it only makes things worse: a deceleration of zero holds the
        // throttle open, and ENet passes the setting to the client's end too.
        enet_peer_throttle_configure(peer, ENET_PEER_PACKET_THROTTLE_INTERVAL,
                                     ENET_PEER_PACKET_THROTTLE_SCALE, 0);
        peer->packetThrottle = ENET_PEER_PACKET_THROTTLE_SCALE;
        auto p = std::make_unique<Player>();
        p->peer = peer;
        p->id = id;
        p->loading = true;
        peer->data = p.get();
        // What arrives after the map: everybody already here, then the state.
        for (auto& o : players) {
            if (!o || !o->named) continue;
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::ExistingPlayer));
            bw.write_u8(static_cast<uint8_t>(o->id));
            bw.write_u8(static_cast<uint8_t>(static_cast<int8_t>(o->team)));
            bw.write_u8(static_cast<uint8_t>(o->weapon));
            bw.write_u8(static_cast<uint8_t>(o->tool));
            bw.write_u32(static_cast<uint32_t>(o->kills));
            write_color(bw, o->color);
            bw.write_cstring(o->name);
            p->saved.push_back(bw.data);
        }
        p->saved.push_back(state_data(id).data);
        p->need_rev = map_rev;
        players[id] = std::move(p);
        Player& pr = *players[id];
        // MapStart goes first, before anything but the handshake; it waits for
        // a map at least as new as this moment (serve_maps).
        if (map_cache && cache_rev >= pr.need_rev) start_map(pr);
        send_handshake(pr);
        log("player #%d connecting", id);
    }
    void send_handshake(Player& p) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::VersionHandshakeInit));
        bw.write_u32(42);
        send(p, bw);
    }
    // Map chunks this pass may still send, across every joining player.
    int chunk_budget = 0;
    void continue_map(Player& p) {
        if (!p.loading || !p.map_data) return;
        // Loopback has no loss and no queue worth speaking of: keep a few
        // hundred KB in flight and the whole map is over in well under a
        // second, where pyspades' ten chunks a frame take several.
        int sent = 0;
        while (p.map_off < p.map_data->size() && sent < 32 && chunk_budget > 0 &&
               p.peer->reliableDataInTransit < 512 * 1024) {
            --chunk_budget;
            const size_t n = std::min(kMapChunk, p.map_data->size() - p.map_off);
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::MapChunk));
            bw.write_bytes(p.map_data->data() + p.map_off, n);
            send(p, bw);
            p.map_off += n;
            ++sent;
        }
        if (p.map_off < p.map_data->size()) return;
        p.map_data.reset();
        p.loading = false;
        for (auto& d : p.saved) {
            ENetPacket* pkt = enet_packet_create(d.data(), d.size(), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(p.peer, 0, pkt);
        }
        p.saved.clear();
        // The store's own word, now that the client is in the world and can
        // hear it: a store that only arrived on the next five-second
        // refresh would leave the store's button hidden for five seconds
        // after the map was drawn.
        store_state_send();
    }

    void on_disconnect(Player& p) {
        if (p.named) {
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::PlayerLeft));
            bw.write_u8(static_cast<uint8_t>(p.id));
            broadcast(bw, true, p.id);
            log("%s left", p.name.c_str());
        }
        players[p.id].reset();
    }

    // --- a player's life --------------------------------------------------
    void set_weapon(Player& p, int weapon) {
        p.weapon = std::min(std::max(weapon, 0), 2);
        p.gun = Gun{};
        p.gun.type = p.weapon;
        p.gun.tier = gun_tier(p, p.weapon);
        p.gun.reset();
    }

    void refill(Player& p) {
        p.hp = 100;
        p.grenades = 3;
        p.blocks = 50;
        p.gun.restock();
    }

    // The gun's own two counts, told to its owner alone: the clip it
    // holds and the reserve behind it, the same word a finished reload
    // speaks, which the owner reads as a plain resync when no reload of
    // theirs is playing. Each count is a byte on the wire, so a reserve
    // past 255 is said as 255.
    void send_gun_state(Player& p) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::WeaponReload));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(static_cast<uint8_t>(std::max(0, p.gun.ammo)));
        const int reserve = std::min(255, std::max(0, p.gun.stock));
        bw.write_u8(static_cast<uint8_t>(reserve));
        send(p, bw);
    }

    // Training never runs out: a person nearly out of reserve ammo, grenades or
    // blocks is given more (Restock: health, grenades, blocks and reserve to
    // full on the client too). Only when nearly out, since a restock plays its
    // sound; not in the middle of a reload, which carries on with what it had.
    void keep_stocked(Player& p) {
        if (!p.peer || !p.alive || p.aim_bot || p.zombie || p.gun.reloading) return;
        const bool low = p.gun.stock <= p.gun.spec().clip || p.grenades <= 0 || p.blocks <= 10;
        if (!low || world_time - p.last_restock < 1.5) return;
        p.last_restock = world_time;
        refill(p);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::Restock));
        bw.write_u8(static_cast<uint8_t>(p.id));
        send(p, bw);
    }

    // A target on its lane of the range, a person at the start.
    Vec3 spawn_location(Player& p) {
        if (p.zombie && p.has_spawn_at) return p.spawn_at;
        if (p.aim_bot && p.has_spawn_at) return p.spawn_at;
        if (tower_defense_mode())
            return random_location(horde_cfg.core_x - 2, horde_cfg.core_y - 2,
                                   horde_cfg.core_x + 3, horde_cfg.core_y + 3);
        if (aim_mode() && !p.aim_bot)
            return Vec3{static_cast<float>(training::kAimSpawnX), static_cast<float>(training::kAimSpawnY),
                        static_cast<float>(map.get_z(training::kAimSpawnX, training::kAimSpawnY))};
        if (p.lane >= 0) {
            const int x = training::kLaneX[p.lane];
            return Vec3{static_cast<float>(x), 256.0f, static_cast<float>(map.get_z(x, 256))};
        }
        return random_location(training::kSpawnX - 2, training::kSpawnY - 2, training::kSpawnX + 3,
                               training::kSpawnY + 3);
    }

    void spawn(Player& p) {
        p.respawn_at = -1;
        if (p.team == -2) return;
        // The purse opens once: the first rise of a person - never a
        // walker, never a range target - is where the opening hand of
        // the economy lands.
        if (tower_defense_mode() && p.playing() && !p.zombie && !p.zc_staked) {
            p.zc_staked = true;
            p.zc = economy.start;
            system_chat(std::to_string(p.zc) + " Zedcreds in hand.", p.id);
        }
        // The ladder's first rungs are free to every defender: a dress
        // that costs nothing is owned by all, granted here rather than
        // merely priced at zero so a player who never opens the store
        // still wears one. The class's stats climb with the rung.
        if (tower_defense_mode() && p.playing() && !p.zombie) {
            bool granted = false;
            for (size_t i = 0; i < store::kItemCount; ++i) {
                const store::Item& it = store::kItems[i];
                if (it.effect != store::kEffectWeaponSkin ||
                    it.tier != 1)
                    continue;
                if (store_price(static_cast<int>(i)) != 0) continue;
                if (p.store_level[i] >= 1) continue;
                p.store_level[i] = 1;
                granted = true;
            }
            if (granted) store_state_send();
        }
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::CreatePlayer));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(static_cast<uint8_t>(p.weapon));
        bw.write_u8(static_cast<uint8_t>(static_cast<int8_t>(p.team)));
        if (!p.spectator()) {
            Vec3 at = spawn_location(p);
            at.x += 0.5f;
            at.y += 0.5f;
            at.z -= 2.4f;
            p.body = physics::Body{};
            p.body.pos = at;
            p.body.eye = at;
            p.body.ori = Vec3{p.team == 0 ? 1.0f : -1.0f, 0, 0};
            p.input = physics::Input{};
            p.net_input = 0;
            p.swing_shown = false;
            p.stuck_at = -1.0;
            p.foot_dist = -1;
            p.stall_windows = 0;
            p.dig_watch_at = -1.0;
            p.detour_at = -1.0;
            p.alive = true;
            p.tool = kToolWeapon;
            if (p.zombie) p.tool = kToolSpade;   // it never fires a shot
            p.gun.type = p.weapon;
            p.gun.tier = gun_tier(p, p.weapon);
            p.gun.reset();
            refill(p);
            bw.write_vec(at);
        } else {
            p.alive = false;
            bw.write_vec(Vec3{0, 0, 0});
        }
        bw.write_cstring(p.name);
        broadcast(bw, true);
        // The slot remembers a face: whoever is no walker stands
        // where one may have stood, and a client that was told a
        // face for the slot still draws it. So the team word is
        // said at their every spawn - the word erases the held
        // face, and the usual parts draw them again.
        if (!p.zombie) {
            ByteWriter outfits_bytes;
            proto::write_set_outfit(outfits_bytes,
                                    static_cast<uint8_t>(p.id),
                                    static_cast<uint8_t>(255));
            for (auto& c : players) {
                if (!c || !c->ext_outfit || c->zombie || c->aim_bot)
                    continue;
                if (c->loading) c->saved.push_back(outfits_bytes.data);
                else send(*c, outfits_bytes);
            }
        }
        if (!p.client_info) send_handshake(p);
        p.appeared_at = world_time;
        if (p.aim_bot) {
            p.home_y = p.body.pos.y;
            p.strafe_until = 0;
            if (cfg.highlight_targets) send_mark(p);
        } else if (p.zombie) {
            p.walk_ref = p.body.pos;
            p.walk_ref_at = world_time;
            p.foot_dist = -1;
            p.stall_windows = 0;
            // Its hands, not a gun: a spade, drawn by the grunts' Tool model.
            ByteWriter tool;
            tool.write_u8(static_cast<uint8_t>(PacketId::SetTool));
            tool.write_u8(static_cast<uint8_t>(p.id));
            tool.write_u8(static_cast<uint8_t>(kToolSpade));
            broadcast(tool, true);
            // A brute stands out of the crowd from the door, its
            // silhouette flared the colour of its skin - and the mark
            // dies with the body, the slot free for the next walker.
            if (p.brute >= 0 && tower_defense_mode())
                send_mark(p, brute_flare(p.brute), 1);
        }
    }

    float respawn_seconds(const Player& p) const {
        if (p.aim_bot) return static_cast<float>(p.respawn_delay);
        return p.lane >= 0 ? kTargetRespawn : cfg.respawn_sec;
    }
    void schedule_respawn(Player& p) {
        if (p.respawn_at < 0) p.respawn_at = world_time + respawn_seconds(p);
    }

    void kill(Player& p, Player* by, int kill_type) {
        if (!p.alive) return;
        if (p.zombie && horde) {
            // A brute stood off the wave pays no toll of it: the ledger
            // counts its own walkers alone.
            if (!p.off_ledger)
                horde->on_zombie_killed();
            // A walker down pays its bounty to the hand that felled it -
            // the shooter, or the builder of a gun, when the gun spent
            // the round - and nothing to a fall of its own.
            const int pay = p.bounty > 0 ? p.bounty : economy.per_kill;
            if (by && by != &p && !by->zombie && !by->aim_bot)
                by->zc += pay;
            // The pack does not lose its way with a fallen mate: whoever
            // followed it follows whoever it followed.
            const int fallen_ahead = p.ahead_id;
            for (auto& qq : players)
                if (qq && qq->zombie && qq->ahead_id == p.id) qq->ahead_id = fallen_ahead;
            p.ahead_id = -1;
        }
        if (p.aim_bot) {
            if (by && !by->aim_bot && aim.phase == training::AimPhase::Running) {
                aim.kills += 1;
                aim.ttk_sum += static_cast<float>(world_time - p.appeared_at);
                aim.ttk_count += 1;
            }
            place_aim_bot(p, true);
        }
        p.alive = false;
        p.hp = 0;
        p.gun.reset();
        p.lamp = false; // every client puts it out on the Kill Action
        float delay = respawn_seconds(p);
        if (p.zombie) delay = 2.0f;   // its slot is freed, not a respawn
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::KillAction));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(static_cast<uint8_t>(by ? by->id : p.id));
        bw.write_u8(static_cast<uint8_t>(kill_type));
        bw.write_u8(static_cast<uint8_t>(std::min(255, static_cast<int>(delay) + 1)));
        broadcast(bw, true);
        if (by && by != &p) by->kills += 1;
        // A boomer dies as it lived: loudly. Any road out of the
        // world ends in the burst, the shot as much as the wick.
        if (p.zombie && p.brute == kBoomer && tower_defense_mode())
            boom_blast(p);
        if (p.zombie) {
            p.respawn_at = -1;
            p.free_slot_at = world_time + delay;
        } else {
            p.respawn_at = world_time + delay;
        }
    }

    void set_hp(Player& p, int value, Player* by, int kill_type, const Vec3* source) {
        // The tank's hide: each hurt leaves it standing a share of
        // itself, and the rounds a volley pays whole come a tenth at a
        // time, gathering on the hide until a whole rounds off. A heal
        // reads the other way and rides through untouched.
        if (p.zombie && p.brute == kTank && value < p.hp) {
            const double owed = p.hide_dust +
                (p.hp - value) *
                specials[static_cast<size_t>(kTank)].keeps / 100.0;
            const int whole = static_cast<int>(owed);
            p.hide_dust = owed - static_cast<double>(whole);
            value = p.hp - whole;
        }
        p.hp = std::max(0, std::min(100, value));
        if (p.hp <= 0) {
            kill(p, by, kill_type);
            return;
        }
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::SetHP));
        bw.write_u8(static_cast<uint8_t>(p.hp));
        bw.write_u8(kill_type == kFallKill ? 0 : 1);
        Vec3 src{0, 0, 0};
        if (source) src = *source;
        else if (by && by != &p) src = by->body.pos;
        bw.write_vec(src);
        send(p, bw);
    }

    void hit(Player& victim, int amount, Player* by, int kill_type) {
        if (!victim.alive) return;
        if (by && by != &victim && by->team == victim.team) return;
        set_hp(victim, victim.hp - amount, by, kill_type, nullptr);
    }

    // --- the range's targets ----------------------------------------------
    // The server's own players, with no connection behind them: they walk a
    // line and fall over, which needs no AI.
    void add_targets() {
        for (int lane = 0; lane < training::kLaneCount; ++lane) {
            int id = -1;
            for (int i = 0; i < kSlots && id < 0; ++i)
                if (!players[i]) id = i;
            if (id < 0) return;
            auto p = std::make_unique<Player>();
            p->id = id;
            p->named = true;
            p->name = "Target " + std::to_string(lane + 1);
            p->team = kTargets;
            p->lane = lane;
            p->client_info = true; // nobody to send a handshake to
            players[id] = std::move(p);
            spawn(*players[id]);
        }
    }
    // Across the corridor and back: face the end it is going to and walk.
    void steer_target(Player& p) {
        auto end = [&] { return p.to_far ? training::kLaneY1 : training::kLaneY0; };
        if (std::fabs(p.body.pos.y - end()) < 0.75f) p.to_far = !p.to_far;
        p.body.ori = Vec3{0.0f, end() > p.body.pos.y ? 1.0f : -1.0f, 0.0f};
        p.input = physics::Input{};
        p.input.mf = true;
    }

    // --- the aim scenarios --------------------------------------------------
    Player* person() {
        for (auto& pp : players)
            if (pp && pp->peer && pp->playing()) return pp.get();
        return nullptr;
    }
    float uniform(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
    int bots_for(training::Scenario sc) const {
        switch (sc) {
            case training::Scenario::Gridshot: return 3;
            case training::Scenario::Flick: return 1;
            case training::Scenario::Tracking: return 1;
            case training::Scenario::StrafeDuel: return 2;
            case training::Scenario::LongRange: return training::kFarLaneCount;
            default: return 1;
        }
    }
    void add_aim_bots() {
        const int n = bots_for(cfg.scenario);
        for (int k = 0; k < n; ++k) {
            int id = -1;
            for (int i = 0; i < kSlots && id < 0; ++i)
                if (!players[i]) id = i;
            if (id < 0) return;
            auto p = std::make_unique<Player>();
            p->id = id;
            p->named = true;
            p->name = "Target " + std::to_string(k + 1);
            p->team = kTargets;
            p->aim_bot = true;
            if (cfg.scenario == training::Scenario::LongRange) p->far_lane = k;
            p->client_info = true;
            players[id] = std::move(p);
            ++bot_count;
            place_aim_bot(*players[id], false);
            spawn(*players[id]);
        }
    }
    // A ground voxel at `x`, `y` in the arena, kept off its walls.
    Vec3 arena_ground(float fx, float fy) {
        const int x = std::min(std::max(static_cast<int>(fx), training::kArenaX0 + 2), training::kArenaX1 - 2);
        const int y = std::min(std::max(static_cast<int>(fy), training::kArenaY0 + 2), training::kArenaY1 - 2);
        return Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(map.get_z(x, y))};
    }
    // Somewhere around the player's spot: `deg` either side of straight ahead,
    // between `d0` and `d1` blocks out.
    Vec3 arena_arc(float deg, float d0, float d1) {
        const float a = uniform(-deg, deg) * 3.14159265f / 180.0f, d = uniform(d0, d1);
        return arena_ground(training::kAimSpawnX + std::cos(a) * d, training::kAimSpawnY + std::sin(a) * d);
    }
    bool clear_of_bots(const Player& self, const Vec3& at, float gap) {
        for (auto& pp : players) {
            if (!pp || pp.get() == &self || !pp->aim_bot || !pp->alive) continue;
            if (std::fabs(pp->body.pos.x - at.x) < gap && std::fabs(pp->body.pos.y - at.y) < gap) return false;
        }
        return true;
    }
    // Where an aim target comes back, and after how long; `pinged` sends the
    // spot ahead of it in Flick.
    void place_aim_bot(Player& p, bool pinged) {
        using training::Scenario;
        Vec3 at{};
        double delay = 0.0;
        for (int tries = 0; tries < 20; ++tries) {
            switch (cfg.scenario) {
                case Scenario::Gridshot: {
                    const int st = std::uniform_int_distribution<int>(0, training::kStandCount - 1)(rng);
                    at = arena_ground(uniform(training::kStandX[st] + 1.0f,
                                              training::kStandX[st] + training::kStandDepth - 1.0f),
                                      uniform(training::kStandY0 + 1.0f, training::kStandY1 - 1.0f));
                    break;
                }
                case Scenario::Flick: at = arena_arc(70.0f, 10.0f, 32.0f); break;
                case Scenario::Tracking: at = arena_ground(training::kAimSpawnX + 20.0f, training::kAimSpawnY); break;
                case Scenario::StrafeDuel: at = arena_arc(45.0f, 15.0f, 35.0f); break;
                case Scenario::LongRange:
                    at = arena_ground(static_cast<float>(training::kFarLaneX[std::max(0, p.far_lane)]),
                                      uniform(training::kFarLaneY0, training::kFarLaneY1));
                    break;
                default: at = arena_arc(45.0f, 15.0f, 30.0f); break;
            }
            if (clear_of_bots(p, at, 3.0f)) break;
        }
        switch (cfg.scenario) {
            case Scenario::Gridshot: delay = 0.0; break;
            case Scenario::Flick: delay = uniform(0.35f, 0.7f); break;
            case Scenario::StrafeDuel: delay = 0.6; break;
            case Scenario::LongRange: delay = 1.0; break;
            default: delay = 0.5; break;
        }
        p.spawn_at = at;
        p.has_spawn_at = true;
        p.respawn_delay = delay;
        if (pinged && cfg.scenario == Scenario::Flick)
            send_ping(Vec3{at.x + 0.5f, at.y + 0.5f, at.z - 1.5f}, static_cast<float>(delay) + 0.2f, 0xFF9020, "");
    }
    // The aim targets' own minds: still, strafing and jumping, or walking.
    void steer_aim_bot(Player& p) {
        using training::Scenario;
        p.input = physics::Input{};
        const Player* who = person();
        // Facing the player, always.
        if (who) {
            float dx = who->body.pos.x - p.body.pos.x, dy = who->body.pos.y - p.body.pos.y;
            const float l = std::sqrt(dx * dx + dy * dy);
            if (l > 1e-3f) p.body.ori = Vec3{dx / l, dy / l, 0.0f};
        }
        switch (cfg.scenario) {
            case Scenario::Tracking:
            case Scenario::StrafeDuel: {
                if (world_time >= p.strafe_until) {
                    const float stop = cfg.scenario == Scenario::StrafeDuel ? 0.3f : 0.1f;
                    p.strafe = uniform(0.0f, 1.0f) < stop ? 0 : (uniform(0.0f, 1.0f) < 0.5f ? -1 : 1);
                    p.strafe_until = world_time + uniform(0.35f, 1.4f);
                }
                // Back towards where it started when it strays too far: whichever
                // way it steps, turned round if that takes it further out.
                const float off = p.body.pos.y - p.home_y;
                // Judged now and then, as a turn takes the body a moment.
                if (std::fabs(off) > 9.0f && world_time >= p.recover_at) {
                    if (p.strafe == 0) p.strafe = 1;
                    else if ((off > 0.0f) == (p.body.vel.y > 0.0f)) p.strafe = -p.strafe;
                    p.strafe_until = world_time + 0.6;
                    p.recover_at = world_time + 0.4;
                }
                p.input.ml = p.strafe < 0;
                p.input.mr = p.strafe > 0;
                if (world_time >= p.next_jump) {
                    p.input.jump = p.body.vel.z >= 0 && p.body.vel.z < 0.017f;
                    p.next_jump = world_time + uniform(1.5f, 4.0f);
                }
                break;
            }
            case Scenario::LongRange: {
                const float end = p.to_far ? training::kFarLaneY1 : training::kFarLaneY0;
                if (std::fabs(p.body.pos.y - end) < 0.75f) p.to_far = !p.to_far;
                p.body.ori = Vec3{0.0f, end > p.body.pos.y ? 1.0f : -1.0f, 0.0f};
                p.input.mf = true;
                break;
            }
            default: break;  // still
        }
    }
    // --- the horde ---------------------------------------------------------
    // The brain's body: the players whose walk and fall it counts. A zombie
    // is a player like a range target is, with no connection behind it. A
    // special of the wave rides the same tread and the same corner, wearing
    // its own face and paying its own bounty - the walk itself is the
    // grunt walk, brute manners to come once the brutes have stood a while.
    // The third word hands the walker in off the wave: a brute stood
    // for a test. It walks and fights the same, and the wave neither
    // counts its life nor waits on its death.
    void add_zombie(const horde::SpawnAt& at, int special = -1,
                    bool off_ledger = false) {
        int id = -1;
        for (int i = 0; i < kSlots && id < 0; ++i)
            if (!players[i]) id = i;
        if (id < 0) {
            if (!off_ledger)
                horde->on_zombie_killed();   // nowhere to stand: skipped
            return;
        }
        const SpecialTuning* st = nullptr;
        if (special >= 0 && special < kSpecialCount)
            st = &specials[static_cast<size_t>(special)];
        auto p = std::make_unique<Player>();
        p->id = id;
        p->named = true;
        p->name = st ? st->name : "Zombie";
        p->team = kTargets;
        p->zombie = true;
        p->born_at = world_time;
        if (st) {
            p->outfit = static_cast<uint8_t>(st->outfit);
            p->bounty = st->bounty;
            p->brute = special;
        }
        // The leaper draws its mind at the door: the walk with the
        // pack or the hunt alone, and the tuning says how rare the
        // hunter is.
        if (special == kLeaper) {
            p->hunts =
                std::uniform_int_distribution<int>(0, 99)(rng) <
                leaper_hunt_chance;
            // A hunter born on the doorstep takes a breath before its
            // first arc: the person earns their eye on the chase.
            p->leap_ready = world_time + 2.0;
        }
        p->off_ledger = off_ledger;
        // Single file behind the pack of its corner: a zombie with
        // nobody to follow is the leader of the pack, and reads the
        // field itself. A brute stood by hand has no corner and no
        // pack: it reads the field alone from its first step.
        const int corner = at.corner;
        const Player* tail = nullptr;
        if (corner >= 0) {
            p->spawn_corner = corner;
            if (pack_tail[corner] >= 0)
                tail = players[static_cast<size_t>(pack_tail[corner])].get();
            pack_tail[corner] = id;
        }
        p->ahead_id = (tail && tail->zombie && tail->alive) ? tail->id : -1;
        p->client_info = true;
        p->has_spawn_at = true;
        p->spawn_at = Vec3{static_cast<float>(at.x), static_cast<float>(at.y),
                           static_cast<float>(at.z)};
        players[id] = std::move(p);
        ++bot_count;
        spawn(*players[id]);
        // Every walker is told its face, the grunts no less than
        // the brutes: the slot a grunt stands in may have worn a
        // brute, and a face the clients still remember dresses
        // the newcomer.
        dress_zombie(*players[id]);
    }
    // A zombie wears its face: told at once to every client that
    // claimed the outfits, held for one still loading its world. The
    // plain grunts are told their word here the same, for the slot a
    // walker stands in may have worn another, and a face the clients
    // still remember would dress the newcomer in it.
    void dress_zombie(Player& z) {
        ByteWriter outfits_bytes;
        proto::write_set_outfit(outfits_bytes,
                                static_cast<uint8_t>(z.id), z.outfit);
        for (auto& c : players) {
            if (!c || !c->ext_outfit || c->zombie || c->aim_bot) continue;
            if (c->loading) c->saved.push_back(outfits_bytes.data);
            else send(*c, outfits_bytes);
        }
    }
    // The fallen keep their slot a moment so their fall is seen, then the rest
    // of the wave may have it.
    void free_dead_zombies() {
        for (auto& pp : players) {
            if (!pp || !pp->zombie || pp->alive || pp->free_slot_at <= 0) continue;
            if (world_time >= pp->free_slot_at) {
                pp.reset();
                --bot_count;
            }
        }
    }
    // --- the sentry guns ---------------------------------------------------
    // A pillar of two real blocks, placed on a block a builder looks at
    // and aimed by the server: the blocks are the game's own map, walked
    // on, shot at, torn down like any wall, and what stands on them is the
    // server's own play upon them. To a client that has not learned the
    // word for it the gun is its blocks: a coloured pillar that hurts,
    // and that is a playable game all by itself. To a client that claimed
    // the smoke's third version the works are drawn over the blocks: the
    // list of guns and the aim the server holds said to it at once (sub
    // 3), and every shot the moment its damage lands (sub 4).

    // Where a gun's works are: the centre of its top block, one block
    // above the ground it stands on.
    Vec3 turret_muzzle(const Turret& t) const {
        return Vec3{static_cast<float>(t.x) + 0.5f, static_cast<float>(t.y) + 0.5f,
                    static_cast<float>(t.z) - 1.0f};
    }

    // The record book of the tower defense: the best wave each map
    // has stood, one line a map in towerdefense_scores.txt, kept beside
    // the server where the tuning file stands and read whole again at
    // every start of a world. A map owns its line by the name the map
    // menu knows - the file sans folder and suffix - a map played
    // straight off the disk names itself the same way, the course the
    // paks carry when no map is named keeps the name it ships with,
    // and the bare course the game draws when there is none of them
    // keeps a word of its own, that no two pages share a line.
    std::string score_name_of() const {
        const std::string* own = &cfg.map_name;
        if (own->empty() && !cfg.map_file.empty()) own = &cfg.map_file;
        std::string name;
        if (own->empty()) {
            name = cfg.map_data.empty() ? "TheCourse"
                                       : "Termina_FieldsTD";
        } else {
            const size_t slash = own->rfind('/');
            const size_t dot = own->rfind('.');
            const size_t from = slash == std::string::npos ? 0 : slash + 1;
            size_t till = own->size();
            if (dot != std::string::npos && dot > from) till = dot;
            if (till > from) name = own->substr(from, till - from);
        }
        if (name.empty()) name = "TheCourse";
        for (char& c : name)
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 &&
                c != '_' && c != '-')
                c = '_';
        return name;
    }

    // The book opens with the world: a line a map and its best wave,
    // said plain - the first word the name, the second the count, and
    // any line that says less skipped without a word, a doubled name
    // keeping the bigger of its counts. No book at all, or one the
    // world cannot read, starts the page bare: the record is a
    // welcome the book owes nobody.
    void load_score_book() {
        score_book.clear();
        score_best = -1;
        score_told = false;
        score_fouled = false;
        score_own = score_name_of();
        std::ifstream in("towerdefense_scores.txt", std::ios::binary);
        if (!in) return;
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        size_t from = 0;
        while (from < text.size()) {
            const size_t nl = text.find('\n', from);
            std::string line = nl == std::string::npos
                ? text.substr(from) : text.substr(from, nl - from);
            from = nl == std::string::npos ? text.size() : nl + 1;
            for (const char mark : {'#', ';'}) {
                const size_t at = line.find(mark);
                if (at != std::string::npos) line.resize(at);
            }
            const size_t name_at = line.find_first_not_of(" \t\r");
            if (name_at == std::string::npos) continue;
            const size_t name_till = line.find_first_of(" \t\r", name_at);
            if (name_till == std::string::npos) continue;
            const std::string name =
                line.substr(name_at, name_till - name_at);
            const size_t wave_at =
                line.find_first_not_of(" \t\r", name_till);
            if (wave_at == std::string::npos) continue;
            const long wave =
                std::strtol(line.c_str() + wave_at, nullptr, 10);
            if (wave <= 0) continue;
            bool placed = false;
            for (auto& slot : score_book)
                if (slot.first == name) {
                    placed = true;
                    if (wave > slot.second) slot.second = wave;
                }
            if (!placed)
                score_book.push_back({name, static_cast<int>(wave)});
        }
        for (const auto& slot : score_book)
            if (slot.first == score_own) score_best = slot.second;
        score_stand = score_best;
        if (score_best > 0)
            log("the record book holds wave %d on %s",
                score_best, score_own.c_str());
    }

    // The book writes whole, every line a map folded back in it
    // again. A book the world cannot write costs a line and nothing
    // else: the run keeps its word in mind either way, and the next
    // world reads whatever the last one left standing.
    void save_score_book() {
        std::ofstream out("towerdefense_scores.txt",
                          std::ios::binary | std::ios::trunc);
        if (!out) {
            log("the record book cannot be written");
            return;
        }
        out << "# The tower defense record book: one line a map, its\n";
        out << "# best wave, kept beside the server, the same place as\n";
        out << "# the tuning file. A line here the game does not say may\n";
        out << "# live here as well.\n";
        for (const auto& slot : score_book)
            out << slot.first << ' ' << slot.second << '\n';
    }

    // A wave the run stands past the book is a record the moment it
    // rises, and the page turns then and there - whatever end the run
    // comes to later, the waves it won stay won. The fall of a best
    // that stood before this run is said to the world the first time
    // this run alone, against the page as the run opened it; past it
    // the book takes its leaves in quiet.
    void note_score_wave(int wave) {
        if (score_fouled) return;   // the cheat words shut the run out
        if (wave <= 0 || wave <= score_best) return;
        if (score_stand >= 0 && !score_told) {
            char text[96];
            std::snprintf(text, sizeof text,
                          "Wave %d: the record falls.", wave);
            system_chat(text);
            score_told = true;
        }
        score_best = wave;
        bool placed = false;
        for (auto& slot : score_book)
            if (slot.first == score_own) {
                placed = true;
                slot.second = wave;
            }
        if (!placed) score_book.push_back({score_own, wave});
        save_score_book();
    }

    // The cheat words, /dosh and /clearwave, buy their help at the
    // price of the run: from the first of them the book hears none of
    // its waves, the page it opened with is stood back as it was, and
    // a record that fell to the cheat is un-fallen with it. The word
    // goes out once, and the rest of the run counts for nothing.
    void foul_score() {
        if (score_fouled) return;
        score_fouled = true;
        if (score_stand >= 0) {
            // The run opened on a page that held a best: it stands back.
            if (score_best > score_stand)
                score_best = score_stand;
            bool placed = false;
            for (auto& slot : score_book)
                if (slot.first == score_own) {
                    placed = true;
                    slot.second = score_stand;
                }
            if (!placed)
                score_book.push_back({score_own, score_stand});
            save_score_book();
        } else if (score_best >= 0) {
            // The run opened on a page with no best of its own: the
            // blank it rose from takes the page back.
            for (auto it = score_book.begin(); it != score_book.end();)
                if (it->first == score_own)
                    it = score_book.erase(it);
                else
                    ++it;
            score_best = -1;
            save_score_book();
        }
        system_chat("The book turns its back on this run.");
    }

    // /dosh: a fistful of Zedcreds out of thin air - at the price the
    // foul above puts on the run it reaches into.
    void dosh_command(Player& p, const std::string& text) {
        const long give = std::strtol(text.c_str() + 5, nullptr, 10);
        if (give <= 0 || give > 100000) {
            system_chat("The dosh reads nothing: /dosh 50", p.id);
            return;
        }
        p.zc += static_cast<int>(give);
        char said[96];
        std::snprintf(said, sizeof said, "%ld more Zedcreds in hand.",
                      give);
        system_chat(said, p.id);
        foul_score();
    }

    // towerdefense.toml beside the server, seen again at every start of
    // a tower defense world: sections under [brackets], `key = value`
    // lines, values a number or a list of numbers in brackets, comments
    // from # or ; to the end of the line. Unknown keys and sections are
    // skipped, a malformed line is skipped, and no file at all changes
    // nothing: the defaults are the game as the fork always played it.
    void load_turrets_tuning() {
        std::ifstream in("towerdefense.toml", std::ios::binary);
        if (!in) {
            log("no towerdefense.toml beside the server: the guns stand "
                "on their own defaults");
            return;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        auto number = [](const std::string& s) {
            return std::strtod(s.c_str(), nullptr);
        };
        auto numbers = [](const std::string& s, std::vector<double>& out) {
            out.clear();
            std::string body = s;
            const size_t open = s.find('['), shut = s.rfind(']');
            if (open != std::string::npos && shut != std::string::npos && shut > open)
                body = s.substr(open + 1, shut - open - 1);
            size_t from = 0;
            for (;;) {
                const size_t comma = body.find(',', from);
                const std::string one = comma == std::string::npos
                                            ? body.substr(from)
                                            : body.substr(from, comma - from);
                const std::string cut = trimmed(one);
                if (!cut.empty()) out.push_back(std::strtod(cut.c_str(), nullptr));
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
        };
        std::string section;
        size_t from = 0;
        while (from <= text.size()) {
            const size_t nl = text.find('\n', from);
            std::string line = nl == std::string::npos ? text.substr(from)
                                                       : text.substr(from, nl - from);
            if (nl == std::string::npos && line.empty() && from > 0) break;
            from = nl == std::string::npos ? text.size() + 1 : nl + 1;
            for (const char mark : {'#', ';'}) {
                const size_t at = line.find(mark);
                if (at != std::string::npos) line.resize(at);
            }
            line = trimmed(line);
            if (line.empty()) continue;
            if (line.front() == '[') {
                const size_t shut = line.find(']');
                section = shut == std::string::npos ? std::string{}
                                                    : lowered(trimmed(line.substr(1, shut - 1)));
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = lowered(trimmed(line.substr(0, eq)));
            const std::string value = trimmed(line.substr(eq + 1));
            if (section == "economy") {
                // The whole of the value as one int; each field then takes
                // its own fence, the floors and caps of the table above.
                const int n = static_cast<int>(number(value));
                if (key == "zedcred_per_kill") {
                    economy.per_kill = std::max(0, n);
                } else if (key == "zedcred_start") {
                    economy.start = std::max(0, n);
                } else if (key == "wave_bonus_base") {
                    economy.wave_bonus = std::max(0, n);
                } else if (key == "turret_price_base") {
                    economy.price_base = std::max(1, n);
                } else if (key == "turret_price_step") {
                    economy.price_step = std::max(0, n);
                } else if (key == "upgrade1_price" || key == "upgrade2_price") {
                    const int i = key == "upgrade1_price" ? 0 : 1;
                    economy.upgrade[i] = std::max(0, n);
                } else if (key == "turret_refund") {
                    economy.refund = std::min(100, std::max(0, n));
                }
                continue;
            }
            if (section == "store") {
                // The store's prices, one card at a time: price_<sku> asks
                // this many Zedcreds for the card the catalogue names. A word
                // the catalogue does not know is skipped, as everywhere
                // here. Free means free, so a 0 is honoured.
                store_prices.clear();
                for (size_t i = 0; i < store::kItemCount; ++i)
                    if (key == std::string("price_") + store::kItems[i].sku) {
                        const int n = static_cast<int>(number(value));
                        if (n >= 0) {
                            StorePrice pr;
                            pr.index = static_cast<int>(i);
                            pr.price = n;
                            store_prices.push_back(pr);
                        }
                        break;
                    }
                continue;
            }
            if (section == "specials") {
                // The brutes of the wave, said by their own words: one
                // table stands, six names read, each field its fence.
                // A hole in the table would send a null word to the
                // scan and fall the server, so the boomer is named too.
                static const char* const kWords[kSpecialCount] = {
                    "spitter", "tank", "healer", "leaper", "breaker",
                    "boomer"};
                int kind = -1;
                for (int k = 0; k < kSpecialCount; ++k)
                    if (key.rfind(kWords[k], 0) == 0) {
                        kind = k;
                        break;
                    }
                // The lone keys stand apart from the brute words: the
                // scan above would name a brute off their prefix and
                // swallow them unread, so they step out of the scan.
                if (key == "healer_with_tank_wave" || key == "healer_reach" ||
                    key == "breaker_stroke" ||
                    key == "breaker_dig_range" || key == "leaper_hunt_chance" ||
                    key == "leaper_leap_every" || key == "leaper_leap_hold" ||
                    key == "leaper_leap_perch" ||
                    key == "leaper_leap_country" ||
                    key == "boomer_wave_first" ||
                    key == "boomer_wave_every" || key == "boomer_wave_scale" ||
                    key == "boomer_fuse" || key == "boomer_red_lead" ||
                    key == "boomer_arm" || key == "boomer_core_hurt" ||
                    key == "boomer_chase")
                    kind = -1;
                if (kind >= 0) {
                    // What the key still holds after the name: the field.
                    const std::string field =
                        key.substr(std::string(kWords[kind]).size());
                    SpecialTuning& s = specials[static_cast<size_t>(kind)];
                    const int n = static_cast<int>(number(value));
                    if (field == "bounty") {
                        s.bounty = std::max(0, n);
                    } else if (field == "first_wave") {
                        s.first_wave = std::max(1, n);
                    } else if (field == "chance") {
                        s.chance = std::min(100, std::max(0, n));
                    } else if (field == "chance_grow") {
                        s.chance_grow = std::max(0, n);
                    } else if (field == "rolls_wave") {
                        s.rolls_wave = std::max(0, n);
                    } else if (field == "rolls_every") {
                        s.rolls_every = std::max(0, n);
                    } else if (field == "every_wave") {
                        s.every_wave = std::max(0, n);
                    } else if (field == "grow_wave") {
                        s.grow_wave = std::max(0, n);
                    } else if (field == "grow_every") {
                        s.grow_every = std::max(0, n);
                    } else if (field == "grows_to") {
                        s.grows_to = std::max(0, n);
                    } else if (field == "keeps") {
                        s.keeps = std::min(100, std::max(1, n));
                    } else if (kind == kSpitter && field == "_spit_every") {
                        spitter.every = std::max(0.1, number(value));
                    } else if (kind == kSpitter && field == "_spit_range") {
                        spitter.range = std::max(1, n);
                    } else if (kind == kSpitter && field == "_spit_min") {
                        spitter.min_range =
                            std::min(spitter.range - 1, std::max(0, n));
                    } else if (kind == kSpitter && field == "_spit_lag") {
                        spitter.lag = std::min(
                            std::max(0.0, number(value)), kPastKeep);
                    } else if (kind == kSpitter && field == "_spit_speed") {
                        spitter.speed = std::max(0.5, number(value));
                    } else if (kind == kSpitter &&
                               field == "_acid_to_people") {
                        spitter.to_people = std::max(0.0, number(value));
                    } else if (kind == kSpitter && field == "_acid_melt") {
                        spitter.melt = std::max(0.0, number(value));
                    } else if (kind == kSpitter && field == "_acid_pool") {
                        spitter.pool = std::min(3, std::max(0, n));
                    }
                } else if (key == "healer_with_tank_wave") {
                    // The wave the two bullies of the count may share.
                    healer_tank_together = std::max(
                        0, static_cast<int>(number(value)));
                } else if (key == "healer_reach") {
                    healer_reach = std::max(
                        4.0f, static_cast<float>(number(value)));
                } else if (key == "breaker_stroke") {
                    breaker_stroke = std::max(0.05, number(value));
                } else if (key == "breaker_dig_range") {
                    breaker_dig_range = std::max(0.0, number(value));
                } else if (key == "leaper_hunt_chance") {
                    leaper_hunt_chance = std::min(
                        100, std::max(0, static_cast<int>(number(value))));
                } else if (key == "leaper_leap_every") {
                    leap_every = std::max(0.5, number(value));
                } else if (key == "leaper_leap_hold") {
                    leap_hold = std::max(0.1, number(value));
                } else if (key == "leaper_leap_perch") {
                    leap_perch = std::min(128, std::max(
                        10, static_cast<int>(number(value))));
                } else if (key == "leaper_leap_country") {
                    leap_country = std::min(128, std::max(
                        20, static_cast<int>(number(value))));
                } else if (key == "boomer_wave_first") {
                    boomer_wave_first = std::max(
                        1, static_cast<int>(number(value)));
                } else if (key == "boomer_wave_every") {
                    boomer_wave_every = std::max(
                        0, static_cast<int>(number(value)));
                } else if (key == "boomer_wave_scale") {
                    boomer_wave_scale = std::min(100, std::max(
                        5, static_cast<int>(number(value))));
                } else if (key == "boomer_fuse") {
                    boomer_fuse = std::max(0.5, number(value));
                } else if (key == "boomer_chase") {
                    boomer_chase = std::max(0.0, number(value));
                } else if (key == "boomer_red_lead") {
                    boomer_red_lead = std::max(0.5, number(value));
                } else if (key == "boomer_arm") {
                    boomer_arm = std::max(1.0, number(value));
                } else if (key == "boomer_core_hurt") {
                    boomer_core_hurt = std::max(
                        0, static_cast<int>(number(value)));
                }
                continue;
            }
            if (section != "turrets") continue;
            if (key == "range") {
                turret_cfg.range = std::max(1.0f, static_cast<float>(number(value)));
            } else if (key == "scan_interval") {
                turret_cfg.scan = std::max(0.05, number(value));
            } else if (key == "place_range") {
                turret_cfg.place_range = std::min(16.0f, std::max(1.0f, static_cast<float>(number(value))));
            } else if (key == "damage" || key == "interval") {
                std::vector<double> ns;
                numbers(value, ns);
                for (size_t i = 0; i < ns.size() && i < kTurretLevels; ++i) {
                    if (key == "damage") {
                        if (ns[i] >= 0) turret_cfg.damage[i] = static_cast<int>(ns[i]);
                    } else {
                        turret_cfg.interval[i] = std::max(0.05, ns[i]);
                    }
                }
            }
        }
        log("towerdefense.toml: turrets range %.0f, scan %.2f, place %.0f, "
            "damage %d/%d/%d, interval %.2f/%.2f/%.2f",
            static_cast<double>(turret_cfg.range), turret_cfg.scan,
            static_cast<double>(turret_cfg.place_range),
            turret_cfg.damage[0], turret_cfg.damage[1], turret_cfg.damage[2],
            turret_cfg.interval[0], turret_cfg.interval[1], turret_cfg.interval[2]);
        log("towerdefense.toml: economy kill %d, start %d, bonus %d, "
            "turret %d+%d, upgrades %d/%d, refund %d%%",
            economy.per_kill, economy.start, economy.wave_bonus,
            economy.price_base, economy.price_step,
            economy.upgrade[0], economy.upgrade[1], economy.refund);
        log("towerdefense.toml: store prices said for %d of %d cards",
            static_cast<int>(store_prices.size()),
            static_cast<int>(store::kItemCount));

        log("towerdefense.toml: specials first waves %d/%d/%d/%d/%d/%d, "
            "bounties %d/%d/%d/%d/%d/%d",
            specials[0].first_wave, specials[1].first_wave,
            specials[2].first_wave, specials[3].first_wave,
            specials[4].first_wave, specials[5].first_wave,
            specials[0].bounty, specials[1].bounty,
            specials[2].bounty, specials[3].bounty,
            specials[4].bounty, specials[5].bounty);
        log("towerdefense.toml: spitter spit every %.1f, span %d-%d, "
            "lag %.1f, speed %.1f, acid %.2f of a hurt, melts in %.1f, "
            "pool %d",
            static_cast<double>(spitter.every), spitter.range,
            spitter.min_range, static_cast<double>(spitter.lag),
            spitter.speed, spitter.to_people, spitter.melt,
            spitter.pool);
    }

    // The store's own words for its prices, said a card a line in
    // store_items.txt beside the server: "sku | name | description |
    // price", the same file the pak ships to the client, so what the
    // shelf says and what the purse pays are the one words the host
    // wrote. The TOML's own word for a card still wins when it wrote
    // one, and a card the file does not name keeps its default.
    void load_store_items_txt() {
        std::ifstream in("store_items.txt", std::ios::binary);
        if (!in) {
            log("no store_items.txt beside the server: the cards stand "
                "on their own prices");
            return;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        auto trimmed = [](const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
                ++a;
            while (b > a &&
                   std::isspace(static_cast<unsigned char>(s[b - 1])))
                --b;
            return s.substr(a, b - a);
        };
        int said = 0;
        size_t at = 0;
        while (at <= text.size()) {
            const size_t end = text.find('\n', at);
            const std::string line =
                text.substr(at, end == std::string::npos
                                   ? std::string::npos
                                   : end - at);
            at = end == std::string::npos ? text.size() + 1 : end + 1;
            const std::string cut = trimmed(line);
            if (cut.empty() || cut[0] == '#') continue;
            // The fourth field is the price; a line without one says
            // no price and only dresses the card's words.
            std::vector<std::string> fields;
            size_t f = 0;
            while (fields.size() < 4) {
                const size_t bar = cut.find('|', f);
                if (bar == std::string::npos || fields.size() == 3) {
                    fields.push_back(trimmed(cut.substr(f)));
                    break;
                }
                fields.push_back(trimmed(cut.substr(f, bar - f)));
                f = bar + 1;
            }
            if (fields.size() < 4) continue;
            const int index = store::find_item(fields[0].c_str());
            if (index < 0) continue;
            const char* from = fields[3].c_str();
            char* to = nullptr;
            const long price = std::strtol(from, &to, 10);
            if (to == from || price < 0) continue;
            bool said_here = false;
            for (const StorePrice& pr : store_prices)
                if (pr.index == index) said_here = true;
            if (said_here) continue;
            StorePrice pr;
            pr.index = index;
            pr.price = static_cast<int>(price);
            store_prices.push_back(pr);
            ++said;
        }
        log("store_items.txt: prices said for %d of %d cards", said,
            static_cast<int>(store::kItemCount));
    }

    // One block's worth of the pillar, aired to the clients as the work of
    // the server's own nobody: a block built by a player that is not one
    // is drawn in the last colour the nobody was said to wear, so the
    // level's colour is said - the SetColor - before the builds that wear
    // it, and the packets a loading client keeps go out in that order.
    void turret_colour_said(uint32_t rgb) {
        ByteWriter c;
        c.write_u8(static_cast<uint8_t>(PacketId::SetColor));
        c.write_u8(kTurretBlockId);
        write_color(c, rgb);
        broadcast(c, true);
    }
    void turret_block_built(int x, int y, int z) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
        bw.write_u8(kTurretBlockId);
        bw.write_u8(static_cast<uint8_t>(kBuildBlock));
        bw.write_i32(x);
        bw.write_i32(y);
        bw.write_i32(z);
        broadcast(bw, true);
    }
    // The acid pool words, the same two the sentry pillar colours are
    // said with: the colour before the builds that wear it, and a build
    // aired over a standing block repaints it, a loading client keeping
    // the order it hears them in.
    void acid_colour_said() {
        ByteWriter c;
        c.write_u8(static_cast<uint8_t>(PacketId::SetColor));
        c.write_u8(kAcidBlockId);
        write_color(c, kAcidColour);
        broadcast(c, true);
    }
    void acid_block_painted(int x, int y, int z) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
        bw.write_u8(kAcidBlockId);
        bw.write_u8(static_cast<uint8_t>(kBuildBlock));
        bw.write_i32(x);
        bw.write_i32(y);
        bw.write_i32(z);
        broadcast(bw, true);
    }
    // The paint come off: the colour said first and the block aired
    // over its own place after, the same two words the paint was laid
    // with, so a client sees the bedrock wear its own face again.
    void acid_block_repainted(int x, int y, int z, uint32_t colour) {
        ByteWriter c;
        c.write_u8(static_cast<uint8_t>(PacketId::SetColor));
        c.write_u8(kAcidBlockId);
        write_color(c, colour);
        broadcast(c, true);
        acid_block_painted(x, y, z);
    }

    // The block under the builder's eye, along the line they look down, out
    // to the place's own reach: the gun goes ON that block, not in front of
    // it - what the crosshair names is the ground the pillar stands on.
    bool turret_aim_hit(const Player& p, int& hx, int& hy, int& hz) const {
        const Vec3& o = p.body.pos;
        const Vec3& d = p.body.ori;
        for (float s = 0.5f; s <= turret_cfg.place_range; s += 0.1f) {
            const int ix = static_cast<int>(std::floor(o.x + d.x * s));
            const int iy = static_cast<int>(std::floor(o.y + d.y * s));
            const int iz = static_cast<int>(std::floor(o.z + d.z * s));
            if (!VoxelMap::valid(ix, iy, iz)) return false;
            if (map.solid(ix, iy, iz)) {
                hx = ix;
                hy = iy;
                hz = iz;
                return true;
            }
        }
        return false;
    }

    // The gun a pillar cell belongs to: both its blocks carry its name.
    int turret_at(int x, int y, int z) const {
        for (size_t i = 0; i < turrets.size(); ++i) {
            const Turret& t = turrets[i];
            if (t.x != x || t.y != y) continue;
            if (z == t.z || z == t.z - 1) return static_cast<int>(i);
        }
        return -1;
    }

    // Whether the gun's own flight is clear to a body: sampled a quarter
    // block a step, the first block and a half stepped over for the pillar
    // the works stand on.
    bool turret_sight(const Vec3& from, const Vec3& to) const {
        const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 1.5f) return true;
        const int steps = std::max(2, static_cast<int>(dist / 0.25f));
        for (int k = 6; k < steps; ++k) {
            const float s = static_cast<float>(k) / static_cast<float>(steps);
            const int ix = static_cast<int>(std::floor(from.x + dx * s));
            const int iy = static_cast<int>(std::floor(from.y + dy * s));
            const int iz = static_cast<int>(std::floor(from.z + dz * s));
            if (map.solid(ix, iy, iz)) return false;
        }
        return true;
    }

    // The aim, quantized for the wire: the word the smoke buries in its
    // third version, so the client that draws the works needs no other.
    // The climb is read with smaller z the higher, the same as everywhere
    // here: a target above reads a negative pitch.
    static uint8_t turret_yaw_pack(float dx, float dy) {
        const double deg = std::atan2(static_cast<double>(dx), static_cast<double>(dy)) *
                           (180.0 / 3.14159265358979323846);
        const long q = std::lround((deg + 180.0) * 128.0 / 360.0);
        return static_cast<uint8_t>(static_cast<unsigned long>(q) & 127UL);
    }
    static uint8_t turret_pitch_pack(float dx, float dy, float dz) {
        const double ground = std::sqrt(static_cast<double>(dx) * dx +
                                        static_cast<double>(dy) * dy);
        const double deg = std::atan2(static_cast<double>(dz), ground) *
                           (180.0 / 3.14159265358979323846);
        long q = std::lround((deg + 80.0) * 127.0 / 160.0);
        if (q < 0) q = 0;
        if (q > 127) q = 127;
        return static_cast<uint8_t>(q);
    }

    // The whole set, as the wire says it.
    std::vector<proto::TurretState> turret_state_now() const {
        std::vector<proto::TurretState> list;
        for (const Turret& t : turrets) {
            proto::TurretState s;
            s.id = t.id;
            s.level = static_cast<uint8_t>(t.level);
            s.x = static_cast<int16_t>(t.x);
            s.y = static_cast<int16_t>(t.y);
            s.z = static_cast<uint8_t>(t.z);
            s.locked = t.locked ? 1 : 0;
            s.yaw_pack = t.yaw_pack;
            s.pitch_pack = t.pitch_pack;
            list.push_back(s);
        }
        return list;
    }

    // The whole set said again: at once when it changed, and whole every
    // five seconds whatever happened, so a client that finished loading
    // mid-game is never left staring at guns it cannot see. A set that
    // is said and unchanged keeps its peace; an empty set that was never
    // said owes nothing.
    void turret_state_send() {
        const std::vector<proto::TurretState> list = turret_state_now();
        if (list.empty() && !turrets_sent_any) return;
        const bool again = world_time >= turret_resend_next;
        if (again) turret_resend_next = world_time + kTrailResend;
        const bool same =
            (list.empty() && turrets_sent.empty()) ||
            (!list.empty() && turrets_sent_any && list.size() == turrets_sent.size() &&
             std::equal(list.begin(), list.end(), turrets_sent.begin(),
                        [](const proto::TurretState& a, const proto::TurretState& b) {
                            return a.id == b.id && a.level == b.level && a.x == b.x &&
                                   a.y == b.y && a.z == b.z && a.locked == b.locked &&
                                   a.yaw_pack == b.yaw_pack && a.pitch_pack == b.pitch_pack;
                        }));
        if (same && !again) return;
        turrets_sent = list;
        turrets_sent_any = !list.empty();
        ByteWriter bw;
        proto::write_turrets(bw, list);
        for (auto& o : players)
            if (o && o->peer && !o->loading && o->ext_turrets && o->playing()) send(*o, bw);
    }

    // The core's own word, the smoke's fifth: the cell the core's column
    // stands on and the plane of its top, so the flag a client draws from
    // its StateData and the flag a client hears about mid game are the
    // same flag. Said at once when the core moved and whole again every
    // five seconds, the guns' own economy - a client that finishes
    // loading mid game is owed it too.
    void core_state_send() {
        if (!tower_defense_mode() || !horde) return;
        const int x = horde_cfg.core_x, y = horde_cfg.core_y;
        const int z = core_top_z;
        const bool again = world_time >= core_resend_next;
        if (again) core_resend_next = world_time + kTrailResend;
        if (!again && x == core_sent_x && y == core_sent_y && z == core_sent_z) return;
        core_sent_x = x;
        core_sent_y = y;
        core_sent_z = z;
        ByteWriter bw;
        proto::write_core(bw, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                          static_cast<uint8_t>(z));
        for (auto& o : players)
            if (o && o->peer && !o->loading && o->ext_turrets && o->playing()) send(*o, bw);
    }

    // The store's own word, the smoke's sixth: one player's purse, the wave
    // and core the world stands at, and what they have bought. Sent to
    // EACH player on their own - a purse differs between two hands - and
    // only on change, with the same five seconds the guns keep, so a
    // client that finishes loading mid game is owed it too.
    void store_state_send() {
        if (!tower_defense_mode()) return;
        std::vector<proto::StoreState> states(players.size());
        for (size_t i = 0; i < players.size(); ++i) {
            // The slots are taken as they are filled, and this runs for every
            // one: an empty one has no purse to say.
            if (!players[i]) continue;
            Player& o = *players[i];
            proto::StoreState& s = states[i];
            s.zedcred = static_cast<uint16_t>((std::max)(0, o.zc));
            s.wave = static_cast<uint8_t>(horde ? horde->wave() : 0);
            s.phase = static_cast<uint8_t>(
                horde ? static_cast<int>(horde->phase()) : 0);
            s.core_hp = static_cast<uint16_t>(
                horde ? (std::max)(0, horde->core_hp()) : 0);
            s.core_max_hp = static_cast<uint16_t>(
                horde ? horde->config().core_hp : 0);
            s.weapon = static_cast<uint8_t>(o.weapon);
            s.grenades = static_cast<uint8_t>(o.grenades);
            s.blocks = static_cast<uint8_t>(o.blocks);
            size_t n = 0;
            for (size_t k = 0; k < store::kItemCount && n < 64; ++k)
                if (o.store_level[k]) {
                    s.owned[n].index = static_cast<uint8_t>(k);
                    s.owned[n].level = o.store_level[k];
                    ++n;
                }
            s.owned_count = static_cast<uint8_t>(n);
        }
        const bool again = world_time >= store_resend_next;
        if (again) store_resend_next = world_time + kTrailResend;
        for (size_t i = 0; i < players.size(); ++i) {
            if (!players[i]) continue;
            Player& o = *players[i];
            if (!o.peer || o.loading || !o.ext_turrets || !o.playing())
                continue;
            const proto::StoreState& s = states[i];
            if (!again && store_sent_any && s == store_sent[i]) continue;
            ByteWriter bw;
            proto::write_store(bw, s);
            if (o.loading)
                o.saved.push_back(bw.data);
            else
                send(o, bw);
        }
        store_sent = std::move(states);
        store_sent_any = true;
    }

    // A pillar taken down: whatever blocks of it still stand go with it,
    // so no coloured stub of a dead gun is left floating, and the wire
    // says the gun is gone by no longer saying it.
    void turret_erase(size_t i) {
        const Turret t = turrets[i];
        std::vector<Cell> removed;
        for (int j = 0; j < 2; ++j) {
            const int z = t.z - j;
            if (!map.solid(t.x, t.y, z)) continue;
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
            bw.write_u8(kTurretBlockId);
            bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
            bw.write_i32(t.x);
            bw.write_i32(t.y);
            bw.write_i32(z);
            broadcast(bw, true);
            map.destroy_point(t.x, t.y, z, &removed);
        }
        touched(removed, false);
        turrets.erase(turrets.begin() + static_cast<long>(i));
        turret_resend_next = 0.0;
    }

    // A pillar said at a new level: the same blocks, wearing the new
    // tier's colour. A build aired over a standing block repaints it, so
    // the blocks need no destroy to say a new colour.
    void turret_repaint(Turret& t) {
        const uint32_t rgb = kTurretColour[static_cast<size_t>(t.level) - 1];
        turret_colour_said(rgb);
        for (int j = 0; j < 2; ++j) {
            const int z = t.z - j;
            if (!map.solid(t.x, t.y, z)) continue;
            map.set(t.x, t.y, z, 0x7F000000u | rgb);
            turret_block_built(t.x, t.y, z);
        }
    }

    // The walker the gun should be on: the nearest one whose body the
    // works can see, the eye first, the chest a half block lower second -
    // a crowd in the lane does not hide the one at its front.
    bool turret_targets(const Turret& t, int& out) const {
        const Vec3 muzzle = turret_muzzle(t);
        const float r2 = turret_cfg.range * turret_cfg.range;
        struct Cand {
            float d2;
            int id;
        };
        std::vector<Cand> candidates;
        for (auto& q : players) {
            if (!q || !q->zombie || !q->alive || q->spectator()) continue;
            const float dx = q->body.pos.x - muzzle.x;
            const float dy = q->body.pos.y - muzzle.y;
            const float dz = q->body.pos.z - muzzle.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > r2) continue;
            candidates.push_back(Cand{d2, q->id});
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
        for (const Cand& c : candidates) {
            const Player& q = *players[static_cast<size_t>(c.id)];
            if (turret_sight(muzzle, q.body.pos) ||
                turret_sight(muzzle, Vec3{q.body.pos.x, q.body.pos.y,
                                          q.body.pos.z + 0.6f})) {
                out = c.id;
                return true;
            }
        }
        return false;
    }

    // Every gun's own moment: the pillars that were dug out from under
    // them go first, then the ones that stand look around on the scan's
    // clock, fire on the interval's, and say themselves when they changed.
    void turret_tick() {
        if (!tower_defense_mode()) return;
        for (size_t i = 0; i < turrets.size();) {
            Turret& t = turrets[i];
            if (!map.solid(t.x, t.y, t.z) || !map.solid(t.x, t.y, t.z - 1)) {
                turret_erase(i);
                continue;
            }
            if (world_time >= t.next_scan) {
                t.next_scan = world_time + turret_cfg.scan;
                int target = -1;
                const bool found = turret_targets(t, target);
                if (found != t.locked) {
                    t.locked = found;
                    if (found) t.next_fire = world_time + 0.1;  // a blink to bring the works round
                }
                t.aim_id = found ? target : -1;
                if (found) {
                    const Player& q = *players[static_cast<size_t>(target)];
                    const Vec3 muzzle = turret_muzzle(t);
                    const float dx = q.body.pos.x - muzzle.x;
                    const float dy = q.body.pos.y - muzzle.y;
                    const float dz = q.body.pos.z - muzzle.z;
                    t.yaw_pack = turret_yaw_pack(dx, dy);
                    t.pitch_pack = turret_pitch_pack(dx, dy, dz);
                }
            }
            if (t.aim_id >= 0) {
                Player* q =
                    t.aim_id < kSlots ? players[static_cast<size_t>(t.aim_id)].get() : nullptr;
                if (q && q->zombie && q->alive && world_time >= t.next_fire) {
                    t.next_fire =
                        world_time + turret_cfg.interval[static_cast<size_t>(t.level) - 1];
                    // The kill is the builder's: the gun only spends it,
                    // and a builder long gone leaves it to the walker's
                    // own fall.
                    Player* credit = nullptr;
                    if (t.owner >= 0 && t.owner < kSlots) {
                        Player* op = players[static_cast<size_t>(t.owner)].get();
                        if (op && op->named && !op->zombie && op->playing()) credit = op;
                    }
                    const Vec3 muzzle = turret_muzzle(t);
                    set_hp(*q, q->hp -
                                   turret_cfg.damage[static_cast<size_t>(t.level) - 1],
                           credit, kWeaponKill, &muzzle);
                    ByteWriter bw;
                    proto::write_turret_shot(bw, t.id);
                    for (auto& o : players)
                        if (o && o->peer && !o->loading && o->ext_turrets && o->playing())
                            send(*o, bw);
                }
            }
            ++i;
        }
        turret_state_send();
        core_state_send();
        store_state_send();
    }

    // The builder's word to the server made good: a pillar on the block
    // the eye names, two cells of free air above it, the map's own rules
    // obeyed, the core's column and the last way in alike. An aim that
    // finds a pillar standing raises it instead, when the word named a
    // higher level; a lower word does not undo a pillar: the way back is
    // the spade, as with any build.
    // What a pillar at `level` asks of this builder: the base and the step by
    // each gun they already stand for a fresh one, or the fixed raisings
    // for one already standing. The store and /turret both ask here, so a
    // gun is one price however the word for it arrives. `standing` is the
    // index of a pillar already on the aimed cell, or -1 for none.
    int turret_price_for(const Player& p, int level, int standing) const {
        level = level < 1 ? 1 : (level > kTurretLevels ? kTurretLevels : level);
        int price = 0;
        if (standing >= 0) {
            const Turret& t = turrets[static_cast<size_t>(standing)];
            for (int l = t.level; l < level; ++l)
                price += economy.upgrade[static_cast<size_t>(l - 1)];
            return price;
        }
        int owned = 0;
        for (const Turret& t : turrets)
            if (t.owner == p.id) ++owned;
        price = economy.price_base + economy.price_step * owned;
        for (int l = 1; l < level; ++l)
            price += economy.upgrade[static_cast<size_t>(l - 1)];
        return price;
    }

    void turret_place(Player& p, int level, std::string& reply) {
        if (!tower_defense_mode()) {
            reply = "Sentry guns are a thing of the tower defense.";
            return;
        }
        if (!p.alive || !p.playing()) {
            reply = "Spawn in before placing sentries.";
            return;
        }
        int hx = 0, hy = 0, hz = 0;
        if (!turret_aim_hit(p, hx, hy, hz)) {
            reply = "Aim at a block to place the sentry on it.";
            return;
        }
        const int standing = turret_at(hx, hy, hz);
        if (standing >= 0) {
            Turret& t = turrets[static_cast<size_t>(standing)];
            if (t.level == level) {
                reply = "That pillar already stands at level " + std::to_string(level) + ".";
                return;
            }
            if (level < t.level) {
                reply = "A pillar is not undone: dig the blocks out instead.";
                return;
            }
            // A raising is a fixed price each level passed, whatever the
            // park around it - the word that takes it down refunds part.
            const int price = turret_price_for(p, level, standing);
            if (p.zc < price) {
                reply = "Not enough Zedcreds: " + std::to_string(price) +
                        " asked, " + std::to_string(p.zc) + " in hand.";
                return;
            }
            p.zc -= price;
            t.spent += price;
            t.level = level;
            turret_repaint(t);
            turret_resend_next = 0.0;
            std::string raised = t.owner == p.id
                                     ? "The pillar is raised to level "
                                     : "Not yours, but raised all the same: ";
            reply = raised + std::to_string(level) + " for " +
                    std::to_string(price) + " Zedcreds; " +
                    std::to_string(p.zc) + " in hand.";
            return;
        }
        if (hz < 2 || hz > 62 || map.solid(hx, hy, hz - 1) || map.solid(hx, hy, hz - 2)) {
            reply = "Not enough room above that block.";
            return;
        }
        if (core_column_cell(hx, hy, hz - 1) || core_column_cell(hx, hy, hz - 2)) {
            reply = "The core's column is not for sentries.";
            return;
        }
        if (horde && (!horde->can_build(hx, hy, hz - 1) || !horde->can_build(hx, hy, hz - 2))) {
            reply = "That place would seal the last way in.";
            return;
        }
        // A new gun is the base price plus the step by each pillar the
        // builder already stands - a park means the next gun costs -
        // and a level above the base adds its raisings on top. The toll
        // is asked before the blocks stand: a purse too thin leaves the
        // ground as it was, and no half bought pillar stays behind.
        const int price = turret_price_for(p, level, -1);
        if (p.zc < price) {
            reply = "Not enough Zedcreds: " + std::to_string(price) +
                    " asked, " + std::to_string(p.zc) + " in hand.";
            return;
        }
        const uint32_t rgb = kTurretColour[static_cast<size_t>(level) - 1];
        turret_colour_said(rgb);
        std::vector<Cell> built;
        if (map.build_point(hx, hy, hz - 1, 0x7F000000u | rgb)) {
            built.push_back(Cell{hx, hy, hz - 1});
            turret_block_built(hx, hy, hz - 1);
        }
        if (map.build_point(hx, hy, hz - 2, 0x7F000000u | rgb)) {
            built.push_back(Cell{hx, hy, hz - 2});
            turret_block_built(hx, hy, hz - 2);
        }
        touched(built, true);
        if (built.empty()) {
            reply = "Not enough room above that block.";
            return;
        }
        // Its name: the next of the wire's own, never one in flight.
        uint8_t id = turret_next_id;
        for (;;) {
            bool used = false;
            for (const Turret& t : turrets)
                if (t.id == id) {
                    used = true;
                    break;
                }
            if (!used) break;
            if (++id == 0) id = 1;
        }
        turret_next_id = id == 255 ? 1 : static_cast<uint8_t>(id + 1);
        Turret t;
        t.id = id;
        t.level = level;
        t.x = hx;
        t.y = hy;
        t.z = hz - 1;
        t.owner = p.id;
        t.spent = price;
        t.next_scan = world_time + 0.1;  // a fresh gun looks around at once
        turrets.push_back(t);
        turret_resend_next = 0.0;
        p.zc -= price;
        reply = "Sentry level " + std::to_string(level) + " placed for " +
                std::to_string(price) + " Zedcreds; " +
                std::to_string(p.zc) + " in hand.";
    }

    // A builder's own pillars taken back down: only their own, the rest
    // of the team's works are not theirs to unsettle.
    // Each one returns a part of what was sunk into it, base and
    // raisings alike.
    int turret_remove_owned(Player& p, int& back) {
        int down = 0;
        back = 0;
        for (size_t i = 0; i < turrets.size();) {
            if (turrets[i].owner != p.id) {
                ++i;
                continue;
            }
            back += turrets[i].spent * economy.refund / 100;
            turret_erase(i);
            ++down;
        }
        if (down > 0)
            p.zc += back;
        return down;
    }

    // The store's own word: /buy <sku> buys one card, /store lists what it
    // carries. The server is the only place a Zedcred is counted, so a
    // client's window is a view: what it asks for is asked here, and
    // answered here with the purse's own answer.
    void store_command(Player& p, const std::string& text) {
        if (!tower_defense_mode()) {
            system_chat("The store stands in a tower defense world.", p.id);
            return;
        }
        if (!p.alive || !p.playing()) {
            system_chat("Spawn in before you buy.", p.id);
            return;
        }
        const std::string rest =
            trimmed(lowered(text).substr(text.size() < 5 ? text.size() : 5));
        if (rest.empty() || rest == "help") {
            store_category_words(p);
            return;
        }
        const int index = store::find_item(rest.c_str());
        if (index < 0) {
            system_chat("No such thing: /buy " + rest, p.id);
            return;
        }
        const store::Item& item = store::kItems[static_cast<size_t>(index)];
        // Already bought to its cap? An unbought card at the cap is a
        // consumable, which may always be bought another of.
        const int have = p.store_level[static_cast<size_t>(index)];
        if (item.max_level != 255 && have >= static_cast<int>(item.max_level)) {
            system_chat("Already bought: " + std::string(item.label), p.id);
            return;
        }
        if (item.prereq != store::kNone &&
            p.store_level[item.prereq] < 1) {
            system_chat("It needs " +
                            std::string(store::kItems[item.prereq].label) +
                            " first.", p.id);
            return;
        }
        const int price = store_price(static_cast<int>(index));
        if (p.zc < price) {
            system_chat("Not enough Zedcreds: " + std::to_string(price) +
                            " asked, " + std::to_string(p.zc) + " in hand.",
                        p.id);
            return;
        }
        // The card is owned the moment the verdict speaks, so the
        // effects read the new rung there: a dress re-reads its gun's
        // ladder with the rung on it, and the gun in hand climbs at
        // once instead of at its next spawn. A card that cannot be
        // carried out is refused, the ownership is taken back, and the
        // purse pays nothing.
        const int rung_before = p.gun.tier;
        ++p.store_level[static_cast<size_t>(index)];
        if (!store_apply(p, index)) {
            --p.store_level[static_cast<size_t>(index)];
            return;
        }
        p.zc -= price;
        // A dress that moved the held gun's rung hands it over loaded:
        // the new magazine is filled and told at once, without a reload
        // asked. The ammo card fills too; until now its fill went
        // unannounced, the owner counting the old rounds until a reload.
        const bool rung_moved = p.gun.tier != rung_before;
        if (rung_moved) p.gun.reset();
        if (rung_moved || item.effect == store::kEffectAmmo)
            send_gun_state(p);
        system_chat(std::string(item.label) + " bought for " +
                        std::to_string(price) + " Zedcreds; " +
                        std::to_string(p.zc) + " in hand.", p.id);
        store_state_send();
    }

    // The words for what stands in the store, said to one player: the
    // three sections, and what each offers.
    void store_category_words(Player& p) {
        std::string text = "The store's shelves: ";
        for (int cat = 0; cat < store::kCategoryCount; ++cat) {
            if (cat) text += ", ";
            text += std::string(store::category_name(
                                    static_cast<uint8_t>(cat))) + " (";
            bool first = true;
            for (size_t i = 0; i < store::kItemCount; ++i)
                if (store::kItems[i].category == cat) {
                    if (!first) text += " ";
                    first = false;
                    text += store::kItems[i].sku;
                }
            text += ")";
        }
        text += ". Say /buy <name> for one; a class wears the best "
                "dress owned.";
        system_chat(text, p.id);
    }

    // The rung of its ladder a class fights at: the best dress tier
    // owned for the gun, never below the ladder's floor of one - the
    // first rung is the class's own starting shape, free to everyone.
    // Zero outside Tower Defense, where the range's guns stay stock.
    // The spade is gun 3: its own ladder is read the same way.
    int gun_tier(const Player& p, int gun) const {
        if (!tower_defense_mode() || gun < 0 || gun > 3) return 0;
        int best = 1;
        for (size_t i = 0; i < store::kItemCount; ++i) {
            const store::Item& it = store::kItems[i];
            if (it.effect != store::kEffectWeaponSkin || it.dress == nullptr)
                continue;
            if (static_cast<int>(it.arg) != gun) continue;
            if (p.store_level[i] < 1 || it.tier <= best) continue;
            best = it.tier;
        }
        return best;
    }

    // The price an item asks, the TOML's word for it when there is one.
    int store_price(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= store::kItemCount)
            return 0;
        const store::Item& item = store::kItems[static_cast<size_t>(index)];
        for (const StorePrice& pr : store_prices)
            if (pr.index == index) return pr.price;
        return item.price;
    }

    // The effect carried out. False when it cannot stand - a core at
    // full health asks for no patch, a gun already carried asks for no
    // ammo - and the caller refuses the buy without a word of it.
    bool store_apply(Player& p, int index) {
        const store::Item& item = store::kItems[static_cast<size_t>(index)];
        switch (item.effect) {
            case store::kEffectWeapon: {
                const int gun = item.arg;
                if (gun < 0 || gun > 2) return false;
                if (p.weapon_open[static_cast<size_t>(gun)]) return false;
                p.weapon_open[static_cast<size_t>(gun)] = true;
                // The gun goes straight to the hands that bought it.
                set_weapon(p, gun);
                return true;
            }
            case store::kEffectGunDamage: {
                const int gun = item.arg;
                if (gun < 0 || gun > 2) return false;
                ++p.gun_damage_bonus[static_cast<size_t>(gun)];
                return true;
            }
            case store::kEffectAmmo: {
                p.gun.restock();
                p.gun.reset();
                return true;
            }
            case store::kEffectGrenades: {
                if (p.grenades >= 8) return false;
                ++p.grenades;
                return true;
            }
            case store::kEffectHealth: {
                if (p.hp >= 100 + p.max_hp_bonus) return false;
                p.hp = 100 + p.max_hp_bonus;
                return true;
            }
            case store::kEffectBlocks: {
                if (p.blocks >= 50) return false;
                p.blocks = std::min(50, p.blocks + 10);
                return true;
            }
            case store::kEffectMaxHp: {
                p.max_hp_bonus += static_cast<int>(item.arg);
                return true;
            }
            case store::kEffectSpeed: {
                p.speed_bonus_pct += static_cast<int>(item.arg);
                return true;
            }
            case store::kEffectCoreRepair: {
                if (!horde) return false;
                const int max_hp = horde->config().core_hp;
                if (horde->core_hp() >= max_hp) return false;
                const int to = std::min(max_hp, horde->core_hp() +
                                                   static_cast<int>(item.arg));
                horde->on_core_damaged(horde->core_hp() - to);
                return true;
            }
            case store::kEffectCoreMaxHp: {
                if (!horde) return false;
                horde->add_core_max_hp(static_cast<int>(item.arg));
                return true;
            }
            case store::kEffectTurret: {
                // The word that places a pillar is the word that asks for
                // its coin, so its own verdict is the store's: it says
                // whether one stands where the eye looks, and takes what
                // it asked. A refusal takes nothing.
                std::string reply;
                turret_place(p, static_cast<int>(item.arg), reply);
                if (reply.rfind("Sentry level", 0) != 0 &&
                    reply.rfind("The pillar is raised", 0) != 0 &&
                    reply.rfind("Not yours", 0) != 0)
                    return false;
                return true;
            }
            case store::kEffectWeaponSkin: {
                // A dress is worn, not fired - but its rung is: the
                // tier's stats take over the gun in hand at once, the
                // magazine growing into the shape the next reload
                // fills. The rest climb at their next spawn.
                const int gun = item.arg;
                if (gun >= 0 && gun <= 2 && gun == p.gun.type) {
                    const int tier = gun_tier(p, gun);
                    if (tier != p.gun.tier) p.gun.tier = tier;
                }
                return true;
            }
            default:
                return false;
        }
    }

    // The builder's own word for a gun: /turret places one of the level
    // the word names, bare /turret the first, and a word that finds a
    // pillar standing raises it instead; /turretremove takes the
    // builder's own pillars down. /sentry says the same as /turret.
    void turret_command(Player& p, const std::string& text) {
        const std::string word = lowered(text);
        if (word == "/turretremove" || word == "/sentryremove") {
            int back = 0;
            const int down = turret_remove_owned(p, back);
            system_chat(down > 0
                            ? std::to_string(down) + " sentries taken down; " +
                                  std::to_string(back) + " Zedcreds back, " +
                                  std::to_string(p.zc) + " in hand."
                            : "You have no sentries.", p.id);
            return;
        }
        const std::string rest =
            trimmed(word.size() < 7 ? word : word.substr(7));
        int level = 1;
        if (!rest.empty()) {
            if (rest[0] >= '1' && rest[0] <= '3') {
                level = rest[0] - '0';
            } else {
                system_chat("usage: /turret [1-3]", p.id);
                return;
            }
        }
        std::string reply;
        turret_place(p, level, reply);
        system_chat(reply, p.id);
    }

    // A brute by the word of a person: the kind named, stood at one of
    // the places the waves themselves are born, drawn of the pool at
    // random - the site map named or one of the built ten - on the very
    // floor the march stands its own walkers on, dressed as its wave
    // dresses it. A testing tool first: the wave keeps its own ledger
    // without it, so its life and its death move no count, and a brute
    // born at a wave door walks the way the waves walk. Says the kinds
    // when the word says none.
    void brute_command(Player& p, const std::string& text) {
        const std::string word = lowered(text);
        const std::string rest =
            trimmed(word.size() < 6 ? word : word.substr(6));
        int kind = -1;
        for (int k = 0; k < kSpecialCount; ++k) {
            const SpecialTuning& s = specials[static_cast<size_t>(k)];
            if (rest == lowered(std::string(s.name))) kind = k;
        }
        if (kind < 0) {
            std::string list;
            for (int k = 0; k < kSpecialCount; ++k) {
                const SpecialTuning& s = specials[static_cast<size_t>(k)];
                const std::string one = lowered(std::string(s.name));
                if (k) list += k + 1 < kSpecialCount ? ", " : " or ";
                list += one;
            }
            system_chat("usage: /brute <" + list + ">", p.id);
            return;
        }
        if (!horde) {
            system_chat("The waves alone stand a brute.", p.id);
            return;
        }
        // Born where the waves are born: a site drawn of the pool at
        // random, its own floor first and one of its square around,
        // then the next site of the pool the same way, until a floor
        // the stand says yes to. That floor is the one the wave stands
        // its walkers on - never a perch over it, never a room under.
        const horde::Config& hc = horde->config();
        const int pool_n = horde->spawn_pool();
        const size_t base = rng() % static_cast<size_t>(pool_n);
        int sx = 0, sy = 0, sz = -1;
        for (int j = 0; j < pool_n && sz < 0; ++j) {
            const size_t k =
                (base + static_cast<size_t>(j)) % static_cast<size_t>(pool_n);
            const int x0 = hc.spawn_x[k], y0 = hc.spawn_y[k];
            for (int dx = -2; dx <= 2 && sz < 0; ++dx)
                for (int dy = -2; dy <= 2; ++dy) {
                    const int f = horde->stand_floor(x0 + dx, y0 + dy);
                    if (f >= 0) {
                        sx = x0 + dx;
                        sy = y0 + dy;
                        sz = f;
                        break;
                    }
                }
        }
        if (sz < 0) {
            system_chat("No wave site stands a floor.", p.id);
            return;
        }
        const horde::SpawnAt at{sx, sy, sz, world_time, -1};
        add_zombie(at, kind, true);
        const std::string stood =
            lowered(std::string(specials[static_cast<size_t>(kind)].name));
        system_chat("The " + stood + " stands with the waves.", p.id);
    }

    // The core's own word: /core x y stands it at a new cell, however the
    // game runs - the old column goes, the fresh one is read off the ground
    // there the same way the first was, and everything that follows the
    // core follows at once: the zombies' field walks to it at its own next
    // reading (the same debounce an edit of the map has), the flag the
    // clients draw moves on the wire's fifth word, and the dig and blast
    // vetoes keep whatever the column is now. A third number names the
    // layer the core's foot block stands in - 61, say, on the bedrock of
    // a cave - and with it the column stands two blocks tall, the same
    // reading the map's own z has; without it, the walk to land speaks.
    void core_command(Player& p, const std::string& text) {
        if (!tower_defense_mode() || !horde) {
            system_chat("The core only stands in a tower defense world.", p.id);
            return;
        }
        std::vector<double> ns;
        const std::string cut = trimmed(lowered(text).substr(5));
        size_t from = 0;
        for (;;) {
            const size_t sp = cut.find(' ', from);
            const std::string one = sp == std::string::npos
                                        ? cut.substr(from)
                                        : cut.substr(from, sp - from);
            if (!one.empty()) ns.push_back(std::strtod(one.c_str(), nullptr));
            if (sp == std::string::npos) break;
            from = sp + 1;
        }
        const bool bad = ns.size() < 2 || std::isnan(ns[0]) || std::isnan(ns[1]);
        if (bad) {
            system_chat("usage: /core x y [z] (the cell; z, the layer its foot stands in)",
                        p.id);
            return;
        }
        int x = static_cast<int>(ns[0]), y = static_cast<int>(ns[1]);
        int foot_z = 0;
        if (ns.size() >= 3 && ns[2] >= 1.0 && ns[2] <= 62.0)
            foot_z = static_cast<int>(ns[2]);
        if (foot_z) {
            // A named layer is trusted where it stands; the walk to land
            // reads the surface, and the foot of a core in a room is not
            // sitting on it.
            x = std::min(std::max(x, 2), 509);
            y = std::min(std::max(y, 2), 509);
        } else {
            nearest_land(x, y);
        }
        if (x == horde_cfg.core_x && y == horde_cfg.core_y) {
            system_chat("The core already stands at " + std::to_string(x) + ", " +
                             std::to_string(y) + ".", p.id);
            return;
        }
        erase_core_column();
        horde_cfg.core_x = x;
        horde_cfg.core_y = y;
        build_core_column(foot_z);
        air_core_column();
        horde->move_core(x, y, core_top_z);
        core_resend_next = 0.0;
        core_state_send();
        system_chat("The core stands at " + std::to_string(x) + ", " +
                         std::to_string(y) + ".");
        log("the core moved to %d, %d by %s's word", x, y, p.name.c_str());
    }

    // /va: the map after this one, of those the game files carry. A world
    // cannot swap its own ground under the feet of a client mid-game, so it
    // only asks the menu thread - the flag read when the mode starts again -
    // and says which map to pick then; leaving and pressing Tower Defense
    // plays it.
    void map_advance_command(Player& p) {
        if (cfg.map_roster.empty()) {
            system_chat("Map advance: no other map stands beside the course "
                        "in the game files.", p.id);
            return;
        }
        std::string next = cfg.map_roster[0];
        for (size_t i = 0; i < cfg.map_roster.size(); i++) {
            if (cfg.map_name == cfg.map_roster[i]) {
                next = cfg.map_roster[(i + 1) % cfg.map_roster.size()];
                break;
            }
        }
        std::string name = next;
        const size_t slash = name.rfind('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        if (cfg.tower_defense) {
            std::lock_guard<std::mutex> lock(cfg.tower_defense->mutex);
            cfg.tower_defense->map_advance = true;
        }
        system_chat("Map advance: " + name + " stands next - leave and press "
                     "Tower Defense to play it.", p.id);
    }

    // One frame of teeth, or of the spade: the swing the clients animate,
    // closed again in the tick after.
    void show_swing(Player& p, double span) {
        p.swing_shown = true;
        p.swing_until = world_time + span;
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::WeaponInput));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(1);
        broadcast(bw, false);
    }

    // The next print the follower owes a step to: the oldest print of the
    // one ahead that its own feet have not stood on yet. A print is
    // stood on when the walker comes within kPrintTouch of it, or past
    // it at
    // the shoulder's width, and it is spent
    // when the walker has come nearer a later print - a print overtaken
    // or flung over is behind the walk and owed no step. The ring is
    // walked newest first, the nearest distance of everything later
    // carried down through the ages as it goes: one pass, and the last
    // print still standing is the oldest one owed. -1: nothing owed -
    // the line's heel is held the plain way, at the body.
    // Trodden beside: a print the walker has stepped past, inside the
    // shoulder's width of its own plane. The plane is drawn between the
    // print's neighbours in the walk, the one before it and the one after
    // it, as far as the ring still holds either; the sequence numbers
    // sort true neighbours from the ring's wrapped end.
    static bool trodden_beside(const Player& a, int slot, float x, float y) {
        const uint32_t s = a.heel_seq[slot];
        const int before = (slot + Player::kHeelN - 1) % Player::kHeelN;
        const int after = (slot + 1) % Player::kHeelN;
        const bool has_before = a.heel_seq[before] != 0 && a.heel_seq[before] < s;
        const bool has_after = a.heel_seq[after] != 0 && a.heel_seq[after] > s;
        if (!has_before && !has_after) return false;
        float ax = a.heel_x[slot], ay = a.heel_y[slot];
        float bx = ax, by = ay;
        if (has_before) { ax = a.heel_x[before]; ay = a.heel_y[before]; }
        if (has_after) { bx = a.heel_x[after]; by = a.heel_y[after]; }
        const float dx = bx - ax, dy = by - ay;
        const float n = std::sqrt(dx * dx + dy * dy);
        if (n < 1e-3f) return false;
        const float rx = x - a.heel_x[slot], ry = y - a.heel_y[slot];
        if (rx * dx + ry * dy <= 0.0f) return false;   // not past it yet
        const float lat = std::fabs(rx * dy - ry * dx) / n;
        return lat < kPrintShoulder;
    }

    static int ahead_print(const Player& a, float x, float y) {
        int chosen = -1;
        float nearer_later = -1.0f;
        for (int k = 0; k < Player::kHeelN; ++k) {
            const int slot =
                (a.heel_head + Player::kHeelN - 1 - k) % Player::kHeelN;
            const uint32_t s = a.heel_seq[slot];
            if (s == 0) continue;                   // an empty slot is no print
            const float ddx = a.heel_x[slot] - x;
            const float ddy = a.heel_y[slot] - y;
            const float d2 = ddx * ddx + ddy * ddy;
            if (d2 >= kPrintTouch * kPrintTouch && !trodden_beside(a, slot, x, y) &&
                (nearer_later < 0.0f || nearer_later >= d2))
                chosen = slot;          // nothing later lies nearer: still owed
            nearer_later = nearer_later < 0.0f ? d2 : std::min(nearer_later, d2);
        }
        return chosen;
    }

    // The spade's lead: the dig walk's advice, for a walker the legs'
    // walk has no honest word for - `leg_led` is that word, read at the
    // feet by the caller: a straight-line walking aid is not a word.
    // The crossing the dig walk hands back may be a step the legs make
    // after all, a drop walked straight off (the
    // dead fear no landing), or a climb beyond the jump: and a climb is
    // the spade's own work. The walker plants itself facing the column
    // it cannot take and empties the crossing's top off it, one voxel a
    // dig_seconds - the living digger's own pace, three blocks the
    // second - until what
    // is left is a jump the hop and the fling can clear, and the field,
    // freshly recomputed around the lowered wall, hands the whole pack
    // the way over. Only the pack's head digs: one spade a crossing, one
    // breach for the line to pour through; a follower steer-led by the
    // dig walk walks and drops as advised, but waits at the wall for the
    // breach its head opens. And what holds for a wall holds for a
    // ceiling: a drop the walk promises through rock that was never
    // opened is not walked but carved, its first solid voxel off, one a
    // stroke, until the fall stands in honest air and the body takes it.
    // False when the licence is off, the walker is aloft, or no
    // clock of the pinned has run out yet; the walker is left to the
    // legs' own answers. Stuck past a windup, the head earns the
    // licence anyway: a wall faced long enough is worth the spade.
    bool dig_lead(Player& p, bool leg_led) {
        if (!tower_defense_mode() || !horde_cfg.dig ||
            p.body.airborne) return false;
        const int cx = static_cast<int>(std::floor(p.body.pos.x));
        const int cy = static_cast<int>(std::floor(p.body.pos.y));
        const int fz = static_cast<int>(std::floor(p.body.pos.z + 2.25f));
        // The stuck head's licence: the road is the road, and the
        // spade stays holstered while the legs make ground. A head
        // stuck past the windup, and not resting on the spade's own
        // cadence at a wall it already opens, is granted the licence:
        // the wall in its face comes down - PROVIDED the walk has no
        // word for the ground it stands on. Pressed to a town wall
        // with a gate still open along it, the stuck clock owes
        // nothing: the pack crowding a head off its own advice is
        // not a wall, and the answer the field still holds is the
        // one to take - while the ground is truly sealed, here
        // alone, the licence is the only way through there is.
        const bool stuck_licence = p.stuck_at >= 0.0 &&
                                   !leg_led &&
                                   world_time - p.stuck_at >= kStuckDigWindup &&
                                   world_time - p.spade_at >= horde_cfg.dig_seconds;
        // The miller's licence: stillness is only one way to be pinned.
        // A pack far from the stairs shuffles every window and so
        // never reads stuck, yet gains not a step of ground - the dig
        // watch says so where the stuck clock cannot, and the licence
        // this second reading owes opens the same wall either way.
        const bool watch_licence = p.dig_watch_at >= 0.0 &&
                                   world_time - p.dig_watch_at >= kDigWatchSeconds &&
                                   world_time - p.spade_at >= horde_cfg.dig_seconds;
        // The detour licence: the road is the road, and only the
        // mute ground, the milled pack and the held work were owed
        // the spade before now. But a town sealed to one gate prices
        // the road to its open gate in hundreds while the spade
        // prices the crossing at the walled one in tens, and the
        // head packed at the walled gate reads that road for as long
        // as the walk is taken at its word - the shuffle hides it
        // from the stuck clock, the shuffling of the pack winds the
        // watch anew, and the siege mills at the wall it should open.
        // So the stalemate is weighed instead of honoured: a head
        // the stuck clock has caught, reading at its feet a walk
        // held kDigDetour above the crossing the spade prices from
        // the same floor for kDigDetourSeconds, owes the spade after
        // all - and the crossing the dig walk names is the one that
        // comes down, detour of a hundred or honest maze turn aside.
        const bool detour_licence = leg_led &&
                                    p.stuck_at >= 0.0 &&
                                    p.detour_at >= 0.0 &&
                                    world_time - p.detour_at >=
                                        kDigDetourSeconds &&
                                    world_time - p.spade_at >=
                                        horde_cfg.dig_seconds;
        const bool licensed = stuck_licence || watch_licence ||
                              detour_licence;
        // The grip of work: a stroke within kDigStaySeconds keeps the
        // gate and the advice both, so a face once begun is a face
        // that gets finished, whatever the field's half-second churn
        // reads into the value of it mid-hammer.
        const bool working = world_time - p.spade_at < kDigStaySeconds;
        // THE ROAD IS THE ROAD: the spade is a rescue and only that.
        // A shortcut priced against the walk is a coin flipped at
        // every read of a field that recomputes every half second,
        // and a leader standing where the road and the wall price
        // alike spent its whole march swinging between the two - a
        // stroke, a step, a stroke again, and neither way ever
        // walked; and a maze the living raise is a wall to honour,
        // not the hint of a tunnel. So the leader walks its road the
        // whole way, and the spade comes out only for the pinned and
        // the milling - the stuck clock run out, the watch run down,
        // a zone walked without a step of ground gained - and a
        // stroke already landing keeps its face while the strokes
        // keep feeding it.
        if (leg_led && !licensed && !working)
            return false;
        int tx = cx, ty = cy, tz = fz;
        if (!horde->next_dig_step(cx, cy, fz, tx, ty, tz)) {
            // The walk only ever hands downhill over - and the wall a
            // stuck head faces prices nothing downhill, whatever its
            // height: without this second reading the licence the
            // comment above promises would be a no-op, the advice
            // silent at every wall in the land. So the licensed head -
            // or the working head, whose grip on its face outlasts a
            // reading that merely churned - reads the one column it
            // faces straight off the map:
            // solid at its own foot level, up to that run's own top,
            // chipped from the top, a stroke a voxel, until the climb
            // is one the legs make after all - and the field, freshly
            // recomputed, pours the pack through the breach.
            if (!licensed && !working) return false;
            const float ox = std::fabs(p.body.ori.x),
                        oy = std::fabs(p.body.ori.y);
            int sx = 0, sy = 0;
            if (ox > oy)
                sx = p.body.ori.x > 0.f ? 1 : -1;
            else if (oy > 1e-3f)
                sy = p.body.ori.y > 0.f ? 1 : -1;
            else
                return false;   // facing nowhere in particular: no call
            tx = cx + sx;
            ty = cy + sy;
            // A face may hang as well as stand: a lip of the hill
            // riding over a passage corners the walker as surely as
            // a chest high wall does, and the one trapped under it
            // reads solid overhead at the walking head with open air
            // below - every field and every probe keyed on the
            // chest cell saw no wall there at all, and the mouth
            // starved with the spade holstered. The lip is a face to
            // stand against the same: its run starts at the hanging
            // cell and climbs to its own crest.
            const bool hanging =
                !map.solid(tx, ty, fz - 1) &&
                 map.solid(tx, ty, fz - 2);
            if (!hanging && !map.solid(tx, ty, fz - 1))
                return false;   // nothing to stand against at the feet
            int z = hanging ? fz - 2 : fz - 1;
            while (z > 0 && map.solid(tx, ty, z - 1)) --z;   // the run's top
            tz = z;
        }
        const float dx = static_cast<float>(tx) + 0.5f - p.body.pos.x;
        const float dy = static_cast<float>(ty) + 0.5f - p.body.pos.y;
        const float dl = std::sqrt(dx * dx + dy * dy);
        if (dl > 1e-3f) p.body.ori = Vec3{dx / dl, dy / dl, 0.0f};
        // The crossing against the jump: a climb the legs cannot make is
        // the column to be taken down, from its own top - the crossing's
        // surface voxel, the one the walk prices its block at. The core's
        // column is never touched, whatever its height - and the drop the
        // walk promises through a ceiling that was never opened is the
        // spade's work the same way: the first solid voxel in the fall's
        // way comes off, top down, one a stroke, until the drop is
        // honest air and the body walks it. The shaft opens beside the
        // core, above the rock of its own column, and the core's own
        // blocks are the one thing never touched.
        const int climb = tz < fz ? fz - tz : 0;
        int spade_z = -1;
        bool flight = false;
        if (climb > horde::Config::kWalkClimb) {
            // THE FRONTAL OATH: the advice hands back the crossing it
            // priced cheapest, and the cheapest crossing of a long mass
            // is a saddle over at its end - a walk along the base, a
            // stair dug SIDEWAYS into the body, a gallery that crawls
            // the wall all evening and never once tops out, for the
            // crossing it serves lies elsewhere than where the spade
            // bites. So the wall decides, not the counsel: of the four
            // neighbours, the face is the one solid at the standing head
            // whose run stands the tallest - and the stair is cut
            // entrance-first INTO that mass, a flight a stroke, over
            // the crest straight above it. No wall at the standing head
            // - a cliff across a gap, a crossing the legs would have to
            // be carried to - keeps the counsel's own column, as it
            // always was.
            int wx = cx, wy = cy, wz = -1;
            int best = -1;
            const int kFx[4] = {1, -1, 0, 0};
            const int kFy[4] = {0, 0, 1, -1};
            for (int n = 0; n < 4; ++n) {
                const int nx = cx + kFx[n], ny = cy + kFy[n];
                // The hanging face ranks here as well: open at the
                // chest and solid at the walking head is a wall the
                // body cannot walk under, and a passage mouthed
                // under such a lip starved whole packs - the old
                // probe called it no wall at all, no face was found,
                // and the spade stayed down while no surface the
                // walk could price existed under the lip. The run
                // of a hanging face climbs from its lowest hung
                // cell, and its crest ranks against the rest.
                const bool hanging =
                    !map.solid(nx, ny, fz - 1) &&
                     map.solid(nx, ny, fz - 2);
                if (!hanging && !map.solid(nx, ny, fz - 1)) continue;
                int z0 = hanging ? fz - 2 : fz - 1;
                while (z0 > 0 && map.solid(nx, ny, z0 - 1)) --z0;
                const int c0 = fz - z0;
                if (c0 > best) {
                    best = c0;
                    wx = nx;
                    wy = ny;
                    wz = z0;
                }
            }
            if (best > 0) {
                tx = wx;
                ty = wy;
                tz = wz;
                spade_z = wz;
            } else {
                spade_z = tz;
            }
            flight = true;
        } else if (tz > fz) {
            for (int z = fz; z < tz; ++z) {
                if (map.solid(tx, ty, z)) { spade_z = z; break; }
            }
        }
        // The lane the stroke cuts runs three wide. A slot one cube
        // wide reads open to the field yet still pinches a body whose
        // box asks more room than a single column ever gives, and the
        // head chips at a breach its own legs cannot enter. So the
        // stroke takes the focus voxel and its two lateral mates at
        // once, the left and the right of it on the axis the walk
        // does not step along, and the lane is wide the whole way
        // down: the mates ride along only onto rock, the core holds
        // its blocks against any of the three, and each fallen voxel
        // carries its own BlockAction to the wire.
        const int fx = tx - cx, fy = ty - cy;
        const bool along_x = fx * fx >= fy * fy;
        const int lx = along_x ? 0 : 1;
        const int ly = along_x ? 1 : 0;
        if (spade_z >= 0) {
            p.input.mf = false;   // it works standing, not pressed at its wall
            if (p.ahead_id >= 0) return true;   // the line waits on its head
            if (world_time < p.spade_at + horde_cfg.dig_seconds) return true;
            if (!map.solid(tx, ty, spade_z)) return true;   // the field has not caught up yet
            if (core_column_cell(tx, ty, spade_z)) return true;
            std::vector<Cell> removed;
            if (flight) {
                // The staircase cut whole flights at a stroke: the
                // chip that came one voxel at a time walked the dead
                // into a breach their own bodies could not follow -
                // a door a cube wide is a wall to the box. So one
                // stroke takes the full flight: the strips run into
                // the face one a block, and each strip stands its
                // room a step higher than the strip before it, the
                // tread of each strip the wall mass below - never
                // touched. THE ROOM STANDS kStandHead AND HOPS TWO
                // MORE over it: a pocket just a body tall reads a
                // floor to the field and a wall to the jump, the
                // step the legs make of it being a hop that spends
                // the crown of its arc against whatever ceiling the
                // slot left standing - so a flight of body-tall
                // rooms cuts a stair the dead walk under and never
                // once take, and the gallery it mouths fills with
                // the stuck, the very cave the pack was found
                // milling in. Two clear over the stand is the arc
                // the climb asks, and the stair the stroke leaves
                // is a stair the dead actually climb.
                // The lane holds its three widths for the walk and
                // its shoulder besides: a wall standing at the side
                // of the trench presses the pack into the width of
                // its own line, and a head stalled in a pocket
                // queues the wave behind it with no room to be
                // overtaken in - the lane pressed to a cliff is a
                // second cave. So the stroke clears kDigLaneMargin
                // columns past each edge of the lane at the height
                // of the rooms it cuts, and a wall nearer than
                // three blocks to the walking line comes down with
                // the stroke: the core holds against every cell
                // still, the air rides along unharmed, and the
                // ground every tread stands on is left standing.
                for (int i = 0; i < kDigFlightDepth; ++i) {
                    const int sx = tx + fx * i;
                    const int sy = ty + fy * i;
                    for (int j = -1 - kDigLaneMargin; j <= 1 + kDigLaneMargin; ++j) {
                        const int bx = sx + lx * j;
                        const int by = sy + ly * j;
                        for (int k = 0; k < horde::Config::kStandHead + 2; ++k) {
                            const int bz = fz - i - k;
                            if (bz < 0) continue;
                            if (!map.solid(bx, by, bz)) continue;
                            if (core_column_cell(bx, by, bz)) continue;
                            map.destroy_point(bx, by, bz, &removed);
                            ByteWriter bw;
                            bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
                            bw.write_u8(static_cast<uint8_t>(p.id));
                            bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
                            bw.write_i32(bx);
                            bw.write_i32(by);
                            bw.write_i32(bz);
                            broadcast(bw, true);
                        }
                    }
                }
            } else {
                for (int i = -1; i <= 1; ++i) {
                    const int bx = tx + lx * i;
                    const int by = ty + ly * i;
                    if (i != 0 && !map.solid(bx, by, spade_z))
                        continue;   // a mate rides along only onto rock
                    if (core_column_cell(bx, by, spade_z))
                        continue;   // the core holds, mate or focus
                    map.destroy_point(bx, by, spade_z, &removed);
                    ByteWriter bw;
                    bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
                    bw.write_u8(static_cast<uint8_t>(p.id));
                    bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
                    bw.write_i32(bx);
                    bw.write_i32(by);
                    bw.write_i32(spade_z);
                    broadcast(bw, true);
                }
            }
            touched(removed, false);
            p.spade_at = world_time;
            show_swing(p, horde_cfg.dig_seconds);
            return true;
        }
        // A step, or a drop of honest air: walked, not spaded.
        p.input.mf = true;
        return true;
    }

    // The pack the terrain pinned, carried down its own road: every last
    // resort spent - the windows counted, the hopped hops, the fling
    // flung, the licence drawn, the line disbanded - and the head still
    // stands on its stuck. The road it reads is the spade's own trace of
    // the way home, and the first tread of it kTeleportAhead treads down
    // that opens a floor the legs count, under the clearing a wave is
    // born in, stands the head and its whole line up, their scores wiped
    // and their order kept - the mercy walks them past the stretch the
    // terrain refused, never off the road, never over a gate. What the
    // walk still owes from there, the walk pays: no distance skipped
    // that the route did not cross itself. Players pass through players,
    // and the pile the carrying stands in one place walks apart in a
    // step or two of its own - a wave born scattered reads the same.
    void teleport_pinned(Player& p) {
        if (!p.zombie || !p.alive || p.ahead_id >= 0) return;
        if (p.body.airborne) return;
        if (world_time - p.teleport_at < kTeleportAgain) return;
        const int cx = static_cast<int>(std::floor(p.body.pos.x));
        const int cy = static_cast<int>(std::floor(p.body.pos.y));
        // The road the pack reads: the spade's trace, the same way home
        // the advice names with - the legs' trace dies where the legs
        // do, and a head pinned is exactly there. No trace, no mercy.
        std::vector<horde::RouteCell> route;
        if (!horde->trace_dig(cx, cy, route)) {
            // The probe: a head owed the road learns that the spade walk
            // holds no road home from where it stands - the one reading
            // a stuck map wants first.
            if (map_probe && world_time - p.probe_at >= kProbeAgain) {
                const int fz = static_cast<int>(
                    std::floor(p.body.pos.z + 2.25f));
                p.probe_at = world_time;
                log("probe %d: the pinned head at %d,%d,%d reads no road"
                    " home the spade walk holds",
                    p.id, cx, cy, fz);
            }
            // The ring takes what the road cannot serve: a head
            // pinned where the spade walk holds no trace home - a
            // bridge, a plateau, a roof the maze raised it onto -
            // keeps its line shelved behind it to the end of time,
            // for no other mercy was written for it. So the ground
            // itself is read instead: ring by ring around the stuck
            // spot, the first floor either walk counts, and the
            // whole line rides there - back on a road, a stride
            // from where it stood, whatever terrain had swallowed
            // it.
            int bx = -1, by = 0, bz = 0;
            for (int r = 2; bx < 0 && r <= 32; ++r) {
                for (int dx = -r; dx <= r && bx < 0; ++dx) {
                    const int ex = r - std::abs(dx);
                    for (int dy = -ex; dy <= ex; ++dy) {
                        if (std::abs(dy) != ex && std::abs(dx) != r)
                            continue;   // the ring alone: the inner
                                        // rings were read before
                        const int qx = cx + dx, qy = cy + dy;
                        if (qx < 0 || qy < 0 || qx > 511 || qy > 511)
                            continue;
                        const int zl = horde->stand_floor(qx, qy);
                        if (zl < 0) continue;
                        if (horde->distance(qx, qy, zl) < 0 &&
                            horde->distance(qx, qy, zl - 1) < 0 &&
                            horde->dig_distance(qx, qy, zl) < 0 &&
                            horde->dig_distance(qx, qy, zl - 1) < 0)
                            continue;   // neither walk counts it
                        bx = qx; by = qy; bz = zl;
                        break;
                    }
                }
            }
            if (bx < 0) return;   // no counted ground around: stands
            std::vector<Player*> line{&p};
            for (size_t k = 0; k < line.size(); ++k) {
                const int ahead = line[k]->id;
                for (auto& q : players) {
                    if (!q || !q->zombie || !q->alive) continue;
                    if (q->ahead_id != ahead) continue;
                    line.push_back(q.get());
                }
            }
            const Vec3 dest{static_cast<float>(bx) + 0.5f,
                            static_cast<float>(by) + 0.5f,
                            static_cast<float>(bz) - 2.4f};
            for (Player* q : line) {
                q->body = physics::Body{};
                q->body.pos = dest;
                q->body.eye = dest;
                q->input = physics::Input{};
                q->foot_dist = -1;
                q->stall_windows = 0;
                q->stuck_at = -1.0;
                q->dig_watch_at = -1.0;
                q->detour_at = -1.0;
                q->walk_ref = dest;
                q->walk_ref_at = world_time;
                q->teleport_at = world_time;
                q->spade_at = -1e9;
            }
            log("the ring carried a pinned pack back to a walk at"
                " %d, %d", bx, by);
            return;
        }
        for (size_t i = kTeleportAhead; i < route.size(); ++i) {
            const horde::RouteCell& at = route[i];
            if (std::abs(at.x - cx) + std::abs(at.y - cy) < 4) continue;
            const int zl = horde->stand_floor(at.x, at.y);
            if (zl < 0) continue;   // no legs-counted floor the born clear above
            // The whole line of it, link by link: whoever follows the
            // head, and whoever follows them, comes along.
            std::vector<Player*> line{&p};
            for (size_t k = 0; k < line.size(); ++k) {
                const int ahead = line[k]->id;
                for (auto& q : players) {
                    if (!q || !q->zombie || !q->alive) continue;
                    if (q->ahead_id != ahead) continue;
                    line.push_back(q.get());
                }
            }
            const Vec3 dest{static_cast<float>(at.x) + 0.5f,
                            static_cast<float>(at.y) + 0.5f,
                            static_cast<float>(zl) - 2.4f};
            for (Player* q : line) {
                q->body = physics::Body{};
                q->body.pos = dest;
                q->body.eye = dest;
                q->input = physics::Input{};
                q->foot_dist = -1;
                q->stall_windows = 0;
                q->stuck_at = -1.0;
                q->dig_watch_at = -1.0;
                q->detour_at = -1.0;
                q->walk_ref = dest;
                q->walk_ref_at = world_time;
                q->teleport_at = world_time;
                q->spade_at = -1e9;   // the carried hold no face behind them
            }
            log("the road carried a pinned pack %d treads, to %d, %d",
                static_cast<int>(i), at.x, at.y);
            return;
        }
    }

    // The crossing the field prices one jump is taken as one jump. A
    // plain hop with a walk behind it mounts the lip of two the way
    // any player mounts it: risen alongside the face, the physics own
    // climb lifts the last block and spends half the run braking on
    // the tread - the sweep of the tall pounce, thrown from a
    // standstill, spends the stair whole. So the walker under a
    // crossing the advice prices holds the key at its approach and
    // lets the physics spend it, tick after tick, the flight its own
    // cadence: grounded again under the same crossing, it presses.
    // The ponce stays the stuck clock alone - a wall that owes no
    // jump lands the walker and keeps the fling it is owed.
    void vault_crossing(Player& p, int fz, int tz, float gap) {
        if (fz - tz != horde::Config::kWalkClimb) return;
        if (gap > kVaultReach) return;
        if (p.body.airborne) return;
        p.input.jump = true;
    }

    // A walker that walks out of the pack hands its line over, so the
    // single file of the waves keeps both its shape and its road:
    // whoever followed it follows whoever it followed, and the corner
    // tail passes the same hand down. The brute was a door in the wall
    // of the walk only while it walked the walk.
    void hand_pack_over(Player& p) {
        const int hand = p.ahead_id;
        for (auto& qq : players) {
            if (!qq || !qq->zombie || !qq->alive) continue;
            if (qq->ahead_id != p.id) continue;
            qq->ahead_id = hand;
        }
        if (p.spawn_corner >= 0 &&
            pack_tail[p.spawn_corner] == p.id)
            pack_tail[p.spawn_corner] = hand;
        p.ahead_id = -1;
    }

    // The facing column of a walker, cast on the map: the column ahead on
    // the axis its aim runs down, told at the foot, the head and the sky
    // over it. The body is two blocks tall where it stands, so the foot
    // and the head are the two cells the way ahead would have it walk
    // into, and the sky is the third.
    void face_ahead(Player& p, int cx, int cy, int fz,
                    int& fx, int& fy, bool& foot, bool& head, bool& sky) {
        int sx = 0, sy = 0;
        if (std::fabs(p.body.ori.x) >= std::fabs(p.body.ori.y))
            sx = p.body.ori.x > 0.0f ? 1 : -1;
        else
            sy = p.body.ori.y > 0.0f ? 1 : -1;
        fx = cx + sx;
        fy = cy + sy;
        foot = map.solid(fx, fy, fz - 1);
        head = map.solid(fx, fy, fz);
        sky = map.solid(fx, fy, fz + 1);
    }

    // The hop the way ahead asks of any walker: a stair of one block or
    // two under an open sky is taken the moment the face reads it, never
    // held back for a stuck clock - a wall or a lip is not a stair, and
    // the mind that walks the body answers those in its own words.
    void hop_step(Player& p, int cx, int cy, int fz) {
        int fx = 0, fy = 0;
        bool foot = false, head = false, sky = false;
        face_ahead(p, cx, cy, fz, fx, fy, foot, head, sky);
        if (!foot && !head) return;
        if (!sky) p.input.jump = true;
    }

    // The hunt of the Spitter and the Leaper: the person the horde
    // names is the road, and the walk to them is the same walk the
    // horde walks to the core - a field of this hunt, flooded
    // from the person it names and refreshed while they hunt - so the
    // way past a wall is the way of the field, around and over and never
    // through, as it never was for the pack. The word of the field
    // steers first; the straight press at the person is the word a
    // walker falls back to where the field holds its silence. The
    // Leaper presses the whole way, to the teeth; a Boomer handed the
    // chase presses the same, for its burst is a tooth of contact; the
    // Spitter holds its own ground inside the reach of the spit and
    // lets the acid close.
    // A stair either way is hopped where the face reads it, and the
    // common answers hold - the hop, the fling - all but the road:
    // the mercy rides the walk to the core, and the hunt never rides.
    void hunt_tick(Player& p, int cx, int cy, int fz) {
        const Player* t = hunt_focus_;
        if (t == nullptr || !t->alive) return;   // nobody: the hunt stands
        const float dx = t->body.pos.x - p.body.pos.x;
        const float dy = t->body.pos.y - p.body.pos.y;
        const float d = std::sqrt(dx * dx + dy * dy);
        int hx = cx, hy = cy;
        const bool stepped = horde->hunt_step(cx, cy, hx, hy);
        if (stepped) {
            const float ax = static_cast<float>(hx) + 0.5f -
                             p.body.pos.x;
            const float ay = static_cast<float>(hy) + 0.5f -
                             p.body.pos.y;
            const float al = std::sqrt(ax * ax + ay * ay);
            if (al > 1e-3f) p.body.ori = Vec3{ax / al, ay / al, 0.0f};
        } else if (d > 1e-3f) {
            p.body.ori = Vec3{dx / d, dy / d, 0.0f};
        }
        // The stand, kept inside the band the tuning draws: no nearer
        // than the band's own edge lets stand, and a breath the walls
        // shut out hands the walker back to the road, for a stand
        // without a riding line holds nothing.
        const float band_stand = std::max(1.0f, std::min(
            kSpitHold, static_cast<float>(spitter.range)));
        const bool breath = p.brute != kSpitter ||
                            p.spit_off <= kSpitOffShut;
        if (p.brute == kSpitter && breath && d > 1e-3f &&
            d < band_stand) {
            // The person walked the band in, the line holds, and the
            // stand keeps the distance it was made to keep: a plain
            // walk back out to the edge the band draws. The arc and
            // the pool are the only teeth this brute carries, and
            // the shuffle back is the run the hunted gets to make.
            p.body.ori = Vec3{-dx / d, -dy / d, 0.0f};
            p.input.mf = true;
            p.input.sprint = false;
        } else {
            p.input.mf = p.brute == kLeaper || p.brute == kBoomer ||
                          d > band_stand || !breath;
            p.input.sprint = d > kHuntSprint;
        }
        if (!p.body.airborne) hop_step(p, cx, cy, fz);
        // The hunt probe, at its own beat: what the field holds for
        // the column underfoot and the step it lends. A hunter lost
        // on a slope reads a field of no word about it, and the line
        // says so aloud - the reading a stuck chase
        // begins with. -1 is that silence; the step is the cell the
        // walk names, or none, and the body presses straight.
        if (map_probe && world_time - p.hunt_probe_at >= kProbeAgain) {
            p.hunt_probe_at = world_time;
            const int field = horde->hunt_reach(cx, cy);
            if (stepped)
                log("hunt %d: at %d,%d,%d the person is %.1f away, the "
                    "field reads %d, the step is %d,%d",
                    p.id, cx, cy, fz, d, field, hx, hy);
            else
                log("hunt %d: at %d,%d,%d the person is %.1f away, the "
                    "field reads %d, no step",
                    p.id, cx, cy, fz, d, field);
        }
    }

    // The hunter's leap, a leaper's own: the arc the chase takes when
    // the ground around the person lends it. Two flights and a stand
    // between them - the flight out to a perch that stands above the
    // person, the coil upon it that says the pounce is owed, and the
    // pounce itself, an arc at where the person is going. Every leg is
    // loud: the perch is plain to see, the coil plainer in its squat,
    // and both buy the hunted their beat to move.
    // The coil between the two flights: a squat held half a second
    // to one, the tuning holds its ceiling and the draw rides
    // under it - the person earns their beat to read the squat and
    // move before the pounce takes them, and the pounce owes them
    // the shorter read a beat that brief buys.
    double coil_hold() {
        const double draw =
            std::uniform_real_distribution<double>(0.0, 1.0)(rng);
        const double top = std::max(0.5, std::min(1.0, leap_hold));
        return 0.5 + (top - 0.5) * draw;
    }

    void leap_start(Player& p, const Vec3& to, const double due,
                    const int stage) {
        p.leap_stage = stage;
        p.leap_until = world_time + due;
        p.leap_due = due;
        p.leap_to = to;
        const float dx = to.x - p.body.pos.x;
        const float dy = to.y - p.body.pos.y;
        const float l = std::sqrt(dx * dx + dy * dy);
        if (l > 1e-3f) p.body.ori = Vec3{dx / l, dy / l, 0.0f};
        if (!p.body.airborne) p.input.jump = true;
    }

    void leap_fly(Player& p) {
        const double left = p.leap_until - world_time;
        const double gone = p.leap_due - left;
        if (left <= 0.0 || (!p.body.airborne && gone > 0.1)) {
            // The touchdown, or the ground that never let it go: the
            // perch coil stands next, or the chase walks on again.
            p.body.vel = Vec3{0.0f, 0.0f, 0.0f};
            if (p.leap_stage == 1) {
                p.leap_stage = 2;
                p.leap_until = world_time + coil_hold();
            } else {
                p.leap_stage = 0;
                p.leap_ready = world_time + leap_every;
            }
            return;
        }
        // The flight steers itself whole: every tick its own line and
        // its own speed, the share of what it still owes - and a lift
        // at the takeoff that thins on the way, so the road reads as
        // an arc in the air, never a throw of a dice.
        // The pounce is a homing one: the person the coil sprang
        // at keeps their feet while the flight is in the air, and a
        // flight aimed only at where they stood lands where they
        // stood - a miss the whole perch was never owed. So the
        // pounce reads its person every tick of its flight and
        // flies at where they stand, the flight steering itself
        // whole already; the perch flight keeps its fixed landing,
        // for a perch is a place, and the person is not.
        if (p.leap_stage == 3) {
            const Player* t = hunt_focus_;
            if (t != nullptr && t->alive) {
                p.leap_to.x = t->body.pos.x;
                p.leap_to.y = t->body.pos.y;
                p.leap_to.z = t->body.pos.z;
            }
        }
        double hold = left;
        if (hold < 0.02) hold = 0.02;
        const float inv = 32.0f * static_cast<float>(hold);
        p.input = physics::Input{};
        p.body.vel.x = (p.leap_to.x - p.body.pos.x) / inv;
        p.body.vel.y = (p.leap_to.y - p.body.pos.y) / inv;
        p.body.vel.z = (p.leap_to.z - p.body.pos.z) / inv -
                       kLeapLift * static_cast<float>(hold);
    }

    void leap_tick(Player& p, int cx, int cy, int fz) {
        // The flights steer themselves; this is the rest of it.
        if (p.leap_stage == 1 || p.leap_stage == 3) return;
        const Player* t = hunt_focus_;
        if (t == nullptr || !t->alive) {
            if (p.leap_stage == 2) p.leap_stage = 0;
            return;   // nobody to spring at: the hunt stands
        }
        const float sdx = t->body.pos.x - p.body.pos.x;
        const float sdy = t->body.pos.y - p.body.pos.y;
        const float sd = std::sqrt(sdx * sdx + sdy * sdy);
        if (p.leap_stage == 2) {
            // The coil: it squats its ground, the person given their
            // beat to read it, and the spring takes the line to where
            // the person is going, not where they stand.
            p.input = physics::Input{};
            p.input.crouch = true;
            if (sd > 1e-3f)
                p.body.ori = Vec3{sdx / sd, sdy / sd, 0.0f};
            if (world_time < p.leap_until) return;
            const double due =
                std::max(0.4, std::min(1.5, static_cast<double>(sd) / 20.0));
            const float lead = static_cast<float>(due) * 0.7f;
            const int px = static_cast<int>(std::floor(
                t->body.pos.x + t->body.vel.x * 32.0f * lead));
            const int py = static_cast<int>(std::floor(
                t->body.pos.y + t->body.vel.y * 32.0f * lead));
            // The pounce lands where the person stands, at the
            // height they stand at: the ground top the old read flew
            // at is the surface under their feet, and a pounce
            // flown at the floor lands a body short of the person
            // every time. The coil aims the flight at the person,
            // and the flight itself homes in on them below.
            Vec3 to{px + 0.5f, py + 0.5f, t->body.pos.z};
            if (px < 1 || py < 1 || px > 510 || py > 510) {
                to.x = t->body.pos.x;
                to.y = t->body.pos.y;
            }
            leap_start(p, to, due, 3);
            const Vec3 at{p.body.pos.x, p.body.pos.y, p.body.pos.z + 0.6f};
            vapour(0, proto::SmokeStyle::Ground, at, 2, 0x80, 0x10,
                   0x10, 0x70, 3, 2);
            return;
        }
        // The chase asks for its perch: no arc while the cooldown
        // stands, no arc in the air, and the person closer than half
        // a perch earns the coil at once, sprung from the ground.
        if (world_time < p.leap_ready || p.body.airborne) return;
        const float perch = static_cast<float>(leap_perch);
        if (sd < perch * 0.5f) {
            p.leap_stage = 2;
            p.leap_until = world_time + coil_hold();
            return;
        }
        // The country of the pattern: the chase walks the map until
        // the person stands about this many blocks off, and only
        // then do the two flights begin - the perch first, the
        // pounce from it.
        if (sd > static_cast<float>(leap_country))
            return;   // out of its country: it walks the map
        // The perch ring: a ledge kPerchRise above the person's own
        // ground, set perch out around them, its own side preferred.
        const int px = static_cast<int>(std::floor(t->body.pos.x));
        const int py = static_cast<int>(std::floor(t->body.pos.y));
        const int stand = map.get_z(px, py);
        int best_x = -1, best_y = 0, best_z = stand;
        double best = 0.0;
        static constexpr double kPi = 3.141592653589793;
        for (int a = 0; a < 24; ++a) {
            const double ang = a * (2.0 * kPi / 24.0);
            const int qx = px + static_cast<int>(std::lround(
                std::cos(ang) * leap_perch));
            const int qy = py + static_cast<int>(std::lround(
                std::sin(ang) * leap_perch));
            if (qx < 1 || qy < 1 || qx > 510 || qy > 510) continue;
            const int qz = map.get_z(qx, qy);
            if (qz > stand - kPerchRise) continue;   // no ledge there
            const double side =
                (qx - px) * (p.body.pos.x - t->body.pos.x) +
                (qy - py) * (p.body.pos.y - t->body.pos.y);
            const double score =
                (stand - qz) + (side > 0.0 ? 2.0 : 0.0);
            if (score > best) {
                best = score;
                best_x = qx;
                best_y = qy;
                best_z = qz;
            }
        }
        if (best_x < 0) return;   // nothing stands: it keeps walking
        const Vec3 to{best_x + 0.5f, best_y + 0.5f,
                      static_cast<float>(best_z)};
        const double due = std::max(0.8, std::min(2.2,
            static_cast<double>(std::sqrt(
                (to.x - p.body.pos.x) * (to.x - p.body.pos.x) +
                (to.y - p.body.pos.y) * (to.y - p.body.pos.y))) / 20.0));
        leap_start(p, to, due, 1);
        const Vec3 at{p.body.pos.x, p.body.pos.y, p.body.pos.z + 0.6f};
        vapour(0, proto::SmokeStyle::Ground, at, 2, 0x80, 0x10,
               0x10, 0x70, 3, 2);
    }

    // The Boomer: a bladder on legs that walks the wave to the person,
    // the wall, or the core, and lights at whichever it meets first.
    // The flare it walks under is a dark orange, and once the wick
    // is lit the skin and the highlight blink a shade against its
    // dim mate - plain red on red for the last stretch - while the
    // mark it carries pulses the head the skin packs, the skull
    // growing and returning quicker as the wick shortens; the end
    // is a burst that
    // carves the ground it stood on and bites whatever stands in it,
    // its own side as much as the people's. A wall built to stop it
    // becomes the very thing it answers for: the wise shoot it while
    // it is still out among its own.
    void boomer_tick(Player& p) {
        if (p.brute != kBoomer || !tower_defense_mode()) return;
        const int cx = static_cast<int>(std::floor(p.body.pos.x));
        const int cy = static_cast<int>(std::floor(p.body.pos.y));
        const int fz = static_cast<int>(std::floor(
            p.body.pos.z + 2.25f));
        if (p.bomb_at < 0.0) {
            // The unlit wick: a person in its reach, a wall it has
            // pressed against, or the core underfoot light it.
            bool met = false;
            for (auto& q : players) {
                if (!q || !q->alive || q->zombie) continue;
                const float dx = q->body.pos.x - p.body.pos.x;
                const float dy = q->body.pos.y - p.body.pos.y;
                if (dx * dx + dy * dy <=
                    static_cast<float>(boomer_arm) *
                    static_cast<float>(boomer_arm)) {
                    met = true;
                    break;
                }
            }
            const bool pinned = p.stuck_at >= 0.0 &&
                                world_time - p.stuck_at >= kBoomStuck;
            if (met || pinned || zombie_at_core(cx, cy, fz)) {
                p.bomb_at = world_time + boomer_fuse;
                p.bomb_beat_at = world_time;
                p.bomb_red = true;
            }
            return;
        }
        if (world_time >= p.bomb_at) {
            // The burst: it dies on its own word, and the fall says
            // the rest.
            kill(p, nullptr, kGrenadeKill);
            return;
        }
        // The blink, on a beat that quickens as the wick shortens:
        // the shade flips between a colour and its dim mate - plain
        // red once the last stretch begins - and the flag the mark
        // carries says the head itself, that the skin the player
        // packs may pulse its skull at the beat and flash the black
        // voxels of its head in the shade. A little smoke at its
        // feet says the count aloud.
        if (world_time >= p.bomb_beat_at) {
            const double rest = p.bomb_at - world_time;
            const double beat = std::max(
                0.12, rest * 0.5 / std::max(0.1, boomer_fuse));
            p.bomb_beat_at = world_time + beat;
            const bool red = rest <= boomer_red_lead;
            p.bomb_red = !p.bomb_red;
            const uint32_t shade = red
                ? (p.bomb_red ? kBoomRed : kBoomRedDim)
                : (p.bomb_red ? kBoomOrange : kBoomOrangeDim);
            send_mark(p, shade,
                      static_cast<uint8_t>(1) | kMarkHeadPulse);
            const Vec3 at{p.body.pos.x, p.body.pos.y,
                          p.body.pos.z + 0.5f};
            const uint8_t shade_r =
                static_cast<uint8_t>((shade >> 16) & 0xFFu);
            const uint8_t shade_g =
                static_cast<uint8_t>((shade >> 8) & 0xFFu);
            const uint8_t shade_b =
                static_cast<uint8_t>(shade & 0xFFu);
            // The bed at its feet stays, thicker than before, and a
            // column of its own colour rises off its head at every
            // beat: the beats themselves quicken as the wick goes,
            // so the plume pulses the count out loud.
            vapour(0, proto::SmokeStyle::Ground, at, 2,
                   shade_r, shade_g, shade_b, 0x70, 2, 1);
            vapour(1, proto::SmokeStyle::Column, at, 1,
                   shade_r, shade_g, shade_b, 0xC0, 15, 5);
        }
    }

    void boom_blast(Player& p) {
        const Vec3 c{p.body.pos.x, p.body.pos.y, p.body.pos.z};
        const int bx = static_cast<int>(std::floor(c.x));
        const int by = static_cast<int>(std::floor(c.y));
        const int bz = static_cast<int>(std::floor(c.z));
        // The dig: a hole two blocks each way and four high, brooked
        // by nothing, the core's own column kept whole against it.
        std::vector<Cell> removed;
        bool dug = false;
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 2; ++dy)
                for (int dz = -3; dz <= 0; ++dz) {
                    const int X = bx + dx;
                    const int Y = by + dy;
                    const int Z = bz + dz;
                    if (!map.solid(X, Y, Z)) continue;
                    if (core_column_cell(X, Y, Z)) continue;
                    map.destroy_point(X, Y, Z, &removed);
                    ByteWriter bw;
                    bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
                    bw.write_u8(static_cast<uint8_t>(p.id));
                    bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
                    bw.write_i32(X);
                    bw.write_i32(Y);
                    bw.write_i32(Z);
                    broadcast(bw, true);
                    dug = true;
                }
        if (dug) touched(removed, false);
        // The bite: the grenade's own blast, the same box of reach,
        // the same fall with distance, the same wall between - each
        // body it sees pays the grenade's own toll, dead side and
        // people alike, and a boomer caught in a boomer's burst goes
        // with it, the chain ending where the sight lines no longer
        // read.
        for (auto& q : players) {
            if (!q || !q->alive || q.get() == &p) continue;
            if (!q->playing()) continue;
            const float dmg =
                nade::blast_damage(&phys_map, c, q->body.pos);
            if (static_cast<int>(dmg) <= 0) continue;
            set_hp(*q, q->hp - static_cast<int>(dmg), nullptr,
                   kGrenadeKill, &c);
        }
        // The core, when the burst reaches its ground: the dig brims
        // four cells out, and the core standing in them pays.
        if (horde->near_core(bx, by, 4))
            horde->on_core_damaged(boomer_core_hurt);
        // The say: the rocket's own burst, the one the fork stood
        // behind - a blast style the client paints at a stroke: the
        // wide clouds, the light that dies in half a second with
        // the boom riding it, then the dark smoke that climbs. A
        // client that knows no blast reads a plain cloud instead.
        const Vec3 at{bx + 0.5f, by + 0.5f, bz + 1.0f};
        vapour(0, proto::SmokeStyle::Blast, at, 4, 0xE0, 0x60, 0x00,
               0xB0, 8, 2);
    }

    // The Breaker: the core is the road entire, and the line between is
    // walked as straight as the map allows the aim - no field, no
    // advice, no detour a cliff asks for, and falls are walked honestly
    // off every edge on the way. What stands on the line comes down:
    // all of it alike, the stair of one as much as the wall, carved at
    // the face, the focus column and its two mates, a cut four high at
    // the walker's own pace of stroke, onto rock only - the air
    // rides along unharmed and the core holds against every cell, for
    // the teeth answer the core standing on its own ground, as they do
    // for any walker that arrives. The charge sprints while the march
    // is long, and walks the line in close. And the line is a siege of
    // the last stretch only: outside breaker_dig_range blocks of the
    // core the breaker walks the horde's road like any walker, and
    // steer_zombie hands it to this tick at the edge of that band.
    void breaker_tick(Player& p, int cx, int cy, int fz) {
        const float dx = static_cast<float>(horde_cfg.core_x) + 0.5f -
                         p.body.pos.x;
        const float dy = static_cast<float>(horde_cfg.core_y) + 0.5f -
                         p.body.pos.y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > 1e-3f) p.body.ori = Vec3{dx / d, dy / d, 0.0f};
        p.input.mf = true;
        p.input.sprint = d > kBreakerCharge;
        if (p.body.airborne) return;
        int fx = 0, fy = 0;
        bool foot = false, head = false, sky = false;
        face_ahead(p, cx, cy, fz, fx, fy, foot, head, sky);
        if (!foot && !head) return;   // the line is open: walk it
        // Everything on the line comes down, even the stair of one
        // a walker would hop: the dig buys its own road twice over.
        // Only when there is no licence to dig at all does the old
        // stair-hop mercy stand in its place.
        if (!horde_cfg.dig) {
            if (!sky)
                p.input.jump = true;   // no licence to dig: the stair-hop
            return;
        }
        if (world_time < p.spade_at + breaker_stroke) return;
        // The stroke, standing at the face: the walker stops pressing
        // and works, mates onto rock only, never the core column, and
        // every voxel that comes off carries its own word to the wire.
        p.input.mf = false;
        const int lx = fx - cx != 0 ? 0 : 1;
        const int ly = fx - cx != 0 ? 1 : 0;
        bool stroked = false;
        std::vector<Cell> removed;
        // The cut stands four high over the ground it walks, three
        // mates wide: its own way through, and what the wave behind
        // it makes of the breach is the business of the wave. The
        // road was an experiment, and the experiment is over.
        for (int i = -1; i <= 1; ++i) {
            const int bx = fx + lx * i;
            const int by = fy + ly * i;
            for (int z = fz - 3; z <= fz; ++z) {
                if (!map.solid(bx, by, z)) continue;
                if (core_column_cell(bx, by, z)) continue;
                map.destroy_point(bx, by, z, &removed);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
                bw.write_i32(bx);
                bw.write_i32(by);
                bw.write_i32(z);
                broadcast(bw, true);
                stroked = true;
            }
        }
        if (stroked) {
            touched(removed, false);
            p.spade_at = world_time;
            show_swing(p, breaker_stroke);
        }
    }

    // The healer never leads: with nobody to follow it takes its place
    // behind the walker nearest it, inside the span a shadow keeps - or,
    // alone on the field with nothing to shadow, it walks the field
    // itself, a leader of nobody for as long as it stands alone. The
    // pack it sheds finds a different head, so no walker ever walks
    // behind a healer.
    bool follow_the_near(Player& p) {
        Player* shadow = nullptr;
        float best = -1.0f;
        for (auto& qq : players) {
            if (!qq || !qq->zombie || !qq->alive || qq.get() == &p)
                continue;
            const float ddx = qq->body.pos.x - p.body.pos.x;
            const float ddy = qq->body.pos.y - p.body.pos.y;
            const float dd2 = ddx * ddx + ddy * ddy;
            if (shadow == nullptr || dd2 < best) {
                best = dd2;
                shadow = qq.get();
            }
        }
        if (shadow == nullptr || best > kHealJoin * kHealJoin) return false;
        p.ahead_id = shadow->id;
        return true;
    }

    // A zombie's mind: its pack walks one path - it holds at the heel of the
    // zombie in front, and the one at the head reads the field, towards the
    // core. The field is the walk without the spade - stairs, ramps and
    // notches, never a wall hacked through - and a stuck one jumps for the
    // step it could not make.
    void steer_zombie(Player& p) {
        p.input = physics::Input{};
        const int cx = static_cast<int>(std::floor(p.body.pos.x));
        const int cy = static_cast<int>(std::floor(p.body.pos.y));
        // The floor its feet are on: the lowest probe of the standing box,
        // truncated to the voxel - the same reading the surfaces are. The
        // advice is taken from HERE, so the field is read from the ground
        // the zombie truly stands on, not from whatever bargain its column
        // keeps under it; the yo-yo of a leader sent after a floor its
        // legs can never reach is advised out of existence.
        const int fz = static_cast<int>(std::floor(p.body.pos.z + 2.25f));
        // The print of the heel: every half-pace of ground truly stood
        // on is set into the ring, in the order it was walked. Nothing
        // is printed in the air - a flung body crosses ground no
        // following leg may tread; the next print says where it walked
        // on from wherever it came down.
        if (!p.body.airborne) {
            const float prx = p.body.pos.x - p.heel_ref_x;
            const float pry = p.body.pos.y - p.heel_ref_y;
            if (prx * prx + pry * pry >= 0.25f) {
                p.heel_x[p.heel_head] = p.body.pos.x;
                p.heel_y[p.heel_head] = p.body.pos.y;
                p.heel_seq[p.heel_head] = heel_stamp();
                p.heel_ref_x = p.body.pos.x;
                p.heel_ref_y = p.body.pos.y;
                p.heel_head = (p.heel_head + 1) % Player::kHeelN;
            }
        }
        // The crowd: players pass through players in this game, so the horde
        // keeps itself apart - a soft push off any of its own inside one
        // block of reach, the weight fading with the room between them.
        // A fresh one is spared for its first seconds: it walks out of the
        // pile through its own, and they walk through it, until the wave
        // has its shape back.
        // And the pack's own head is no source of the push, for the head
        // is the very door the line pours through: the breach it opens
        // stands on the ground its own two feet hold, and a heel shoved
        // off the one that opens the way is a heel kept from the way
        // forever - the pack masses at its head and pours past it, the
        // leader keeps its ground.
        Vec3 sep{};
        for (const auto& qq : players) {
            if (!qq || qq.get() == &p || !qq->zombie || !qq->alive) continue;
            if (qq->ahead_id < 0) continue;   // the head is the door
            if (world_time - qq->born_at < kZombieSettle) continue;
            const float dx = p.body.pos.x - qq->body.pos.x;
            const float dy = p.body.pos.y - qq->body.pos.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < 1e-6f) {   // stood in the same skin: part by number
                sep.x += (p.id & 1) != 0 ? 0.5f : -0.5f;
                sep.y += (p.id & 2) != 0 ? 0.5f : -0.5f;
                continue;
            }
            if (d2 >= 1.0f) continue;
            const float d = std::sqrt(d2);
            const float w = 1.0f - d;
            sep.x += dx / d * w;
            sep.y += dy / d * w;
        }
        if (zombie_at_core(cx, cy, fz)) {
            // It bites, it does not walk - but it still shuffles its
            // neighbours out of its skin. One roofing the room the core
            // hides in is not eating through the rock by standing over
            // it: it walks on, it digs, it comes down for a real bite.
            const float sl = std::sqrt(sep.x * sep.x + sep.y * sep.y);
            if (sl > 0.15f) {
                p.body.ori = Vec3{sep.x / sl, sep.y / sl, 0.0f};
                p.input.mf = true;
            }
            return;
        }
        // A Spitter rides no pack: the pack pours along its road with
        // its teeth ahead of it, and a ranged brute carried at a heel
        // walks clean past the band it was made to keep. Its own hunt
        // is its road - the stand, the arc, the walk for a riding
        // line - so it hands the pack on and walks the hunt alone.
        // The Boomer's short chase: inside the reach the tuning draws,
        // the person's own ground is its road - the burst is a tooth
        // of contact, and neither the pack's prints nor the road to
        // the core hold it out of the wick's way. Outside that reach
        // it walks the pack's road like any walker of the ramp.
        const bool boomer_chases =
            p.brute == kBoomer && hunt_focus_ != nullptr &&
            hunt_focus_->alive &&
            (hunt_focus_->body.pos.x - p.body.pos.x) *
                (hunt_focus_->body.pos.x - p.body.pos.x) +
            (hunt_focus_->body.pos.y - p.body.pos.y) *
                (hunt_focus_->body.pos.y - p.body.pos.y) <=
            static_cast<float>(boomer_chase * boomer_chase);
        // The breaker's siege is the last stretch only: out past
        // breaker_dig_range blocks of the core it walks the road the
        // whole horde walks, and its straight line begins at the edge
        // of that band - a tunnel is a tool of the siege, and the
        // siege begins where the core stands within its reach.
        const bool breaker_digs =
            p.brute == kBreaker &&
            (static_cast<float>(horde_cfg.core_x) + 0.5f - p.body.pos.x) *
                (static_cast<float>(horde_cfg.core_x) + 0.5f - p.body.pos.x) +
            (static_cast<float>(horde_cfg.core_y) + 0.5f - p.body.pos.y) *
                (static_cast<float>(horde_cfg.core_y) + 0.5f - p.body.pos.y) <=
            static_cast<float>(breaker_dig_range * breaker_dig_range);
        Player* pack_ahead = p.ahead_id >= 0
            ? players[static_cast<size_t>(p.ahead_id)].get() : nullptr;
        const Player* ahead =
            p.brute == kSpitter || p.brute == kBreaker ||
            (p.brute == kLeaper && p.hunts) || boomer_chases
                ? nullptr : pack_ahead;
        // The hunt owns its hunter only while the field of it speaks
        // at the feet: flooded from the person, over ground the legs
        // themselves can walk, it holds no word about a perch, a
        // tower, a town the person climbed or sealed beyond the
        // reach of the walk. And silence is not a standstill the
        // hunt answers: the press it leaves behind - straight at a
        // wall the field has no word about - is a wedge no clock of
        // the hunt opens, for the hunt never rode the mercy and
        // never drew the spade. So a hunt gone silent hands its
        // hunter to the road the whole pack walks - to the core,
        // with the clocks, the spade and the mercy that road has
        // always kept - and takes the hunter back the moment the
        // field reads again: a breach the dead open, a step the
        // person takes down off their perch, and the chase is on
        // once more. A Spitter keeps its stand even so while its
        // line rides - the arc crosses what the walk refuses - and
        // a Leaper mid pattern keeps its two flights: the pattern
        // owns its own air.
        const bool hunts_mind = p.brute == kSpitter ||
                                (p.brute == kLeaper && p.hunts);
        const bool person_live =
            hunt_focus_ != nullptr && hunt_focus_->alive;
        const bool hunt_blind = hunts_mind &&
            (!person_live ||
             (p.leap_stage == 0 &&
              horde->hunt_reach(cx, cy) < 0 &&
              (p.brute == kLeaper ||
               p.spit_off > kSpitOffShut)));
        if (ahead && ahead->id != p.id && ahead->zombie && ahead->alive) {
            // The heel held in the footsteps of the one ahead: a follower
            // is led by the prints its leader left - the threshold it
            // crossed, the wall it frayed along, the corner it took -
            // and never by the open air's own straight line, which
            // shaves the bend a threshold sat in and leaves the end of
            // the line stranded on the wrong side of a step the leader
            // never even saw. A spade's length of leeway once the wave
            // has its shape: close enough to hold the line, far enough
            // to let the one in front swing. A fresh one walks at the
            // heel instead - a block behind at most - for its first
            // seconds are the pile's, and the pile leaves as one body
            // or it stands and stands while the gap it waits on opens a
            // spade's length at a time.
            const float adx = ahead->body.pos.x - p.body.pos.x;
            const float ady = ahead->body.pos.y - p.body.pos.y;
            const float l = std::sqrt(adx * adx + ady * ady);
            const float leash = world_time - p.born_at < kZombieSettle ? 1.0f : 1.6f;
            const int print = ahead_print(*ahead, p.body.pos.x, p.body.pos.y);
            // The strayed one takes the pack's head's read: a print owed
            // behind or abreast, or nothing owed with the line's end past
            // the ring's whole depth, means the walker is off the trail -
            // and doubling back for the miss would engrave it into the
            // trail for every heel behind to walk again. So it reads the
            // field instead, the walk the whole of it shares, never the
            // open air's own bearing, and the corridor it marches is the
            // one the line marches: the nearest print ahead stands
            // unmasked, and the chase resumes on its own. And the head
            // stalled loses its line for a while the same way: stuck
            // past the disband's own patience and not swinging at a
            // wall it already opens for the line, it owes prints it
            // cannot walk, and the follower reads the field for itself,
            // a way down of its own rather than a place in a standstill
            // that never ends - the field's way out of a maze loop
            // steps away from the core and nearer all the while, the
            // walk's count being steps, not miles.
            const bool head_disbanded =
                ahead->stuck_at >= 0.0 &&
                world_time - ahead->stuck_at >= kStuckHeadDisband &&
                world_time - ahead->spade_at >= kStuckHeadDisband;
            bool strayed = (print < 0 && l > leash) || head_disbanded;
            if (print >= 0 && !strayed) {
                const float dx = ahead->heel_x[print] - p.body.pos.x;
                const float dy = ahead->heel_y[print] - p.body.pos.y;
                const float dl = std::sqrt(dx * dx + dy * dy);
                // Squared against the bearing to the one it follows: the
                // cosine of the angle between the print's pull and the
                // line's own pull, cheap and free of any square root the
                // chase below has not already paid.
                if (dl > 1e-3f &&
                    (l <= 1e-3f ||
                     (dx * adx + dy * ady) / (dl * l) >= kPrintAside)) {
                    p.body.ori = Vec3{dx / dl, dy / dl, 0.0f};
                    p.input.mf = true;
                    // The caught line climbs the treads of its own trail
                    // as well: the print says which way, the field says
                    // what the crossing there costs, and a tread priced
                    // one jump on the way the print itself pulls gets
                    // the vault ahead of the stuck clock. A face the
                    // trail never climbs keeps its old answer.
                    int gx = cx, gy = cy, gz = fz;
                    if (!p.body.airborne &&
                        horde->next_step(cx, cy, fz, gx, gy, gz)) {
                        const float ex = static_cast<float>(gx) + 0.5f -
                                        p.body.pos.x;
                        const float ey = static_cast<float>(gy) + 0.5f -
                                        p.body.pos.y;
                        const float el = std::sqrt(ex * ex + ey * ey);
                        if (el > 1e-3f &&
                            (ex * dx + ey * dy) / (el * dl) >= kPrintAside)
                            vault_crossing(p, fz, gz, el);
                    }
                    // The catch: the run that draws a strung-out follower
                    // back onto its leader's heels - kept off the stairs
                    // and banks, where a run cannot take its own step.
                    p.input.sprint = l > kPackCatch &&
                                    std::fabs(ahead->body.pos.z - p.body.pos.z) < 0.5f;
                } else {
                    strayed = true;
                }
            }
            if (strayed) {
                int tx = cx, ty = cy, tz = fz;
                // The field read from the footing, as the head reads it -
                // and, the same mercy, the cell's own read in the air.
                // The read at the feet keeps the surface the crossing it
                // names, and the climb the errand may ask on the way.
                const bool from_footing =
                    !p.body.airborne &&
                    horde->next_step(cx, cy, fz, tx, ty, tz);
                const bool led =
                    from_footing || horde->next_step(cx, cy, tx, ty);
                if (led) {
                    // The centre of the cell, never its corner.
                    const float tdx = static_cast<float>(tx) + 0.5f -
                                      p.body.pos.x;
                    const float tdy = static_cast<float>(ty) + 0.5f -
                                      p.body.pos.y;
                    const float tdl = std::sqrt(tdx * tdx + tdy * tdy);
                    if (tdl > 1e-3f) {
                        p.body.ori = Vec3{tdx / tdl, tdy / tdl, 0.0f};
                        p.input.mf = true;
                        p.input.sprint = l > kPackCatch &&
                                        std::fabs(ahead->body.pos.z - p.body.pos.z) < 0.5f;
                    }
                    if (from_footing)
                        vault_crossing(p, fz, tz, tdl);
                } else if (dig_lead(p, false)) {
                    // The field had nothing and the spade's walk had a
                    // way: the strayed one walks it - and where it
                    // climbs, it is the pack's head's work to open, not
                    // a heel's; the heel waits at the wall, held on the
                    // line of prints it still owes.
                } else if (print >= 0) {
                    // The field silent - at the core's own cell it always
                    // is - the print is paid rather than the walker stand:
                    // the walker stood on the walk, the trail holds it.
                    const float dx = ahead->heel_x[print] - p.body.pos.x;
                    const float dy = ahead->heel_y[print] - p.body.pos.y;
                    const float dl = std::sqrt(dx * dx + dy * dy);
                    if (dl > 1e-3f) {
                        p.body.ori = Vec3{dx / dl, dy / dl, 0.0f};
                        p.input.mf = true;
                        p.input.sprint = l > kPackCatch &&
                                        std::fabs(ahead->body.pos.z - p.body.pos.z) < 0.5f;
                    }
                } else if (l > leash) {
                    // Off the ring's depth with the field silent as well:
                    // the air line remains, a slow walk over broken ground
                    // still beats a standstill at the line's very end.
                    p.body.ori = Vec3{adx / l, ady / l, 0.0f};
                    p.input.mf = true;
                    p.input.sprint = l > kPackCatch &&
                                    std::fabs(ahead->body.pos.z - p.body.pos.z) < 0.5f;
                } else {
                    p.body.ori = ahead->body.ori;
                }
            } else if (print < 0) {
                p.body.ori = ahead->body.ori;
            }
        } else {
            // Nobody to follow: it is the pack's leader now, and the field
            // leads it - around the cliffs, when it can - unless the walker
            // is a brute of a mind of its own, whose road was never the
            // walk: the hunt, the Boomer's short chase and the line
            // hand the pack over first.
            p.ahead_id = -1;
            if (hunts_mind && !hunt_blind) {
                hand_pack_over(p);
                hunt_tick(p, cx, cy, fz);
                if (p.brute == kLeaper) leap_tick(p, cx, cy, fz);
            } else if (boomer_chases) {
                hand_pack_over(p);
                hunt_tick(p, cx, cy, fz);
            } else if (breaker_digs) {
                hand_pack_over(p);
                breaker_tick(p, cx, cy, fz);
            } else if (p.brute == kHealer && follow_the_near(p)) {
                hand_pack_over(p);
                // A walker stands in its shadow: the follower branch
                // holds the healer at a heel from the next tick on.
            } else {
                if (p.brute == kHealer || hunt_blind)
                    hand_pack_over(p);
            int tx = cx, ty = cy, tz = fz;
            // The field read from the footing - and when the feet are in
            // the air, or on a floor the walk has no opinion about, the
            // cell's own read instead, the one it has always had.
            // The read at the feet keeps the surface the crossing runs onto,
            // and with it the climb the advice is asking for.
            const bool from_footing =
                !p.body.airborne && horde->next_step(cx, cy, fz, tx, ty, tz);
            const bool led =
                from_footing || horde->next_step(cx, cy, tx, ty);
            // The honest word of the walk, read at the feet: ground the
            // legs reach has a word, ground they never joined has none.
            // The straight-line fallback inside the advice above is a
            // walking aid for a broken field, not a word of the walk - a
            // leader pressed to a town wall from a moat its feet are
            // stuck in paces on ground the walk is silent on, and a
            // licence gate fed the fallback would keep the spade from
            // the one walker it exists for. The advice still steers the
            // legs; only the licence is told the truth.
            const bool on_legs =
                !p.body.airborne && horde->distance(cx, cy, fz) >= 0;
            // The spade's second opinion, taken on the licences of the
            // pinned: the stuck clock run out, the watch run down - and
            // where the dig walk says climb, the head digs and the line
            // waits.
            const bool digs = dig_lead(p, on_legs);
            if (led && !digs) {
                // The cell handed back is a square the legs are steered into,
                // so it is its CENTRE they make for - never its corner. A
                // corner is the point four cells meet at, a line's width of
                // nowhere that belongs to no step: a walker aimed there
                // crosses the boundary as it arrives, becomes the next cell's
                // to advise, is handed a corner its own side of the line,
                // and paces across the crossing forever - there and back,
                // moving all the while, never a cell nearer. The centre is
                // one cell's own ground: once set foot in, the next advice
                // is read from inside it, and the walk goes on.
                const float dx = static_cast<float>(tx) + 0.5f - p.body.pos.x;
                const float dy = static_cast<float>(ty) + 0.5f - p.body.pos.y;
                const float l = std::sqrt(dx * dx + dy * dy);
                if (l > 1e-3f) {
                    p.body.ori = Vec3{dx / l, dy / l, 0.0f};
                    p.input.mf = true;
                }
                // The climb the advice priced one jump is owed the hop
                // the moment the walker stands under it - the lip of two
                // pressed at its face is the very stair the crossing
                // meant, and waiting on the stuck clock for it is what
                // filled the stairs with the milling crowd.
                if (from_footing)
                    vault_crossing(p, fz, tz, l);
            }
            }   // the grunt walk ends; the brute minds closed above
        }
        // The shuffle apart, folded into the facing: a walker veers a step
        // out of a knot, a standstill in reach of its own becomes the
        // shuffle outright. Politeness stands aside for a stuck walker -
        // shuffling one off a wall it leans on would read as progress and
        // unarm the hop, and the hop is the only way out it has. It stands
        // aside for the pack's head too: the field leads it, and the crowd
        // it parts must never bend it, for the line behind goes where it
        // goes. And it has not begun for a fresh one: it is still walking
        // out of the pile it rose in.
        const float sl = std::sqrt(sep.x * sep.x + sep.y * sep.y);
        if (p.ahead_id >= 0 && world_time - p.born_at >= kZombieSettle &&
            sl > 0.15f && p.stuck_at < 0.0) {
            const float ox = p.body.ori.x + sep.x * 0.7f;
            const float oy = p.body.ori.y + sep.y * 0.7f;
            const float ol = std::sqrt(ox * ox + oy * oy);
            if (ol > 1e-3f) {
                p.body.ori = Vec3{ox / ol, oy / ol, 0.0f};
                p.input.mf = true;
            }
        }
        // The unstick. Progress is measured flat, over the ground it stands
        // on: a hop against a step gains none, so the jump does not wait on
        // where in its bounce the clock samples it. The walk's own answer to
        // a standstill is the hop; the spade takes walls down only where
        // the dig walk says the price is paid in. And pacing is a
        // standstill to the walk - the yo-yo of a leader that crosses to a
        // floor it cannot hold and turns back, over and over, moves a
        // window's worth in every window and so slips the moved measure
        // whole. So the leader is scored by the field itself: the distance
        // read at its feet, kept window by window, and windows without a
        // lower floor underfoot are counted until they are the yo-yo's own
        // three seconds - then the same hop and the same fling are owed,
        // for whatever the pacing faces is the thing to vault, and the
        // vault is the only way out a pacer has. A follower keeps no
        // score: holding a heel's place in the line stirs on the spot by
        // design.
        // The unstick, shelved while the spade works: a digger stands by
        // design, and its standstill is the pace of its own work - the
        // windows wait out each stroke's dig_seconds before they score
        // again, so no hop and no fling spoils the work at the wall.
        if (world_time - p.walk_ref_at >= 0.5 &&
            world_time - p.spade_at >= horde_cfg.dig_seconds) {
            const float moved = std::fabs(p.body.pos.x - p.walk_ref.x) +
                                std::fabs(p.body.pos.y - p.walk_ref.y);
            p.walk_ref = p.body.pos;
            p.walk_ref_at = world_time;
            const int fd =
                p.body.airborne ? -1 : horde->distance(cx, cy, fz);
            // The score off the field that leads the walker: the legs read
            // first, and where the legs run silent the spade's own count
            // stands in - the walker the dig walk leads crosses ground the
            // legs price no way over, the hill above a sealed room, the wall
            // a maze turns it against, and its progress is that walk's own
            // count, never the silence of the field it was never on. And
            // the silence of BOTH - a leader pacing with its feet nowhere
            // either walk counts - is scored window by window instead of
            // excused: the pacing is a standstill to the walk either way,
            // and only counted windows arm the hop, the fling, the spade's
            // licence and the line's disband to answer it.
            // A hunter's standstill is its design only while the
            // design itself stands: the hold of a Spitter with a
            // riding line inside its band, and the coil and the
            // flights of a Leaper mid pattern. Every other step a
            // hunter takes is walking ground, owed the clocks of any
            // walker - and scored on the hunt while the hunt speaks,
            // on the road while the hunter walks it blind, for the
            // pacing that gains none is the standstill the windows
            // below are owed by. The hold that keeps a band bounces
            // on the spot no longer, and the coil no longer springs
            // on its own perch.
            bool hunts_on_own = false;
            if (hunts_mind && person_live) {
                if (p.brute == kLeaper) {
                    hunts_on_own = p.leap_stage != 0;
                } else {
                    const float hdx = hunt_focus_->body.pos.x -
                                      p.body.pos.x;
                    const float hdy = hunt_focus_->body.pos.y -
                                      p.body.pos.y;
                    const float hdd = std::sqrt(hdx * hdx + hdy * hdy);
                    const float band = std::max(1.0f, std::min(
                        kSpitHold, static_cast<float>(spitter.range)));
                    hunts_on_own = p.spit_off <= kSpitOffShut &&
                                  hdd <= band + 0.5f;
                }
            }
            if (hunts_on_own) p.stuck_at = -1.0;
            int score = fd >= 0 ? fd : horde->dig_distance(cx, cy, fz);
            if (hunts_mind && !hunts_on_own && !hunt_blind) {
                const int hunt_own = horde->hunt_reach(cx, cy);
                if (hunt_own >= 0) score = hunt_own;
            }
            // The dig watch, leader only: the milestone is a reading
            // kDigWatchGain lower than the one it holds, and that
            // alone winds it - the shuffle that hides a pack from the
            // stuck clock gains nothing it can cash here. Pacing where
            // neither walk reads stands the watch down: no milestone,
            // no licence owed for feet with nowhere to dig.
            if (!hunts_on_own && p.ahead_id < 0 && score >= 0) {
                if (p.dig_watch_at < 0.0 ||
                    score <= p.dig_watch_dist - kDigWatchGain) {
                    p.dig_watch_at = world_time;
                    p.dig_watch_dist = score;
                }
            } else if (p.ahead_id < 0 && score < 0 &&
                       p.dig_watch_at >= 0.0) {
                p.dig_watch_at = -1.0;   // nowhere either walk reads
            }
            // The detour clock, leader only: the two walks weighed at
            // the feet, window by window. A leg field that reads, a spade
            // field that reads, and a walk held kDigDetour above the
            // spade own crossing: that alone is a detour kept out of
            // the honesty of the walk, and the clock runs on it. A
            // march that prices fair, a spade field gone silent, a
            // head off the walk or a follower in line stands the
            // clock down - the road is the road again.
            if (!hunts_on_own && p.ahead_id < 0 && fd >= 0) {
                const int dd = horde->dig_distance(cx, cy, fz);
                if (dd >= 0 && fd >= dd + kDigDetour) {
                    if (p.detour_at < 0.0) p.detour_at = world_time;
                } else {
                    p.detour_at = -1.0;   // the detour is off: hold
                }
            } else if (p.detour_at >= 0.0) {
                p.detour_at = -1.0;       // no weighing to hold
            }
            if (hunts_on_own || p.ahead_id >= 0 || p.body.airborne) {
                p.foot_dist = -1;          // off the walk or a step in line
                p.stall_windows = 0;
            } else if (score < 0) {
                p.foot_dist = -1;          // pacing: nowhere either walk
                ++p.stall_windows;         // counts, and the windows add up
            } else if (p.foot_dist < 0 || score < p.foot_dist) {
                p.foot_dist = score;        // a lower floor reached: it walks
                p.stall_windows = 0;
            } else {
                ++p.stall_windows;         // moving, maybe - nearer, never
            }
            if (!hunts_on_own &&
                (moved < 0.35f ||
                 p.stall_windows >= kZombieYoyoWindows)) {
                // The probe the map asked for: a window stood still says
                // where the walker is, what each walk reads at its feet,
                // what the advice hands back, and what every clock of
                // the pinned has come to - one line a walker, no sooner
                // than the probe beat, that the log stays readable.
                if (map_probe && world_time - p.probe_at >= kProbeAgain) {
                    p.probe_at = world_time;
                    const int legs = p.body.airborne
                                         ? -2 : horde->distance(cx, cy, fz);
                    const int spade = p.body.airborne
                                         ? -2 : horde->dig_distance(cx, cy, fz);
                    int qx = cx, qy = cy, qz = fz;
                    const bool has_advice = !p.body.airborne &&
                                            horde->next_step(cx, cy, fz,
                                                             qx, qy, qz);
                    log("probe %d: at %d,%d,%d ahead %d, legs %d, spade %d,"
                        " advice %d -> %d,%d,%d, stuck %.1f, watch %.1f,"
                        " detour %.1f, spade_at %.1f",
                        p.id, cx, cy, fz, p.ahead_id, legs, spade,
                        static_cast<int>(has_advice), qx, qy, qz,
                        p.stuck_at < 0.0 ? -1.0 : world_time - p.stuck_at,
                        p.dig_watch_at < 0.0
                            ? -1.0 : world_time - p.dig_watch_at,
                        p.detour_at < 0.0
                            ? -1.0 : world_time - p.detour_at,
                        world_time - p.spade_at);
                }
                // A heel held is a place kept, not a standstill: within
                // the sight of the one ahead on LEVEL ground the window
                // is shelved - but a heel one step down the stair holds
                // no place at all, the place itself stands a step up,
                // and the hop the climb asks is owed however close the
                // one ahead stands: a line that cannot hop while its
                // own head is in sight is a line that never takes a
                // stair, and the mouth of it fills with the stuck.
                if (ahead && ahead->id != p.id && ahead->zombie &&
                    ahead->alive) {
                    const float sdx = ahead->body.pos.x - p.body.pos.x;
                    const float sdy = ahead->body.pos.y - p.body.pos.y;
                    if (sdx * sdx + sdy * sdy <
                        kHeelSight * kHeelSight) {
                        if (std::fabs(ahead->body.pos.z - p.body.pos.z) <
                            1.0f) {
                            // The probe: the pile each member of it can
                            // see the one ahead of stays shelved here,
                            // hop and fling and spade all disarmed - the
                            // doctor wants the shelving said out loud.
                            if (map_probe &&
                                world_time - p.probe_at >= kProbeAgain) {
                                p.probe_at = world_time;
                                log("probe %d: at %d,%d,%d shelved by sight"
                                    " of ahead %d at dz %.1f",
                                    p.id, cx, cy, fz, p.ahead_id,
                                    static_cast<double>(
                                        ahead->body.pos.z - p.body.pos.z));
                            }
                            p.stuck_at = -1.0;
                            return;
                        }
                    }
                }
                if (p.stuck_at < 0.0) p.stuck_at = world_time;
                // The fling. The reading crosses steps two blocks tall at
                // the price of a jump, and a jump alone was never going to
                // clear one pressed against its face - the hop rises a
                // block and a half and the wall takes the rest out of it.
                // So a walker still standing after what its own hop could
                // give lunges at the step instead: thrown clear of what it
                // faces, high enough to land standing on it, shoulder to
                // the way out until the next try. Every walker has the
                // same fling in it, the pack's head as much as its heel:
                // where the leader is flung the whole line goes after.
                //
                // The fling is the whole jump of its tick: the step's own
                // jump gate writes its smaller hop over any velocity the
                // key asks with, so on the tick the lunge flies the key
                // is held back - flung once, the hop resumes next window.
                bool flung = false;
                if (world_time - p.stuck_at >= kPounceWindup &&
                    world_time - p.pounce_at >= kPounceAgain && !p.body.airborne) {
                    p.body.vel.z = kDeadPounce;
                    p.body.vel.x += p.body.ori.x * 0.15f;
                    p.body.vel.y += p.body.ori.y * 0.15f;
                    p.pounce_at = world_time;
                    flung = true;
                }
                if (!flung) p.input.jump = true;
            } else {
                p.stuck_at = -1.0;
            }
            // The last resort, asked only after the walk's own answers
            // have all failed the head: the pinned line rides its road
            // down (teleport_pinned above, for the prose of it).
            if (p.ahead_id < 0 && p.stuck_at >= 0.0 &&
                world_time - p.stuck_at >= kStuckTeleport &&
                // the mercy rides the road to the core: a hunter
                // wedged on walking ground rides it like any walker,
                // and only the stands the hunt itself designs stay
                // off it - the breaker walking with the horde rides
                // it, the breaker digging its line carves on its own
                // clock
                !hunts_on_own && !breaker_digs)
                teleport_pinned(p);
        }
    }
    // The teeth's reach to the core: the cells around it in the plan,
    // AND the walker's own footing in the band of the column's ground -
    // its own level, give or take a pounce and a block stood on. The
    // plan alone says near on any height, and a grave was dug with it:
    // a horde walking the hill that roofs the room the core hides in
    // ate through the rock by standing over it. Now it digs, it comes
    // down, and the teeth meet the core standing on its own ground.
    bool zombie_at_core(int cx, int cy, int fz) const {
        return horde->near_core(cx, cy, 2) && fz > core_ground_z - 3 &&
               fz <= core_ground_z + 1;
    }
    // Teeth: the core first, any person in reach second. A bite a second.
    void zombie_bite(Player& p) {
        if (world_time < p.next_bite) return;
        const int cx = static_cast<int>(std::floor(p.body.pos.x));
        const int cy = static_cast<int>(std::floor(p.body.pos.y));
        const int fz = static_cast<int>(std::floor(p.body.pos.z + 2.25f));
        bool bit = false;
        if (zombie_at_core(cx, cy, fz)) {
            horde->on_core_damaged(10);
            bit = true;
            if (world_time >= core_alarm_at) {
                core_alarm_at = world_time + 4.0;
                char text[96];
                std::snprintf(text, sizeof text, "The core is under attack! Core %d",
                             horde->core_hp());
                system_chat(text);
            }
        }
        // A gun in the way and at the walker's own height is gnawed at
        // like any wall the teeth come to: the pillar is two real blocks,
        // and the way a horde has always had against walls is patience,
        // at the blocks. Only a pillar that stands IN the lane pays: two
        // blocks off the line at most, and at the walker's own band of
        // height, the footing cell and two above it. A gun set on a
        // perch or beside the stream is never so much as looked at, and
        // that margin is the whole game of the placing.
        for (auto& t : turrets) {
            if (bit) break;
            const float gdx = static_cast<float>(t.x) + 0.5f - p.body.pos.x;
            const float gdy = static_cast<float>(t.y) + 0.5f - p.body.pos.y;
            if (gdx * gdx + gdy * gdy > 4.0f) continue;
            if (t.z > fz || t.z < fz - 2) continue;
            int bz = t.z;
            if (!map.solid(t.x, t.y, bz)) bz = t.z - 1;
            if (!map.solid(t.x, t.y, bz)) continue;
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
            bw.write_u8(static_cast<uint8_t>(p.id));
            bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
            bw.write_i32(t.x);
            bw.write_i32(t.y);
            bw.write_i32(bz);
            broadcast(bw, true);
            std::vector<Cell> removed;
            map.destroy_point(t.x, t.y, bz, &removed);
            touched(removed, false);
            bit = true;
            break;
        }
        for (auto& qq : players) {
            if (bit) break;
            // The Spitter keeps its band: its answer to a person is
            // the arc and never the teeth, at whatever distance the
            // hunt hands it. The core and the guns stay walls by
            // the walker's rule, and the pool of its acid is the
            // bite it lands.
            if (p.brute == kSpitter) break;
            if (!qq || qq->zombie || !qq->alive || qq->spectator() || qq->team != kPeople) continue;
            if (!near3(p.body.pos, qq->body.pos, kMeleeDistance)) continue;
            hit(*qq, 34, &p, kMeleeKill);
            bit = true;
            break;
        }
        if (bit) {
            p.next_bite = world_time + 0.8;
            show_swing(p, 0.4);   // the teeth the clients animate
        }
    }

    // The memory of the Spitter: where the people were. A sample every
    // kPastBeat into the ring of its slot, held kPastKeep, read back at
    // whatever lag the tuning asks.
    void remember_place(Player& p) {
        PastPlace& h = past[p.id];
        if (h.n > 0 &&
            world_time - h.t[(h.head + kPastN - 1) % kPastN] < kPastBeat)
            return;
        h.t[h.head] = world_time;
        h.x[h.head] = p.body.pos.x;
        h.y[h.head] = p.body.pos.y;
        h.z[h.head] = p.body.pos.z;
        h.head = (h.head + 1) % kPastN;
        if (h.n < kPastN) ++h.n;
    }
    // The place of a body ago seconds back: the newest sample that is
    // old enough, read down from the newest, the oldest when none of
    // them is old enough, and the body itself when none is kept.
    Vec3 was_at(int id, double ago) const {
        const Player* p = players[static_cast<size_t>(id)].get();
        if (!p) return Vec3{};
        const PastPlace& h = past[id];
        if (h.n == 0) return p->body.pos;
        const double want = world_time - ago;
        for (int i = 1; i <= h.n; ++i) {
            const int k = (h.head + kPastN - i) % kPastN;
            if (h.t[k] <= want) return Vec3{h.x[k], h.y[k], h.z[k]};
        }
        const int oldest = (h.head + kPastN - h.n) % kPastN;
        return Vec3{h.x[oldest], h.y[oldest], h.z[oldest]};
    }
    // The bite of a Spitter is at range: one breath of acid in an arc,
    // aimed where its person stood lag seconds back, said at most one
    // every `every`. The blob is a grenade the clients fly the flight
    // of themselves, and the landing is the server word alone: a pool
    // painted green that melts, not a crater that blasts. The teeth
    // answer anything too close, as any walker.
    void spitter_tick(Player& p) {
        if (p.brute != kSpitter) return;
        // The probe of the breath: one line a beat while the debug speaks,
        // the one reading a quiet spitter wants first - the person the
        // spit would land nearest, the band the tuning asks of them, and
        // how long the next spit sits in its clock.
        if (map_probe && world_time - p.probe_at >= kProbeAgain) {
            p.probe_at = world_time;
            float pd2 = -1.0f;
            for (auto& qq : players) {
                if (!qq || qq->zombie || !qq->alive ||
                    qq->team != kPeople) continue;
                const float ndx = qq->body.pos.x - p.body.pos.x;
                const float ndy = qq->body.pos.y - p.body.pos.y;
                const float dd2 = ndx * ndx + ndy * ndy;
                if (pd2 < 0.0f || dd2 < pd2) pd2 = dd2;
            }
            const float wait = static_cast<float>(
                std::max(0.0, p.next_spit - world_time));
            if (pd2 >= 0.0f)
                log("spitter %d probe: person at %.1f blocks"
                    " (band %d to %d), next spit in %.1f s",
                    p.id, std::sqrt(pd2), spitter.min_range,
                    spitter.range, wait);
            else
                log("spitter %d probe: nobody living to spit at"
                    ", next spit in %.1f s",
                    p.id, wait);
        }
        // A second before the breath blows: bright green particles off
        // the head's origin, that a spit about to fly is said before it
        // leaves. Only a person in the band lights it - a spitter whose
        // window closes with nobody to spit at holds its breath and
        // says nothing.
        if (!p.spit_warned && world_time >= p.next_spit - 1.0 &&
            world_time < p.next_spit) {
            Player* tw = nullptr;
            float least = static_cast<float>(spitter.range);
            const float wx = p.body.pos.x, wy = p.body.pos.y;
            for (auto& qq : players) {
                if (!qq || qq->zombie || !qq->alive || qq->team != kPeople)
                    continue;
                const float dx = qq->body.pos.x - wx;
                const float dy = qq->body.pos.y - wy;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > least) continue;
                least = d;
                tw = qq.get();
            }
            if (tw && least >= static_cast<float>(spitter.min_range)) {
                p.spit_warned = true;
                const Vec3 at{wx, wy, p.body.pos.z + 0.5f};
                vapour(0, proto::SmokeStyle::Spray, at, 1,
               0x00, 0xFF, 0x00, 0xFF, 10, 3);
            }
        }
        if (world_time < p.next_spit) return;
        Player* t = nullptr;
        float best = static_cast<float>(spitter.range);
        const float px = p.body.pos.x, py = p.body.pos.y;
        for (auto& qq : players) {
            if (!qq || qq->zombie || !qq->alive || qq->team != kPeople)
                continue;
            const float dx = qq->body.pos.x - px;
            const float dy = qq->body.pos.y - py;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > best) continue;
            best = d;
            t = qq.get();
        }
        // The clock ran out with nobody in the band: the breath it
        // owed keeps its plume unlit, and the warning is rearmed that
        // the window to come lights its own.
        if (!t || best < static_cast<float>(spitter.min_range)) {
            p.spit_warned = false;
            return;
        }
        Vec3 aim = was_at(t->id, spitter.lag);
        // The arc is owed the ground the old place stood on, not the
        // head the ring reads: a burst solved onto the head pops in
        // the open air beside the ears - the pool paints nothing, and
        // only the splash lands. A still pair of feet was soaking in
        // a hurt with nothing on the ground to say why. The aim walks
        // down to the block those feet stood on, and the burst meets
        // it face to face at the end of the ride.
        const int stand_z = static_cast<int>(std::floor(aim.z + 2.25f));
        aim.z = static_cast<float>(stand_z + 1.5f);
        // The ride: the stand stands off now, so the arc is the show, slow
        // enough to watch and step aside from. The pace asks its own
        // seconds - thirty blocks at ten a second ride three of them,
        // and climb for it - and the bounds keep the ride on the map.
        const float pace = std::max(1.0f, static_cast<float>(spitter.speed));
        const float flight = std::min(kSpitMaxFlight,
                                      std::max(kSpitMinFlight, best / pace));
        const Vec3 src{px, py, p.body.pos.z + 0.4f};
        // A grenade rides the wire in the nade's own unit of pace, a
        // thirty-second of a block a second, and the sim the server
        // flies it through takes the same: the arc that owes R blocks
        // in T seconds says R over T of them, and the climb the pull
        // asks reads in the unit the gravity takes, its own half of
        // T. Thirty blocks over three seconds ride at ten blocks a
        // second and top about thirty-six, the ride the tuning owns.
        Vec3 vel;
        vel.x = (aim.x - src.x) / (flight * nade::kBlocksPerUnit);
        vel.y = (aim.y - src.y) / (flight * nade::kBlocksPerUnit);
        vel.z = (aim.z - src.z) / (flight * nade::kBlocksPerUnit) -
                0.5f * flight;
        // The landing is the touch, not a fuse: the bolt rides its
        // arc and bursts the tick it first meets the world, at the
        // step of the sim itself - the same step every client
        // walks. The face it meets holds the burst: the ground the
        // person stands beside, the wall stood on the way. What
        // never meets the world was aimed at a body mid leap, and
        // the drop below the burst still owes the paint its place.
        Vec3 at_pos = src;
        Vec3 at_vel = vel;
        float rode = flight;
        bool touched = false;
        for (int tick = 1; tick <= 240; ++tick) {
            const float left =
                flight - static_cast<float>(tick - 1) * nade::kTickDt;
            if (left <= 0.0f) break;
            const Vec3 from = at_pos;
            at_vel.z += nade::kTickDt;
            at_pos.x +=
                at_vel.x * nade::kTickDt * nade::kBlocksPerUnit;
            at_pos.y +=
                at_vel.y * nade::kTickDt * nade::kBlocksPerUnit;
            at_pos.z +=
                at_vel.z * nade::kTickDt * nade::kBlocksPerUnit;
            const int lx = static_cast<int>(std::floor(at_pos.x));
            const int ly = static_cast<int>(std::floor(at_pos.y));
            const int lz = static_cast<int>(std::floor(at_pos.z));
            if (phys_map.solid(lx, ly, lz)) {
                at_pos = from;
                rode = static_cast<float>(tick) * nade::kTickDt;
                touched = true;
                break;
            }
        }
        // A ride that never met the world was aimed at a body mid
        // leap, and no puddle holds in the open sky: the burst owes
        // its paint to the ground it flew over, so the column under
        // it answers and the top of its solid takes the landing the
        // arc just missed by a stride. Sky to the bottom of the map
        // keeps the pop where it fell, and the paint says nothing.
        if (!touched) {
            const int gx = static_cast<int>(std::floor(at_pos.x));
            const int gy = static_cast<int>(std::floor(at_pos.y));
            int gz = static_cast<int>(std::floor(at_pos.z)) + 1;
            while (gz < 64 && !phys_map.solid(gx, gy, gz))
                ++gz;
            if (gz < 64)
                at_pos.z = static_cast<float>(gz);
        }
        // The reading written on the landing: the distance it fell
        // from the aim it was owed. Close, the line held, and the
        // stand stands; past kSpitOffShut names the wall on the way,
        // and the road is owed again until another line rides.
        const float ox = at_pos.x - aim.x, oy = at_pos.y - aim.y;
        const float oz = at_pos.z - aim.z;
        p.spit_off = std::sqrt(ox * ox + oy * oy + oz * oz);
        Grenade g{p.id, p.team, at_pos,
                  world_time + static_cast<double>(rode)};
        g.acid = true;
        grenades.push_back(g);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::GrenadePacket));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_f32(rode);
        bw.write_vec(src);
        bw.write_vec(vel);
        broadcast(bw, false);
        if (map_probe) {
            log("spitter %d spat, rode %.2f, landing %.1f, %.1f, %.1f",
                p.id, rode, at_pos.x, at_pos.y, at_pos.z);
            if (p.spit_off > kSpitOffShut)
                log("spitter %d breath shut, %.1f afoot of its aim -"
                    " the walk takes over until a line rides again",
                    p.id, p.spit_off);
        }
        p.next_spit = world_time + spitter.every;
        p.spit_warned = false;
    }
    // Where a wave comes from, while it comes: the same ping the tutorial's
    // goals use, re-sent for a corner every few seconds it still streams -
    // one marker at the pack's spawn, held long enough to be walked to,
    // renewed so a client mid-load still sees it once it lands. A client
    // keeps a ping a player-id, so a front's ping goes out under an id of
    // its own - the ten fronts' ids, the corners counted off the tenth
    // player-slot - or a second front's marker would wipe the first's
    // off the map. The ping wears its front's own colour, the one its
    // trail sleeps on too: the marker names the front that its vapour
    // pours out of.
    void ping_wave_spawn(const horde::SpawnAt& s) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
        bw.write_u8(1);
        bw.write_u8(static_cast<uint8_t>(245 + s.corner));   // this front's own ping id
        bw.write_vec(Vec3{static_cast<float>(s.x) + 0.5f, static_cast<float>(s.y) + 0.5f,
                          static_cast<float>(s.z) - 1.0f});
        bw.write_f32(20.0f);
        bw.write_u8(1 | 2);
        write_color(bw, kFrontColour[s.corner]);
        bw.write_u8(0);
        const char* text = "The wave comes from here";
        for (const char* c = text; *c; ++c) bw.write_u8(static_cast<uint8_t>(*c));
        for (auto& o : players)
            if (o && o->peer && !o->loading && o->ext_teamplay && o->playing()) send(*o, bw);
    }

    // The ways in, one a front: the reading the vapour stands on - the
    // traced route of every front the coming wave walks on, each such
    // reading from its front's own place. While the wave walks, a
    // front's reading begins at the feet of that front's own pack head -
    // its leader that has come nearest the core, on the ground it
    // stands on: with every beat the trail is re-said from where the
    // pack has got to, the part behind said no more, and the vapour the
    // leader has already crossed thins out on its own - the trail looks
    // less like a plan drawn ahead of time and more like a thing
    // pouring out of the walker's own feet, live. A front whose pack
    // has not raised a walker yet - or has none left standing - is read
    // from its anchor instead, so the way in is said whole while it
    // waits; and whichever end it is read from, the sealed way in goes
    // to the spade's own reading, painted the same, that the defenders
    // see where the fences will be gnawed. Nothing when no front
    // anchors.
    struct FrontRead {
        int corner;                            // the site this way comes in from: its colour
        std::vector<horde::RouteCell> cells;    // the way itself, from the walking end to the doorstep
    };
    bool route_reading(std::vector<FrontRead>& fronts) {
        fronts.clear();
        if (!horde) return false;
        // The wave whose ways in are watched: the one walking now, or the
        // next one when the world waits for it.
        const bool walking = horde->phase() == horde::Phase::Wave;
        const int wave = walking ? horde->wave() : horde->wave() + 1;
        const int nf = horde->wave_fronts(wave);
        for (int r = 0; r < nf; ++r) {
            const int corner = horde->ranked_corner(r);
            FrontRead fr;
            fr.corner = corner;
            int sx = 0, sy = 0;
            bool have_start = false;
            if (walking) {
                // This pack's own head: the nearest to the core of the
                // leaders its corner raised, on the ground it stands on.
                const Player* head = nullptr;
                int head_fd = -1;
                for (auto& q : players) {
                    if (!q || !q->zombie || !q->alive || q->ahead_id >= 0) continue;
                    if (q->spawn_corner != corner) continue;
                    if (q->body.airborne) continue;    // no print is set in the air
                    const int qx = static_cast<int>(std::floor(q->body.pos.x));
                    const int qy = static_cast<int>(std::floor(q->body.pos.y));
                    const int qz = static_cast<int>(std::floor(q->body.pos.z + 2.25f));
                    const int fd = horde->distance(qx, qy, qz);
                    if (fd < 0 || (head != nullptr && fd >= head_fd)) continue;
                    head = q.get();
                    head_fd = fd;
                }
                if (head != nullptr) {
                    sx = static_cast<int>(std::floor(head->body.pos.x));
                    sy = static_cast<int>(std::floor(head->body.pos.y));
                    have_start = true;
                }
            }
            if (!have_start && !horde->corner_anchor(corner, sx, sy)) continue;
            if (!horde->trace_route(sx, sy, fr.cells) && horde_cfg.dig)
                horde->trace_dig(sx, sy, fr.cells);   // the sealed way in: over the walls it will take down
            // The doorstep trimmed: the core's own column is where the
            // way ends, not a tread of it. A line drawn onto the
            // pillar's own edge wavered with every re-say of the
            // marker it hung from, a jitter at the very door; the way
            // in stops one block short instead, on the ground the
            // walk itself stands on there - and the door it names is
            // read as well from a steady line as from a wavering one.
            while (!fr.cells.empty() &&
                   fr.cells.back().x == horde_cfg.core_x &&
                   fr.cells.back().y == horde_cfg.core_y)
                fr.cells.pop_back();
            if (fr.cells.empty()) continue;
            fronts.push_back(std::move(fr));
        }
        return !fronts.empty();
    }

    // The ways in, said as heavy vapour for the clients that know no
    // other word for them - a bed of it laid along each front's reading, one bed every
    // couple of treads so a trail reads unbroken from where it is
    // walked to the core's doorstep, over the dressed ground and across
    // the world's bed alike - the legs walk the bedrock as freely as any
    // ground, and no wave crosses unwarned. A client that claimed the
    // smoke's second version is handed the way as a path instead, drawn
    // by itself, and owes these beds nothing. Each bed sits at its
    // tread's floor, a cloud hardly a block across that pools rather
    // than climbs, kept coming for its whole stay so the bed stays
    // fresh. Every front owns a band of ids of its own - a bed's id is
    // its place in its front's band, the bands stacked one a front - so
    // a resend lays its vapour over the old in place, and a reading
    // that no longer walks a place simply stops saying it: the vapour
    // hung there thins out on its own, no clearing sweep to keep a
    // stale way in on the air. The bands are the client's own budget of
    // bed ids split across the fronts the blink says: one front on the
    // ground takes the whole of it and its trail runs its full length,
    // while a ten-front wave takes it ten ways and each trail holds
    // only so many beds - and either way a trail trimmed short keeps
    // the end the defence reads, the treads by the core, dropping the
    // far end; what the trail drops, its ping at the site still says.
    // Within a band the ids count from the doorstep outwards, so the
    // beds nearest the core keep their ids while a walking trail grows
    // shorter behind its advancing head. A bed is laid a tread, one
    // tread one cloud - the full pour, twice the spread: a trail that
    // reads as one unbroken ribbon of its colour, not beads on a
    // string. A trail wears its front's own colour, held solid the
    // whole way: the colour names the front, its ping the same; the one
    // marker a defender cannot mistake for smoke of their own.
    // Sound it carries none: a line of hisses is a racket, not a marker.
    void smoke_route(const std::vector<FrontRead>& fronts) {
        if (fronts.empty()) return;
        constexpr int kStride = 1;          // a bed a tread: the full pour, twice the spread
        // forty beds of it held off the top: the acid's ten, the
        // healer's thirty
        constexpr int kBeds = 214;
        constexpr uint16_t kStay = 7;       // 0.7 s it keeps coming: the heavier pour, one blink's worth and a little over
        constexpr uint8_t kThins = 2;       // 0.2 s the last of it takes to go: it leaves like it settles, slowly
        const int per = kBeds / static_cast<int>(fronts.size());   // the budget, split across the fronts the blink says
        for (size_t f = 0; f < fronts.size(); ++f) {
            const FrontRead& fr = fronts[f];
            const uint32_t rgb = kFrontColour[fr.corner];
            const uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFFu),
                          g = static_cast<uint8_t>((rgb >> 8) & 0xFFu),
                          b = static_cast<uint8_t>(rgb & 0xFFu);
            const int band = static_cast<int>(f) * per;
            const std::vector<horde::RouteCell>& cells = fr.cells;
            int bed = 0;
            // The doorstep's tread first, then one outwards - the ids of
            // the band count from the core, so the beds the defence
            // watches keep their ids blink to blink.
            for (size_t k = 0; k < cells.size(); ++k) {
                if (k % kStride != 0 && k + 1 != cells.size()) continue;  // the last tread is said whatever rhythm it falls on
                if (bed >= per) break;
                const horde::RouteCell& c = cells[cells.size() - 1 - k];
                ++bed;
                ByteWriter bw;
                proto::write_smoke(bw, static_cast<uint8_t>(band + bed),
                                   proto::SmokeStyle::Ground,
                                   Vec3{static_cast<float>(c.x) + 0.5f,
                                        static_cast<float>(c.y) + 0.5f,
                                        static_cast<float>(c.z) - 0.35f},
                                   static_cast<uint8_t>(1), r, g, b, 0xE8,
                                   kStay, kThins, static_cast<uint8_t>(0));
                for (auto& o : players)
                    if (o && o->peer && !o->loading && o->ext_smoke && !o->ext_trail &&
                        o->playing()) send(*o, bw);
            }
        }
    }

    // Vapour on a bed id: the spit's landing wears it twice - a cloud
    // of green that breaks open where it struck and thins inside a
    // blink, and a bed of green held over its paint for the whole of
    // the melt - and the healer lays its pale blue at the feet of the
    // dead it works among. Neither hisses, and both ride bands held
    // off the top of the client's own bed budget, said to every smoke
    // the client claimed: a way read as paths wears these all the same.
    void vapour(const uint8_t id, const proto::SmokeStyle style,
                const Vec3& at, const uint8_t radius, const uint8_t red,
                const uint8_t green, const uint8_t blue,
                const uint8_t opacity, const uint16_t stay,
                const uint8_t fade) {
        ByteWriter bw;
        proto::write_smoke(bw, id, style, at, radius, red, green, blue,
                           opacity, stay, fade, static_cast<uint8_t>(0));
        for (auto& o : players)
            if (o && o->peer && !o->loading && o->ext_smoke &&
                o->playing()) send(*o, bw);
    }

    // A reading's turns, and nothing else: a way in is said as the
    // cells its trace bends on, cut into stays a byte of delta can
    // reach, the level held where the ground holds it - the level a
    // span keeps is the drift it is allowed, and a step that leaves it
    // makes a point of its own, so the line the client draws between
    // the points never swings off the way the walk walks. The turns of
    // a real route are a couple dozen; a way this long is said in a
    // hundred bytes where its beds were twenty-five packets the blink.
    void compress_waypoints(const std::vector<horde::RouteCell>& cells,
                            std::vector<proto::PathPoint>& out) const {
        out.clear();
        if (cells.empty()) return;
        constexpr int kDrift = 2;   // the level a span holds, in blocks
        constexpr int kStay = 100;  // the longest run a byte of delta says, kept shy of it
        int run = 0;
        auto push = [&](const horde::RouteCell& c) {
            proto::PathPoint p;
            p.x = static_cast<int16_t>(c.x);
            p.y = static_cast<int16_t>(c.y);
            p.z = static_cast<uint8_t>(c.z);
            out.push_back(p);
            run = 0;
        };
        push(cells.front());
        for (size_t k = 1; k + 1 < cells.size(); ++k) {
            const horde::RouteCell& prev = cells[k - 1];
            const horde::RouteCell& here = cells[k];
            const horde::RouteCell& next = cells[k + 1];
            ++run;
            const bool bend = here.x - prev.x != next.x - here.x ||
                              here.y - prev.y != next.y - here.y;
            const bool left_level = here.z > out.back().z + kDrift ||
                                    here.z + kDrift < out.back().z;
            if ((bend || left_level || run >= kStay) && out.size() < 255)
                push(here);
        }
        // The doorstep itself, whatever rhythm it falls on: the line ends
        // where the walk does, not a stay short of it.
        if (out.size() < 255) push(cells.back());
    }

    // The ways in, said as paths - the word the smoke's second version
    // knows: one packet a front, the turns of its way in that front's
    // own colour, drawn whole by the client that reads it. A reading
    // that has not changed is not said again until the whole set's
    // refresh comes due, so a client's screen is not fed its own
    // history; one that has changed is said at once, so the line
    // follows the leader that walks it. The fronts that fell out of
    // the reading - their pack gone down, their corner outranked - are
    // the ones owed an empty path, that the client takes their line
    // off the air rather than draw a way in nothing walks any more.
    void trail_route(const std::vector<FrontRead>& fronts) {
        const bool again = world_time >= trail_resend_next;
        if (again) trail_resend_next = world_time + kTrailResend;
        bool seen[horde::Config::kSpawnSites] = {};
        for (const FrontRead& fr : fronts) {
            const int c = fr.corner;
            if (c < 0 || c >= horde::Config::kSpawnSites) continue;
            seen[c] = true;
            std::vector<proto::PathPoint> pts;
            compress_waypoints(fr.cells, pts);
            const bool unchanged =
                trail_sent_any[c] && trail_sent[c].size() == pts.size() &&
                std::equal(pts.begin(), pts.end(), trail_sent[c].begin(),
                           [](const proto::PathPoint& a, const proto::PathPoint& b) {
                               return a.x == b.x && a.y == b.y && a.z == b.z;
                           });
            trail_sent_any[c] = true;
            trail_sent[c] = std::move(pts);
            if (unchanged && !again) continue;
            ByteWriter bw;
            const uint32_t rgb = kFrontColour[c];
            proto::write_path(bw, static_cast<uint8_t>(c),
                              static_cast<uint8_t>((rgb >> 16) & 0xFFu),
                              static_cast<uint8_t>((rgb >> 8) & 0xFFu),
                              static_cast<uint8_t>(rgb & 0xFFu),
                              trail_sent[c]);
            for (auto& o : players)
                if (o && o->peer && !o->loading && o->ext_trail && o->playing()) send(*o, bw);
        }
        for (int c = 0; c < horde::Config::kSpawnSites; ++c) {
            if (seen[c] || !trail_sent_any[c]) continue;
            ByteWriter bw;
            proto::write_path(bw, static_cast<uint8_t>(c), 0, 0, 0,
                              std::vector<proto::PathPoint>{});
            for (auto& o : players)
                if (o && o->peer && !o->loading && o->ext_trail && o->playing()) send(*o, bw);
            trail_sent_any[c] = false;
            trail_sent[c].clear();
        }
    }

    // Every way in's line taken off the air at once: victory and defeat
    // alike leave no way in worth drawing, and a path, unlike a bed of
    // vapour, does not burn out on its own - it is owed its clear.
    void clear_trails() {
        for (int c = 0; c < horde::Config::kSpawnSites; ++c) {
            if (!trail_sent_any[c]) continue;
            ByteWriter bw;
            proto::write_path(bw, static_cast<uint8_t>(c), 0, 0, 0,
                              std::vector<proto::PathPoint>{});
            for (auto& o : players)
                if (o && o->peer && !o->loading && o->ext_trail && o->playing()) send(*o, bw);
            trail_sent_any[c] = false;
            trail_sent[c].clear();
        }
    }

    // The test bench's broom: every walker of the standing wave put down,
    // the core mended whole, and a defeat called off with it - the world
    // returns to its waiting, and the next wave comes when the wait is
    // over. The walkers fall where they stand, their slots go back, and
    // the wave the clock had coming is owed nobody: the counting starts
    // again at the next one.
    void clear_wave() {
        const bool defeated = horde->phase() == horde::Phase::Defeat;
        int down = 0;
        for (auto& qq : players)
            if (qq && qq->zombie && qq->alive) {
                kill(*qq, nullptr, kFallKill);
                ++down;
            }
        horde->clear_wave(world_time);
        if (tower_defense_mode() && !cfg.map_file.empty()) build_core_column();
        char text[96];
        std::snprintf(text, sizeof text, "%d down; the core mends to %d",
                      down, horde->core_hp());
        system_chat(text);
        if (defeated)
            system_chat("The defeat is called off; the next wave is coming.");
    }

    // The specials a wave stands, drawn at its start from the tuning
    // tables: one or more brutes queue here and ride the due treads of
    // the wave, one by one, beside the grunts. The two bullies of the
    // count, the tank and the healer, stand apart on the waves before
    // the wave the tuning names: below it the healer keeps the wave
    // the tank would have walked, and from it on they walk as one.
    void plan_specials() {
        special_due.clear();
        const int wave = horde->wave();
        if (horde->placed() == 0)
            return;   // no way in: a wave that cannot walk holds no brutes
        for (int k = 0; k < kSpecialCount; ++k) {
            const SpecialTuning& s = specials[static_cast<size_t>(k)];
            if (k == kBoomer)
                continue;   // it comes with its own rounds, never a plan
            if (wave < s.first_wave) continue;
            int want = 0;
            if (s.chance > 0) {
                // The chance kinds: a percentile rolled a place a wave,
                // the percentile itself climbing a notch a wave, and
                // the rolls growing by the tuning - two chances a wave
                // from the rolls wave, one more each rolls_every after.
                const int pct = std::min(100, s.chance +
                    s.chance_grow * (wave - s.first_wave));
                int rolls = 1;
                if (s.rolls_wave > 0 && s.rolls_every > 0 &&
                    wave >= s.rolls_wave)
                    rolls = 2 + (wave - s.rolls_wave) / s.rolls_every;
                for (int roll = 0; roll < rolls; ++roll)
                    if (std::uniform_int_distribution<int>(0, 99)(rng) <
                        pct)
                        ++want;
            } else {
                // The count kinds: one on its first wave and each
                // every_wave after it, and every wave from the grow_wave
                // on, one more standing each grow_every - counted from
                // the wave the growth begins - held at the cap the
                // tuning says, when it says one at all.
                const bool stands =
                    (s.grow_wave > 0 && wave >= s.grow_wave) ||
                    s.every_wave <= 0 ||
                    (wave - s.first_wave) % s.every_wave == 0;
                if (!stands) continue;
                want = 1;
                if (s.grow_wave > 0 && wave >= s.grow_wave &&
                    s.grow_every > 0) {
                    // The climb counts its steps from the wave the
                    // growth itself begins: one more each grow_every
                    // of the way, and a grows_to of zero says the
                    // climb never tops out.
                    want = 1 + (wave - s.grow_wave) / s.grow_every;
                    if (s.grows_to > 0 && want > s.grows_to)
                        want = s.grows_to;
                }
            }
            // The healer holds the tank's waves: below the wave the
            // tuning names, whenever the healer keeps a wave the tank
            // walks none, for its place stands taken.
            if (k == kHealer && want > 0 &&
                wave < healer_tank_together) {
                for (size_t i = 0; i < special_due.size();) {
                    if (special_due[i] == kTank)
                        special_due.erase(
                            special_due.begin() + static_cast<long>(i));
                    else
                        ++i;
                }
            }
            for (int j = 0; j < want; ++j)
                special_due.push_back(k);
        }
    }

    // The rounds the boomers own: a wave in every boomer_wave_every
    // from the first, built of their kind alone, and thinned to the
    // scale the tuning stands for what it walks. The word is given
    // between the waves, while the coming one is still on the cloth.
    bool boomer_wave(int wave) const {
        return tower_defense_mode() && boomer_wave_every > 0 &&
               wave >= boomer_wave_first &&
               (wave - boomer_wave_first) % boomer_wave_every == 0;
    }

    // The wave's announcements, its spawns, and its dead slots given back.
    void horde_tick() {
        if (tower_defense_mode() &&
            horde->phase() != horde::Phase::Wave)
            horde->set_wave_scale(boomer_wave(horde->wave() + 1)
                                      ? boomer_wave_scale
                                      : 100);
        horde->tick(world_time);
        // The focus of the hunt: the person the hunters hold nearest most
        // between them - one name for the one field every hunter walks,
        // read fresh every tick. Nobody hunting or nobody living means
        // no name, and the field stands down and the hunters stand.
        // The Boomer holds no share of the hunt but its own short one:
        // it votes only inside the reach of its chase, and a pack with
        // no hunter left still finds a name for its boomer to run at.
        hunt_focus_ = nullptr;
        std::vector<int> votes(players.size(), 0);
        for (auto& z : players) {
            if (!z || !z->alive || !z->zombie) continue;
            if (z->brute != kSpitter && z->brute != kBoomer &&
                (z->brute != kLeaper || !z->hunts)) continue;
            Player* t = nullptr;
            float bd = -1.0f;
            for (auto& qq : players) {
                if (!qq || qq->zombie || !qq->alive || qq->spectator() ||
                    qq->team != kPeople) continue;
                const float ddx = qq->body.pos.x - z->body.pos.x;
                const float ddy = qq->body.pos.y - z->body.pos.y;
                const float dd2 = ddx * ddx + ddy * ddy;
                if (t == nullptr || dd2 < bd) {
                    bd = dd2;
                    t = qq.get();
                }
            }
            if (t != nullptr &&
                (z->brute != kBoomer ||
                 bd <= static_cast<float>(boomer_chase * boomer_chase)))
                ++votes[static_cast<size_t>(t->id)];
        }
        int focus_votes = 0;
        for (size_t i = 0; i < players.size(); ++i) {
            if (votes[i] > focus_votes) {
                focus_votes = votes[i];
                hunt_focus_ = players[i].get();
            }
        }
        if (hunt_focus_ != nullptr)
            horde->hunt_follow(
                static_cast<int>(std::floor(hunt_focus_->body.pos.x)),
                static_cast<int>(std::floor(hunt_focus_->body.pos.y)),
                world_time);
        else
            horde->hunt_follow(-1, -1, world_time);
        // The safety net: the rare wave that neither falls nor wins
        // would hold the game hostage - once the clock the wave was
        // born holding runs out, the net takes it here, and the clear
        // speaks for the rest.
        if (horde->safety_net_fired()) {
            system_chat("The safety net takes the wave: it stood too long.");
            clear_wave();
        }
        const horde::Phase ph = horde->phase();
        if (ph != horde_told) {
            horde_told = ph;
            switch (ph) {
                case horde::Phase::Wave:
                    if (horde->wave() > horde_wave_told) {
                        horde_wave_told = horde->wave();
                        // New packs, their pings due right away, and the
                        // way in's vapour laid before the first tread is walked.
                        for (int k = 0; k < horde::Config::kSpawnSites; ++k) {
                            pack_tail[k] = -1;
                            corner_ping_next[k] = 0.0;
                        }
                        route_smoke_next = 0.0;
                        plan_specials();
                        char text[96];
                        if (horde->placed() == 0) {
                            // The walk priced every corner and none of them
                            // stands on it: without a spade there is no way
                            // in, and saying so beats a wave that clears
                            // itself in silence.
                            std::snprintf(text, sizeof text,
                                          "Wave %d: no way in the walk can take",
                                          horde->wave());
                        } else if (boomer_wave(horde->wave())) {
                            std::snprintf(text, sizeof text,
                                          "Wave %d: %d boomers coming",
                                          horde->wave(),
                                          static_cast<int>(horde->placed()));
                        } else {
                            std::snprintf(text, sizeof text, "Wave %d: %d zombies coming",
                                          horde->wave(),
                                          static_cast<int>(horde->placed()));
                        }
                        system_chat(text);
                        note_score_wave(horde->wave());
                    }
                    break;
                case horde::Phase::Intermission: {
                    route_smoke_next = 0.0;  // the next wave's way in, said again at once
                    char text[96];
                    std::snprintf(text, sizeof text, "Wave cleared; the core mends to %d",
                                  horde->core_hp());
                    system_chat(text);
                    // The pay of the wave: a bonus that climbs with it,
                    // every hand still in the world the richer for it.
                    const int bonus = economy.wave_bonus + horde->wave();
                    for (auto& o : players) {
                        if (!o || o->zombie || o->aim_bot || !o->playing())
                            continue;
                        o->zc += bonus;
                        system_chat("Wave bonus: +" + std::to_string(bonus) +
                                    " Zedcreds; " +
                                    std::to_string(o->zc) + " in hand.", o->id);
                    }
                    break;
                }
                case horde::Phase::Victory:
                    system_chat("Every wave is down. Victory!");
                    trail_resend_next = 0.0;   // the last ways in, taken back at once
                    break;
                case horde::Phase::Defeat:
                    system_chat("The core has fallen. Defeat.");
                    if (score_best > 0) {
                        char page[96];
                        std::snprintf(page, sizeof page,
                                      "The record book holds wave %d"
                                      " on this map.",
                                      score_best);
                        system_chat(page);
                    }
                    trail_resend_next = 0.0;   // and on defeat no less: the war is over
                    break;
                case horde::Phase::Prep:
                    route_smoke_next = 0.0;   // a restarted world says its way in at once
                    break;
            }
        }
        // The way in said, every blink: the trail on the ground is the one
        // marker of where the dead walk, so while the world waits for
        // them and while they walk it alike, the reading is traced and
        // said again each beat - as beds of vapour to the clients that
        // still read them, as paths to those that draw their own lines,
        // cheaper the dozen and whole the length. The world is built
        // on under the reading's feet, and what is said follows the true
        // way in, over the dressed ground and across the bedrock the
        // legs walk as freely as any ground.
        const bool waiting = ph == horde::Phase::Prep || ph == horde::Phase::Intermission;
        if (waiting || ph == horde::Phase::Wave) {
            if (world_time >= route_smoke_next) {
                route_smoke_next = world_time + kRouteBlink;
                // One reading, said two ways: the beds of vapour for the
                // clients that know no other word, the paths for those
                // that draw their own - the trace is walked once.
                std::vector<FrontRead> fronts;
                route_reading(fronts);
                smoke_route(fronts);
                trail_route(fronts);
            }
        } else {
            // No way in is on the air any more: victory and defeat hold
            // no trails either, and what was said must be taken back.
            if (world_time >= trail_resend_next) {
                trail_resend_next = world_time + kTrailResend;
                clear_trails();
            }
        }
        std::vector<horde::SpawnAt> due;
        while (horde->take_due(due, world_time)) {
            for (const horde::SpawnAt& s : due) {
                // While the point streams, its spawn is pinged again every
                // so often: the marker survives a client's map load.
                const int c = s.corner;
                if (world_time >= corner_ping_next[c]) {
                    corner_ping_next[c] = world_time + 30.0;
                    ping_wave_spawn(s);
                }
                // A brute of the queue stands for a grunt: it takes
                // this tread itself, the place a grunt was to walk -
                // a walker of the wave either way, and never a wave
                // grown fat by its own specials.
                if (!special_due.empty()) {
                    add_zombie(s, special_due.front());
                    special_due.erase(special_due.begin());
                } else if (boomer_wave(horde->wave())) {
                    // The round is the boomer's own: each tread a
                    // bladder, its dead thinned to its own scale.
                    add_zombie(s, kBoomer);
                } else {
                    add_zombie(s);
                }
            }
        }
        free_dead_zombies();
    }

    // Whether `who` is looking at `t`: their view's ray passing within a body's
    // half width of it, from the eye down to the knees.
    static bool aimed_at(const Player& who, const Player& t) {
        const Vec3& o = who.body.pos;
        const Vec3& d = who.body.ori;
        for (int i = 0; i <= 4; ++i) {
            const Vec3 c{t.body.pos.x, t.body.pos.y, t.body.pos.z + 0.45f * static_cast<float>(i)};
            const Vec3 v{c.x - o.x, c.y - o.y, c.z - o.z};
            const float along = v.x * d.x + v.y * d.y + v.z * d.z;
            if (along <= 0.0f) continue;
            const Vec3 q{v.x - d.x * along, v.y - d.y * along, v.z - d.z * along};
            if (q.x * q.x + q.y * q.y + q.z * q.z < 0.6f * 0.6f) return true;
        }
        return false;
    }
    void reset_run() {
        const int run = aim.run + 1;
        aim = training::AimStatus{};
        aim.scenario = cfg.scenario;
        aim.run = run;
        aim.phase = training::AimPhase::Waiting;
        for (auto& pp : players) {
            if (!pp || !pp->aim_bot) continue;
            place_aim_bot(*pp, false);
            if (pp->alive) {
                pp->alive = false;   // back at a fresh spot at once, without a kill
                spawn(*pp);
            } else {
                pp->respawn_at = world_time;
            }
        }
    }
    void aim_tick(float dt) {
        using training::AimPhase;
        // The client's restart, or a run nobody is left to play.
        bool restart = false;
        if (cfg.aim) {
            std::lock_guard<std::mutex> lk(cfg.aim->mutex);
            restart = cfg.aim->restart;
            cfg.aim->restart = false;
        }
        Player* who = person();
        if (restart) {
            reset_run();
            if (who && who->alive) spawn(*who);  // back on the spot
        }
        if (!who && aim.phase != AimPhase::Waiting) reset_run();
        switch (aim.phase) {
            case AimPhase::Waiting:
                if (who && who->alive) {
                    aim.phase = AimPhase::Countdown;
                    aim.weapon = who->weapon;
                    phase_until = world_time + training::kCountdownSeconds;
                }
                break;
            case AimPhase::Countdown:
                aim.countdown = static_cast<float>(std::max(0.0, phase_until - world_time));
                if (world_time >= phase_until) {
                    aim.phase = AimPhase::Running;
                    phase_until = world_time + training::kRunSeconds;
                    shots_base = who ? who->gun.fired_by(world_time) : 0;
                    // The targets' clocks start with the run.
                    for (auto& pp : players)
                        if (pp && pp->aim_bot) pp->appeared_at = world_time;
                }
                break;
            case AimPhase::Running:
                aim.elapsed += dt;
                aim.time_left = static_cast<float>(std::max(0.0, phase_until - world_time));
                if (who) {
                    aim.shots = std::max(0, who->gun.fired_by(world_time) - shots_base);
                    if (who->alive && cfg.scenario == training::Scenario::Tracking) {
                        for (auto& pp : players)
                            if (pp && pp->aim_bot && pp->alive && aimed_at(*who, *pp)) {
                                aim.on_target += dt;
                                break;
                            }
                    }
                }
                if (world_time >= phase_until) {
                    aim.phase = AimPhase::Finished;
                    aim.time_left = 0.0f;
                    log("aim run %d over: %d kills, %d of %d shots hit", aim.run, aim.kills, aim.hits, aim.shots);
                }
                break;
            case AimPhase::Finished: break;
        }
        if (cfg.aim) {
            std::lock_guard<std::mutex> lk(cfg.aim->mutex);
            cfg.aim->status = aim;
        }
    }

    // --- Teamplay from the server: pings and marks --------------------------
    void send_ping(const Vec3& at, float duration, uint32_t rgb, const std::string& reason) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
        bw.write_u8(1);                       // Ping
        bw.write_u8(255);                     // the server's own
        bw.write_vec(at);
        bw.write_f32(duration);
        bw.write_u8(1 | 2);                   // in the world and on the minimap
        write_color(bw, rgb);
        bw.write_u8(0);
        for (char ch : reason) bw.write_u8(static_cast<uint8_t>(ch));
        for (auto& o : players)
            if (o && o->peer && !o->loading && o->ext_teamplay) send(*o, bw);
    }
    // The flare each brute walks under, the colour of its own skin:
    // green, blue, purple, red, yellow, dark orange.
    static uint32_t brute_flare(int brute) {
        switch (brute) {
            case kSpitter: return 0x20FF60;
            case kTank: return 0x3080FF;
            case kHealer: return 0xA040FF;
            case kLeaper: return 0xFF3030;
            case kBreaker: return 0xFFD020;
            case kBoomer: return kBoomOrange;
            default: return 0x20FF60;
        }
    }
    void send_mark(const Player& target, uint32_t rgb = 0x20FF60,
                   uint8_t flags = 0) {
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
        bw.write_u8(2);                       // ESP Mark
        bw.write_u8(static_cast<uint8_t>(target.id));
        bw.write_f32(std::numeric_limits<float>::infinity());
        bw.write_u8(1);                       // the body's outline
        bw.write_u8(flags);
        write_color(bw, rgb);
        bw.write_u8(0);
        for (auto& o : players)
            if (o && o->peer && !o->loading && o->ext_teamplay) send(*o, bw);
    }

    // --- the tutorial's pings: where the next step is -------------------------
    void tutorial_tick() {
        if (world_time < next_tutorial_check) return;
        next_tutorial_check = world_time + 0.25;
        using namespace training;
        for (auto& pp : players) {
            if (!pp || !pp->peer || !pp->playing() || !pp->ext_teamplay) continue;
            Player& p = *pp;
            for (int st = 0; st < StepCount; ++st)
                if (p.alive && step_reached(st, p.body.pos.x, p.kills)) p.steps_done |= 1 << st;
            int next = -1;
            for (int st = 0; st < StepCount && next < 0; ++st)
                if (!(p.steps_done & (1 << st))) next = st;
            if (next == p.pinged_step) continue;
            p.pinged_step = next;
            struct Goal { float x, z; const char* what; };
            const float up = static_cast<float>(kFloorZ - kLedgeRise);
            const Goal goals[StepCount] = {
              {static_cast<float>(kRange.x0 + 6), kFloorZ - 1.0f, "Walk to the range"},
              {static_cast<float>(kLaneX[0]), kFloorZ - 2.5f, "Shoot 3 targets"},
              {static_cast<float>(kDigWall.x0), kFloorZ - 2.0f, "Dig through"},
              {static_cast<float>(kLedgeX + 1), up - 1.0f, "Build up here"},
              {static_cast<float>(kPlug.x0), up - 1.5f, "Blow it open"},
              {static_cast<float>(kFinish.x0 + 6), up - 1.0f, "Finish"},
            };
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
            bw.write_u8(1);
            bw.write_u8(255);
            if (next < 0) {
                bw.write_vec(Vec3{0, 0, 0});
                bw.write_f32(0.0f);           // all done: no more ping
            } else {
                bw.write_vec(Vec3{goals[next].x, 256.5f, goals[next].z});
                bw.write_f32(std::numeric_limits<float>::infinity());
            }
            bw.write_u8(1 | 2);
            write_color(bw, 0xFFC830);
            bw.write_u8(0);
            if (next >= 0)
                for (const char* c = goals[next].what; *c; ++c) bw.write_u8(static_cast<uint8_t>(*c));
            send(p, bw);
        }
    }

    // --- the world --------------------------------------------------------
    void world_tick() {
        const float dt = static_cast<float>(kTick);
        for (auto& pp : players) {
            if (!pp) continue;
            Player& p = *pp;
            if (p.gun.update(world_time)) {
                // Tower Defense never runs out of rounds: the reserve is
                // topped back up behind every answer, so the count the
                // packet carries never falls.
                if (tower_defense_mode()) p.gun.restock();
                send_gun_state(p);
            }
            if (!p.alive) {
                if (p.respawn_at >= 0 && world_time >= p.respawn_at) spawn(p);
                continue;
            }
            if (!p.playing()) continue;
            if (p.lane >= 0) steer_target(p);
            if (p.aim_bot) steer_aim_bot(p);
            if (p.zombie) {
                steer_zombie(p);
                // What a player's own client would tell everybody, the horde
                // tells for it: the clients animate its walk by it.
                const uint8_t f = (p.input.mf ? 1u : 0u) | (p.input.mb ? 2u : 0u) |
                                  (p.input.ml ? 4u : 0u) | (p.input.mr ? 8u : 0u) |
                                  (p.input.jump ? 16u : 0u);
                if (f != p.net_input) {
                    p.net_input = f;
                    ByteWriter bw;
                    bw.write_u8(static_cast<uint8_t>(PacketId::InputData));
                    bw.write_u8(static_cast<uint8_t>(p.id));
                    bw.write_u8(static_cast<uint8_t>(f));
                    broadcast(bw, false);
                }
                if (p.swing_shown && world_time >= p.swing_until) {
                    p.swing_shown = false;
                    ByteWriter bw;
                    bw.write_u8(static_cast<uint8_t>(PacketId::WeaponInput));
                    bw.write_u8(static_cast<uint8_t>(p.id));
                    bw.write_u8(0);
                    broadcast(bw, false);
                }
            }
            keep_stocked(p);
            if (tower_defense_mode() && p.peer && p.alive && !p.zombie)
                remember_place(p);
            // A leaper in flight owes the sky its own hand: the arc is
            // steered here, before the world steps the body.
            if (p.zombie && (p.leap_stage == 1 || p.leap_stage == 3))
                leap_fly(p);
            p.input.holding_gun = p.tool == kToolWeapon;
            p.input.secondary_fire = p.secondary;
            // Light feet: every step of the store's perk is a tenth of the
            // pace the same physics runs. The client keeps its own measure
            // and is never told, so the two must agree - which they do,
            // because the numbers come from the same table.
            const float move_scale =
                1.0f + 0.1f * static_cast<float>(p.speed_bonus_pct);
            p.input.move_scale = move_scale;
            const physics::StepResult r = physics::step(phys_map, p.body, p.input, dt);
            p.input.jump = false;
            // The fallen have no fear of falling: a cliff on the map's rim is
            // a walk down, not a death.
            if (r.fall_damage > 0 && !p.zombie)
                set_hp(p, p.hp - r.fall_damage, nullptr, kFallKill, nullptr);
            if (!p.alive) continue;
            if (p.zombie) zombie_bite(p);
            if (p.zombie) spitter_tick(p);
            if (p.zombie) boomer_tick(p);
            if (p.body.pos.z > 64.0f || !finite3(p.body.pos)) kill(p, nullptr, kFallKill);
        }
        if (aim_mode()) aim_tick(dt);
        else if (tower_defense_mode()) {
            horde_tick();
            // The guns' own beat, after the horde's, so a scan never
            // looks at a wave before the wave takes its step.
            turret_tick();
        } else {
            tutorial_tick();
        }
        for (size_t i = 0; i < grenades.size();) {
            if (world_time >= grenades[i].explode_at) {
                const Grenade g = grenades[i];
                grenades.erase(grenades.begin() + static_cast<long>(i));
                explode(g);
            } else {
                ++i;
            }
        }
        acid_melt_tick();
        acid_pool_tick();
        healer_tick();
    }

    void explode(const Grenade& g) {
        if (g.acid) {
            acid_explode(g);
            return;
        }
        Player* owner = players[g.owner].get();
        if (!owner || !owner->named || owner->team != g.team || owner->spectator()) return;
        const Vec3& pos = g.pos;
        if (pos.x < 0 || pos.x > 512 || pos.y < 0 || pos.y > 512 || pos.z < 0 || pos.z > 63) return;
        for (auto& pp : players) {
            if (!pp || !pp->alive) continue;
            if (pp.get() != owner && pp->team == owner->team) continue;
            const float dmg = nade::blast_damage(&phys_map, pos, pp->body.pos);
            if (static_cast<int>(dmg) <= 0) continue;
            set_hp(*pp, pp->hp - static_cast<int>(dmg), owner, kGrenadeKill, &pos);
        }
        const int x = static_cast<int>(std::floor(pos.x));
        const int y = static_cast<int>(std::floor(pos.y));
        const int z = static_cast<int>(std::floor(pos.z));
        std::vector<Cell> removed;
        for (int nx = x - 1; nx <= x + 1; ++nx)
            for (int ny = y - 1; ny <= y + 1; ++ny)
                for (int nz = z - 1; nz <= z + 1; ++nz) {
                    if (core_column_cell(nx, ny, nz)) continue;  // the core holds
                    map.destroy_point(nx, ny, nz, &removed);
                }
        touched(removed, false);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
        bw.write_u8(static_cast<uint8_t>(owner->id));
        bw.write_u8(kGrenadeDestroy);
        bw.write_i32(x);
        bw.write_i32(y);
        bw.write_i32(z);
        broadcast(bw, true);
    }

    // The landing of the acid: a pool painted green where it touches
    // ground, every cell of the pool booked to melt when its clock
    // runs out - the bedrock painted too, but booked its own colour
    // back, for the floor of the world is never taken - and a hurt
    // brought only where the spit met a body:
    // nothing of the grenade blast rides out from the landing any
    // more, the people hurt are the people the pool holds under their
    // feet or their stand, and a step out of the paint walks the
    // peril off. The core column it does not touch.
    void acid_explode(const Grenade& g) {
        Player* owner = players[static_cast<size_t>(g.owner)].get();
        if (!owner || !owner->named || owner->team != g.team) return;
        const Vec3& pos = g.pos;
        if (pos.x < 0 || pos.x > 512 || pos.y < 0 || pos.y > 512 ||
            pos.z < 0 || pos.z > 63) return;
        const int x = static_cast<int>(std::floor(pos.x));
        const int y = static_cast<int>(std::floor(pos.y));
        const int z = static_cast<int>(std::floor(pos.z));
        // The burst opens a cloud of green where the spit struck,
        // thinned inside a blink: the paint may fail it, the vapour
        // says the mark all the same.
        const uint8_t bed = static_cast<uint8_t>(
            kAcidBedFirst + acid_smoke_next * 2);
        acid_smoke_next = static_cast<uint8_t>(
            (acid_smoke_next + 1) % (kAcidBeds / 2));
        vapour(bed, proto::SmokeStyle::Cloud,
               Vec3{static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z)}, 2,
               0x5A, 0xFF, 0x3C, 0xE8, 7, 4);
        // The pool: the cells it lands in, the one under them, one ring
        // more around for each block of reach.
        std::vector<Cell> pool;
        for (int dx = -spitter.pool; dx <= spitter.pool; ++dx)
            for (int dy = -spitter.pool; dy <= spitter.pool; ++dy)
                for (int dz = 0; dz <= 1; ++dz) {
                    const int cx = x + dx, cy = y + dy, cz = z + dz;
                    if (!VoxelMap::valid(cx, cy, cz) ||
                        !map.solid(cx, cy, cz))
                        continue;
                    // The floor of the world holds against the acid
                    // as it holds against the people: the bedrock is
                    // never rebuilt, so the pool paints it but takes
                    // nothing of it, the paint owed back at the melt.
                    if (core_column_cell(cx, cy, cz)) continue;
                    pool.push_back(Cell{cx, cy, cz});
                }
        if (pool.empty()) return;
        acid_colour_said();
        // The hurt of the landing itself, and none beyond it: the
        // people the pool holds stood in the spit, a share of a
        // grenade hurt once, charged to the hand that spat; the pool
        // then burns whoever wades in the paint a beat at a time,
        // its own tick taking over. Both wear the fall on the wire,
        // the no one's hand death: a body the acid takes leaves no
        // burst behind it, only the ragdoll's own fall.
        const int hurt = static_cast<int>(
            100.0f * static_cast<float>(spitter.to_people));
        if (hurt > 0)
            for (auto& pp : players) {
                if (!pp || !pp->alive || pp->zombie || pp->spectator() ||
                    pp->team != kPeople)
                    continue;
                const int fx = static_cast<int>(std::floor(
                    pp->body.pos.x));
                const int fy = static_cast<int>(std::floor(
                    pp->body.pos.y));
                const int fz = static_cast<int>(std::floor(
                    pp->body.pos.z + 2.25f));
                bool in_pool = false;
                for (const Cell& c : pool) {
                    if (c.x != fx || c.y != fy) continue;
                    if (c.z != fz && c.z != fz + 1) continue;
                    in_pool = true;
                    break;
                }
                if (in_pool)
                    set_hp(*pp, pp->hp - hurt, owner, kFallKill,
                           &pos);
            }
        // And its breath over the paint: a bed of green kept at the
        // pool for the whole of the melt, fading as the last of the
        // blocks comes down.
        vapour(static_cast<uint8_t>(bed + 1),
               proto::SmokeStyle::Ground,
               Vec3{static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z) - 0.35f}, 2,
               0x5A, 0xFF, 0x3C, 0xE8,
               static_cast<uint16_t>(spitter.melt * 10.0), 8);
        int laid_cells = 0;
        for (const Cell& c : pool) {
            const uint32_t was = map.color(c.x, c.y, c.z);
            map.set(c.x, c.y, c.z, kAcidColour);
            acid_block_painted(c.x, c.y, c.z);
            // A cell a crumble apart once the clock runs out: the
            // pool falls away one at a time and the ground says so,
            // a crumble a cell, the paint holding its peril whole.
            const double at =
                world_time + spitter.melt +
                static_cast<double>(laid_cells) *
                    static_cast<double>(kAcidCrumble);
            ++laid_cells;
            // The bedrock is painted but never taken: its cell is
            // booked to come back in its own colour at the melt. A
            // second pool over a cell still painted keeps the first
            // booking - the true colour is already said - and rides
            // its clock on to the later melt.
            if (c.z >= 62) {
                bool booked = false;
                for (AcidRestore& r : restores) {
                    if (r.x != c.x || r.y != c.y || r.z != c.z)
                        continue;
                    r.at = std::max(r.at, at);
                    booked = true;
                    break;
                }
                if (!booked && was != kAcidColour)
                    restores.push_back(
                        AcidRestore{c.x, c.y, c.z, at, was});
                continue;
            }
            melts.push_back(AcidMelt{c.x, c.y, c.z, at});
        }
    }
    // The melts come due, a cell a pass of the tick: the block leaves
    // by the hand of the acid nobody, the map edit told to the march
    // and the loading clients the way any dig is.
    void acid_melt_tick() {
        for (size_t i = 0; i < melts.size();) {
            if (world_time < melts[i].at) {
                ++i;
                continue;
            }
            const AcidMelt m = melts[i];
            melts.erase(melts.begin() + static_cast<long>(i));
            if (!VoxelMap::valid(m.x, m.y, m.z) ||
                !map.solid(m.x, m.y, m.z))
                continue;
            // The booking refuses the floor of the world, and the
            // bite keeps the same word: a melt that reads the bedrock
            // or the water under it holds its crumble.
            if (m.z >= 62) continue;
            if (core_column_cell(m.x, m.y, m.z)) continue;
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
            bw.write_u8(kAcidBlockId);
            bw.write_u8(static_cast<uint8_t>(kDestroyBlock));
            bw.write_i32(m.x);
            bw.write_i32(m.y);
            bw.write_i32(m.z);
            broadcast(bw, true);
            std::vector<Cell> removed;
            map.destroy_point(m.x, m.y, m.z, &removed);
            touched(removed, false);
        }
        // The paint comes off the bedrock the same cadence the melt
        // works at: the floor is given its own colour back, not taken,
        // and the repaint is said the way the paint was.
        for (size_t i = 0; i < restores.size();) {
            if (world_time < restores[i].at) {
                ++i;
                continue;
            }
            const AcidRestore r = restores[i];
            restores.erase(restores.begin() + static_cast<long>(i));
            if (!VoxelMap::valid(r.x, r.y, r.z) ||
                !map.solid(r.x, r.y, r.z))
                continue;
            // A hand may have aired its own colour over the paint:
            // the floor keeps whatever it wears now but the acid's.
            if (map.color(r.x, r.y, r.z) != kAcidColour)
                continue;
            map.set(r.x, r.y, r.z, r.colour);
            acid_block_repainted(r.x, r.y, r.z, r.colour);
        }
    }

    // The pool, a peril the whole paint holds: the acid works on
    // whoever stands their ground inside it, a share of a grenade
    // hurt a beat, until the melt takes the blocks out from under
    // them. A body reads touched at the cells its feet and its
    // stand hold, and a melt gone past its clock bites nobody -
    // that block is already on its way down.
    void acid_pool_tick() {
        if (world_time < acid_beat_at) return;
        acid_beat_at = world_time + static_cast<double>(kAcidBeat);
        const int hurt = static_cast<int>(
            100.0f * static_cast<float>(spitter.to_people));
        if (hurt <= 0) return;
        for (auto& qq : players) {
            if (!qq || qq->zombie || !qq->alive || qq->spectator() ||
                qq->team != kPeople) continue;
            const int fx = static_cast<int>(std::floor(qq->body.pos.x));
            const int fy = static_cast<int>(std::floor(qq->body.pos.y));
            const int fz = static_cast<int>(std::floor(
                qq->body.pos.z + 2.25f));
            bool in_pool = false;
            for (const AcidMelt& m : melts) {
                if (world_time >= m.at) continue;
                if (m.x != fx || m.y != fy) continue;
                if (m.z != fz && m.z != fz + 1) continue;
                in_pool = true;
                break;
            }
            if (!in_pool)
                for (const AcidRestore& r : restores) {
                    if (world_time >= r.at) continue;
                    if (r.x != fx || r.y != fy) continue;
                    if (r.z != fz && r.z != fz + 1) continue;
                    in_pool = true;
                    break;
                }
            if (in_pool)
                set_hp(*qq, qq->hp - hurt, nullptr, kFallKill,
                       &qq->body.pos);
        }
    }

    // The healer's work, at the pool's own beat: every walker standing
    // in its reach wears a bed of pale blue at its feet, thinned inside
    // a blink and kept fresh while the healer holds its ground, and
    // the walker worst hurt among them is mended by the pace of a
    // second level sentry each beat - the same gun everyone buys, the
    // same word of it, tuned with it. A healer mends no healer of its
    // own standing, that two of them read each other whole.
    void healer_tick() {
        if (world_time < heal_beat_at) return;
        heal_beat_at = world_time + static_cast<double>(kHealBeat);
        bool any_healer = false;
        for (auto& hp : players) {
            if (!hp || !hp->alive || !hp->zombie || hp->brute != kHealer)
                continue;
            any_healer = true;
            break;
        }
        if (!any_healer) return;
        const float reach2 = healer_reach * healer_reach;
        const int mend = static_cast<int>(
            turret_cfg.damage[1] /
            static_cast<double>(turret_cfg.interval[1]) * kHealBeat);
        for (auto& hp : players) {
            if (!hp || !hp->alive || !hp->zombie || hp->brute != kHealer)
                continue;
            Player* worst = nullptr;
            for (auto& qq : players) {
                if (!qq || !qq->zombie || !qq->alive) continue;
                const float ddx = qq->body.pos.x - hp->body.pos.x;
                const float ddy = qq->body.pos.y - hp->body.pos.y;
                if (ddx * ddx + ddy * ddy > reach2) continue;
                vapour(static_cast<uint8_t>(
                           kHealBedFirst + heal_bed_next),
                       proto::SmokeStyle::Ground,
                       Vec3{std::floor(qq->body.pos.x) + 0.5f,
                            std::floor(qq->body.pos.y) + 0.5f,
                            std::floor(qq->body.pos.z + 2.25f) + 0.65f},
                       1, kHealRed, kHealGreen, kHealBlue, 0xE8, 5, 3);
                heal_bed_next = static_cast<uint8_t>(
                    (heal_bed_next + 1) % kHealBeds);
                // The pick reads no healer standing: a mender never
                // mends a mender, itself or another.
                if (qq->brute == kHealer) continue;
                if (worst == nullptr || qq->hp < worst->hp)
                    worst = qq.get();
            }
            if (worst != nullptr && worst->hp < 100 && mend > 0)
                set_hp(*worst, std::min(100, worst->hp + mend),
                       nullptr, kFallKill, nullptr);
        }
    }

    void world_update() {
        int highest = -1;
        for (int i = 0; i < kSlots; ++i)
            if (players[i] && players[i]->named) highest = i;
        if (highest < 0) return;
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::WorldUpdate));
        for (int i = 0; i <= highest; ++i) {
            const Player* p = players[i].get();
            if (p && p->named && p->playing()) {
                bw.write_vec(p->body.pos);
                bw.write_vec(p->body.ori);
            } else {
                bw.write_vec(Vec3{0, 0, 0});
                bw.write_vec(Vec3{0, 0, 0});
            }
        }
        ENetPacket* pkt = nullptr;
        for (auto& pp : players) {
            if (!pp || pp->loading || !pp->peer) continue;
            if (!pkt) pkt = make_packet(bw, false);
            enet_peer_send(pp->peer, 0, pkt);
        }
        if (pkt && pkt->referenceCount == 0) enet_packet_destroy(pkt);
    }

    // --- packets from clients ---------------------------------------------
    void on_packet(Player& p, const uint8_t* data, size_t len) {
        if (len < 1) return;
        ByteReader br(data + 1, len - 1);
        const double now = world_time;
        switch (static_cast<PacketId>(data[0])) {
            case PacketId::ExistingPlayer: on_join(p, br); break;
            case PacketId::VersionHandshakeResponse: {
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::VersionGet));
                send(p, bw);
            } break;
            case PacketId::VersionResponse: {
                p.client_info = true;
                // Offered: kick reasons, Teamplay, the lights (flashlights and
                // the time of day), message types, the outfits and the smoke -
                // the smoke at its third version, whose paths draw a way in
                // whole and whose sentry guns are said over their own blocks,
                // so a client that knows them is sent neither vapour it does
                // not need nor a pillar without its works. A horde world adds
                // the pie menu on top: the wheel the middle button opens is
                // served whole, its Defence page first, and a client that
                // claimed it drops the rings it ships with for the server
                // word. Anywhere else the wheel keeps its own.
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::ExtensionInfo));
                bw.write_u8(tower_defense_mode() ? 8 : 7);
                if (tower_defense_mode()) {
                    bw.write_u8(static_cast<uint8_t>(proto::Extension::PieMenu));
                    bw.write_u8(1);
                }
                bw.write_u8(static_cast<uint8_t>(proto::Extension::ChatType));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::Teamplay));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::KickReason));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::Flashlight));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::DaytimeWeather));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::PlayerOutfit));
                bw.write_u8(1);
                bw.write_u8(static_cast<uint8_t>(proto::Extension::Smoke));
                bw.write_u8(3);   // paths that draw a way in, sentry guns over their blocks
                send(p, bw);
            } break;
            case PacketId::ExtensionInfo: {
                proto::ExtensionInfoMsg m;
                if (!proto::read_extension_info(br, m)) break;
                const bool tp = m.offers(proto::Extension::Teamplay);
                if (tp && !p.ext_teamplay) {
                    ByteWriter bw;
                    bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
                    bw.write_u8(0);                 // Config
                    bw.write_u8(1 | 2 | 4);         // team ESP, pings, compass
                    bw.write_f32(0.0f);             // north: toward y = 0
                    bw.write_f32(-1.0f);
                    send(p, bw);
                }
                p.ext_teamplay = tp;
                // The time of day first: a client that claimed it does not draw
                // the world until it knows whether it is night.
                if (m.offers(proto::Extension::DaytimeWeather)) {
                    ByteWriter sky;
                    proto::write_daytime_sky(sky, cfg.night ? proto::kMidnight : proto::kNoon);
                    send(p, sky);
                }
                const bool fl = m.offers(proto::Extension::Flashlight);
                if (fl && !p.ext_flashlight) {
                    // Whose lamps are lit, after the state the client is loading.
                    ByteWriter state;
                    proto::write_flashlight_state(state, lit_lamps());
                    if (p.loading) p.saved.push_back(state.data);
                    else send(p, state);
                }
                p.ext_flashlight = fl;
                const bool po = m.offers(proto::Extension::PlayerOutfit);
                if (po && !p.ext_outfit) {
                    // Team 1 wears the walking dead: outfit 64, which its
                    // holders draw from Models/Player/Custom as the grunts.
                    // An outfit shows at its player's next spawn, and every
                    // wave the horde raises is one.
                    ByteWriter outfits_bytes;
                    proto::write_team_outfit(outfits_bytes, 1, proto::kOutfitZombie);
                    // The specials already standing are said again, so a
                    // latecomer sees the true faces of the wave in progress.
                    for (auto& z : players) {
                        if (!z || !z->zombie ||
                            z->outfit == proto::kOutfitZombie)
                            continue;
                        proto::write_set_outfit(
                            outfits_bytes, static_cast<uint8_t>(z->id),
                            z->outfit);
                    }
                    if (p.loading) p.saved.push_back(outfits_bytes.data);
                    else send(p, outfits_bytes);
                }
                p.ext_outfit = po;
                // The way in's smoke, whatever phase is running: its columns
                // are sent on their own cycle the moment a smoke-claiming
                // client finishes loading, so nothing is owed on the claim.
                p.ext_smoke = m.offers(proto::Extension::Smoke);
                // The paths are the same extension's second version: a
                // client that claimed v2 draws the ways in itself, as lines,
                // and is owed the beds no more; one that claimed v1 is
                // served the vapour as it always was.
                p.ext_trail = m.version_of(proto::Extension::Smoke) >= 2;
                // The sentry guns are the same extension's third word: a
                // client that claimed v3 draws the works over the blocks
                // itself. It is told the whole set of them for the claim -
                // held for it if it is still loading - and whichever state
                // later, the same five-second whole the paths are refreshed
                // on and at once whenever it changes, so a client that
                // finished loading mid-game is never left staring at guns
                // it cannot see.
                p.ext_turrets = m.version_of(proto::Extension::Smoke) >= 3;
                if (p.ext_turrets && !turrets.empty()) {
                    ByteWriter guns;
                    proto::write_turrets(guns, turret_state_now());
                    if (p.loading) p.saved.push_back(guns.data);
                    else send(p, guns);
                }
                // The pie menu: the wheel is the server word now, so it is
                // owed at once - held with the state for a client still
                // loading the map - rings and slices whole, for a client
                // that claimed it opens the server wheel or none at all.
                p.ext_piemenu = tower_defense_mode() &&
                                m.offers(proto::Extension::PieMenu);
                if (p.ext_piemenu) send_pie_menu(p);
            } break;
            case PacketId::Flashlight: on_flashlight(p, br); break;
            case PacketId::Teamplay: on_teamplay(p, br); break;
            case PacketId::ChatMessage: on_chat(p, br); break;
            case PacketId::ChangeWeapon: {
                if (!p.named || !br.can_read(2)) break;
                br.read_u8();
                const int w = br.read_u8();
                if (w > 2) break;
                // A gun the store has not sold is not a gun this player may
                // hold: the rifle is always theirs, and the rest are bought
                // with Zedcreds one way or another.
                if (tower_defense_mode() &&
                    !p.weapon_open[static_cast<size_t>(w)]) {
                    const int index = w == 1 ? 1 : 2;   // SMG, Shotgun
                    const std::string label =
                        store::kItems[static_cast<size_t>(index)].label;
                    system_chat(std::string("Buy the ") + label +
                                    " in the store first.",
                                p.id);
                    break;
                }
                set_weapon(p, w);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::ChangeWeapon));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(w));
                broadcast(bw, true);
                kill(p, nullptr, kClassChangeKill);
            } break;
            case PacketId::ChangeTeam: {
                if (!p.named || !br.can_read(2)) break;
                br.read_u8();
                int t = static_cast<int8_t>(br.read_u8());
                t = (t == 0 || t == 1) ? kPeople : -1;
                if (t == p.team) break;
                const bool was_spectator = p.spectator();
                p.team = t;
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::ChangeTeam));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(static_cast<int8_t>(t)));
                broadcast(bw, true);
                if (was_spectator || !p.alive) schedule_respawn(p);
                else kill(p, nullptr, kTeamChangeKill);
            } break;
            default: break;
        }
        if (!p.alive) return;

        switch (static_cast<PacketId>(data[0])) {
            case PacketId::PositionData: {
                if (!br.can_read(12)) break;
                const Vec3 v = br.read_vec();
                if (!finite3(v)) break;
                // Taken as sent, at any rate: there is no internet between the
                // client and here.
                p.body.pos = v;
                p.body.eye = v;
            } break;
            case PacketId::OrientationData: {
                if (!br.can_read(12)) break;
                Vec3 o = br.read_vec();
                const float l = std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z);
                if (!finite3(o) || l < 1e-4f) break;
                p.body.ori = Vec3{o.x / l, o.y / l, o.z / l};
            } break;
            case PacketId::InputData: {
                if (!br.can_read(2)) break;
                br.read_u8();
                uint8_t f = br.read_u8();
                p.input.mf = f & 1;
                p.input.mb = (f >> 1) & 1;
                p.input.ml = (f >> 2) & 1;
                p.input.mr = (f >> 3) & 1;
                bool jump = (f >> 4) & 1;
                if (jump && !(p.body.vel.z >= 0 && p.body.vel.z < 0.017f)) jump = false;
                p.input.jump = jump;
                const bool crouch = (f >> 5) & 1;
                if (crouch != p.body.crouching) physics::set_crouch(phys_map, p.body, crouch);
                p.input.crouch = crouch;
                p.input.sneak = (f >> 6) & 1;
                p.input.sprint = (f >> 7) & 1;
                if (!jump) f &= static_cast<uint8_t>(~0x10u);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::InputData));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(f);
                broadcast(bw, false, p.id, -2, false);
            } break;
            case PacketId::WeaponInput: {
                if (!br.can_read(2)) break;
                br.read_u8();
                const uint8_t f = br.read_u8();
                const bool primary = f & 1, secondary = (f >> 1) & 1;
                if (primary != p.primary && p.tool == kToolWeapon) p.gun.set_shoot(primary, now);
                if (secondary != p.secondary && secondary && p.tool == kToolBlock) {
                    p.line_start = p.body.pos;
                    p.line_started = true;
                }
                p.primary = primary;
                p.secondary = secondary;
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::WeaponInput));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(f & 3);
                broadcast(bw, false, p.id);
            } break;
            case PacketId::WeaponReload: {
                p.gun.reload(now);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::WeaponReload));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(0);
                bw.write_u8(0);
                broadcast(bw, false, p.id);
            } break;
            case PacketId::HitPacket: on_hit(p, br); break;
            case PacketId::GrenadePacket: on_grenade(p, br); break;
            case PacketId::SetTool: {
                if (!br.can_read(2) || !p.named) break;
                br.read_u8();
                const int tool = br.read_u8();
                if (tool > 3) break;
                if (p.tool == kToolWeapon) p.gun.set_shoot(false, now);
                p.tool = tool;
                if (tool == kToolWeapon) p.gun.set_shoot(p.primary, now);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::SetTool));
                bw.write_u8(static_cast<uint8_t>(p.id));
                bw.write_u8(static_cast<uint8_t>(tool));
                broadcast(bw, true, p.id);
            } break;
            case PacketId::SetColor: {
                if (!br.can_read(4) || !p.named) break;
                br.read_u8();
                const uint32_t b = br.read_u8(), g = br.read_u8(), r = br.read_u8();
                p.color = b | (g << 8) | (r << 16);
                ByteWriter bw;
                bw.write_u8(static_cast<uint8_t>(PacketId::SetColor));
                bw.write_u8(static_cast<uint8_t>(p.id));
                write_color(bw, p.color);
                broadcast(bw, true, p.id);
            } break;
            case PacketId::BlockAction: on_block_action(p, br); break;
            case PacketId::BlockLine: on_block_line(p, br); break;
            default: break;
        }
    }

    void on_join(Player& p, ByteReader& br) {
        if (p.team == 0 || p.team == 1) return;   // only from limbo or spectating
        if (!br.can_read(3)) return;
        br.read_u8();
        const int team = static_cast<int8_t>(br.read_u8());
        const int weapon = br.read_u8();
        std::string name;
        if (br.can_read(1)) {
            br.read_u8();                         // tool
            if (br.can_read(7)) {
                br.read_u32();                    // kills
                br.read_u8(); br.read_u8(); br.read_u8();
                name = br.read_cstring();
            }
        }
        p.team = (team == 0 || team == 1) ? kPeople : -1;
        if (!p.named) {
            name = sanitise_name(name);
            if (name.empty()) name = "Deuce";
            std::string candidate = name;
            for (int i = 1;; ++i) {
                bool taken = false;
                for (auto& o : players) {
                    if (!o || !o->named || o.get() == &p) continue;
                    std::string a = o->name, b = candidate;
                    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                    if (a == b) { taken = true; break; }
                }
                if (!taken) break;
                candidate = name + std::to_string(i);
            }
            p.name = candidate;
            p.named = true;
            log("%s joined", p.name.c_str());
        }
        set_weapon(p, weapon > 2 ? 0 : weapon);
        // The dead are announced to a newcomer as fallen, or it draws them
        // standing about.
        for (auto& o : players) {
            if (!o || o.get() == &p || !o->named || o->alive || o->spectator()) continue;
            ByteWriter bw;
            bw.write_u8(static_cast<uint8_t>(PacketId::KillAction));
            bw.write_u8(static_cast<uint8_t>(o->id));
            bw.write_u8(static_cast<uint8_t>(o->id));
            bw.write_u8(kFallKill);
            bw.write_u8(0);
            send(p, bw);
        }
        spawn(p);
        // The targets' outlines, for one who joins after they were placed.
        if (cfg.highlight_targets)
            for (auto& o : players)
                if (o && o->aim_bot && o->alive) send_mark(*o);
        // The brutes of a field in play stand flaring for the new eye
        // as well, the same colours the field wears them in.
        if (tower_defense_mode())
            for (auto& o : players)
                if (o && o->alive && o->zombie && o->brute >= 0)
                    send_mark(*o, brute_flare(o->brute), 1);
        p.pinged_step = -1;   // the tutorial's next goal, pinged again
    }

    void on_hit(Player& p, ByteReader& br) {
        if (!br.can_read(2)) return;
        const int target = br.read_u8();
        const int value = br.read_u8();
        if (target >= kSlots || !players[target] || target == p.id) return;
        Player& victim = *players[target];
        if (!victim.alive || value > kMelee) return;
        const bool melee = value == kMelee;
        int amount;
        if (melee) {
            if (p.tool != kToolSpade && p.tool != kToolWeapon) return;
            // The spade's own ladder: the reach of the swing and
            // what it does to what stands in it.
            const training::tiers::SpadeSpec& sp =
                training::tiers::kSpade[gun_tier(p, 3)];
            if (!near3(p.body.pos, victim.body.pos,
                       kMeleeDistance * sp.range))
                return;
            amount = sp.melee_damage;
        } else {
            if (p.tool != kToolWeapon || p.gun.is_empty(world_time)) return;
            // The damage bands the store sold, paid one shot at a time.
            amount =
                p.gun.spec().damage[value] + p.gun_damage_bonus[p.gun.type];
        }
        const int kill_type = melee ? kMeleeKill : value == kHead ? kHeadshotKill : kWeaponKill;
        if (victim.aim_bot) {
            // Only the run's own hits count, and only the run's targets take them.
            if (aim.phase != training::AimPhase::Running) return;
            if (!melee) {
                // A shot's pellets arrive together: one hit per shot, and one
                // headshot if any of them was.
                if (world_time - p.last_hit_at > 0.03) {
                    aim.hits += 1;
                    p.head_this_shot = false;
                }
                p.last_hit_at = world_time;
                if (value == kHead && !p.head_this_shot) {
                    aim.headshots += 1;
                    p.head_this_shot = true;
                }
            }
            if (cfg.scenario == training::Scenario::Tracking) return;  // it never falls
            if (training::one_shot(cfg.scenario)) {
                kill(victim, &p, kill_type);
                return;
            }
        }
        hit(victim, amount, &p, kill_type);
    }

    void on_grenade(Player& p, ByteReader& br) {
        if (!br.can_read(29)) return;
        br.read_u8();
        float fuse = br.read_f32();
        Vec3 pos = br.read_vec();
        Vec3 vel = br.read_vec();
        if (!std::isfinite(fuse) || !finite3(pos) || !finite3(vel)) return;
        if (p.grenades <= 0) return;
        p.grenades -= 1;
        if (!near3(pos, p.body.pos, 10.0f)) pos = p.body.pos;
        fuse = std::min(fuse, 3.0f);
        Vec3 rel{vel.x - p.body.vel.x, vel.y - p.body.vel.y, vel.z - p.body.vel.z};
        const float l = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
        if (l > 2.0f) {
            rel = Vec3{rel.x / l * 2.0f, rel.y / l * 2.0f, rel.z / l * 2.0f};
            vel = Vec3{rel.x + p.body.vel.x, rel.y + p.body.vel.y, rel.z + p.body.vel.z};
        }
        // Where it goes off is known now: the same flight every client
        // simulates. The map may change under it in three seconds; pyspades'
        // moving object would see that and this does not, which costs a
        // grenade landing on a block somebody dug out mid-flight.
        const nade::Impact at = nade::simulate(&phys_map, pos, vel, std::max(fuse, 0.0f));
        grenades.push_back(Grenade{p.id, p.team, at.pos, world_time + std::max(fuse, 0.0f)});
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::GrenadePacket));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_f32(fuse);
        bw.write_vec(pos);
        bw.write_vec(vel);
        broadcast(bw, false, p.id);
    }

    void on_block_action(Player& p, ByteReader& br) {
        if (!br.can_read(14)) return;
        br.read_u8();
        const int value = br.read_u8();
        const proto::IVec3 c = br.read_ivec();
        const int x = c.x, y = c.y, z = c.z;
        if (value == kGrenadeDestroy || value > kGrenadeDestroy) return;
        if (z >= 62 || !VoxelMap::valid(x, y, z)) return;
        const Vec3& pos = p.body.pos;
        auto within = [&](float d) {
            return std::fabs(pos.x - x) < d && std::fabs(pos.y - y) < d && std::fabs(pos.z - z) < d;
        };
        if (value == kBuildBlock) {
            // Tower Defense spends no block: the range stands its
            // builders in a standing fifty, refilled without a word.
            p.blocks -= tower_defense_mode() ? 0 : 1;
            if (p.blocks < -kBuildTolerance || !within(kMaxBlockDistance)) return;
            if (horde && !horde->can_build(x, y, z)) {
                // a refusal spends no block
                p.blocks += tower_defense_mode() ? 0 : 1;
                system_chat("That block would seal the last way in.", p.id);
                return;
            }
            if (!map.build_point(x, y, z, p.color | 0x7F000000u)) return;
            touched({Cell{x, y, z}}, true);
        } else {
            if (p.tool == kToolWeapon && p.gun.is_empty(world_time)) return;
            if (!map.solid(x, y, z)) return;
            if (core_column_cell(x, y, z)) {
                system_chat("The core cannot be torn down.", p.id);
                return;
            }
            if (p.tool == kToolSpade &&
                !within(kMaxDigDistance *
                        training::tiers::kSpade[gun_tier(p, 3)].range))
                return;
            std::vector<Cell> removed;
            if (value == kDestroyBlock) {
                if (map.destroy_point(x, y, z, &removed)) p.blocks = std::min(50, p.blocks + 1);
            } else {
                map.destroy_point(x, y, z, &removed);
                map.destroy_point(x, y, z + 1, &removed);
                map.destroy_point(x, y, z - 1, &removed);
            }
            touched(removed, false);
        }
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockAction));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(static_cast<uint8_t>(value));
        bw.write_i32(x);
        bw.write_i32(y);
        bw.write_i32(z);
        broadcast(bw, true);
    }

    void on_block_line(Player& p, ByteReader& br) {
        if (!br.can_read(25) || !p.line_started) return;
        br.read_u8();
        const proto::IVec3 a = br.read_ivec();
        const proto::IVec3 b = br.read_ivec();
        if (!VoxelMap::valid(a.x, a.y, a.z) || !VoxelMap::valid(b.x, b.y, b.z)) return;
        auto within = [](const Vec3& v, const proto::IVec3& c) {
            return std::fabs(v.x - c.x) < kMaxBlockDistance && std::fabs(v.y - c.y) < kMaxBlockDistance &&
                   std::fabs(v.z - c.z) < kMaxBlockDistance;
        };
        if (!within(p.body.pos, b) || !within(p.line_start, a)) return;
        if (!map.has_neighbors(a.x, a.y, a.z) || !map.has_neighbors(b.x, b.y, b.z)) return;
        const std::vector<Cell> pts = cube_line(a.x, a.y, a.z, b.x, b.y, b.z);
        if (pts.empty() || static_cast<int>(pts.size()) > p.blocks + kBuildTolerance) return;
        std::vector<Cell> built;
        for (const Cell& c : pts) {
            if (map.solid(c.x, c.y, c.z)) continue;
            if (horde && !horde->can_build(c.x, c.y, c.z)) continue;   // the veto skips a cell
            if (!map.build_point(c.x, c.y, c.z, p.color | 0x7F000000u)) break;
            built.push_back(c);
        }
        touched(built, true);
        // A line of blocks spends none on the Tower Defense range either.
        p.blocks -= tower_defense_mode() ? 0 : static_cast<int>(pts.size());
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::BlockLine));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_ivec(a.x, a.y, a.z);
        bw.write_ivec(b.x, b.y, b.z);
        broadcast(bw, true);
    }

    // --- the pie menu (extension 0x31): the wheel, served whole ----------
    //
    // The middle button opens a wheel of rings, and a menu a server sends
    // replaces the client built-in rings entirely, ring for ring - so the
    // Defence ring is served first, the page the wheel opens on wherever it
    // is opened, and behind it the built-in rings are said again, their
    // words and their marks, that nothing the wheel always offered is lost
    // to the server word. A ring is six slices whatever fills them: the
    // empty ones draw nothing and cannot be chosen. A slice is a chat line
    // the client speaks as the player picks it, on team chat or the whole
    // server by its flag: the turret words and the wave word land in the
    // command own hands, the mark words carry the aimed place as their
    // first numbers, and the rest are chat as they always were. The context
    // bits are what the crosshair is on when the wheel opens, as the client
    // numbers them.
    static constexpr uint8_t kPieBlock = 1 << 0;
    static constexpr uint8_t kPieTeammate = 1 << 1;
    static constexpr uint8_t kPieEnemy = 1 << 2;
    static constexpr uint8_t kPieSpectator = 1 << 3;
    static constexpr uint8_t kPieDead = 1 << 4;
    static constexpr uint8_t kPieSky = 1 << 5;
    static constexpr uint8_t kPieWorld = kPieBlock | kPieSky;
    static constexpr uint8_t kPiePlayer = kPieTeammate | kPieEnemy;

    struct PieSliceDef {
        const char* text;
        const char* label;
        const char* tooltip;
        bool global;
    };
    struct PieRingDef {
        uint8_t contexts;
        const char* theme;
        PieSliceDef slices[6];
    };

    // The served whole. The Defence ring stands in every context, dead and
    // spectating with the rest, for the wave word is owed wherever a
    // defender stands. The built-in rings behind it keep the order the
    // client ships them: the pointing ring that marks the aimed place with
    // a ping the server takes, the words said to no-one in particular, the
    // warnings and the asking said to the player under the crosshair
    // alone, the taunt said to the whole server, and the pointing ring
    // again wherever a player is the ground aimed at.
    static const std::vector<PieRingDef>& pie_rings() {
        static const std::vector<PieRingDef> rings = {
          {kPieBlock | kPieTeammate | kPieEnemy | kPieSpectator | kPieDead |
               kPieSky,
           "Defence",
           {{"/turret1", "Turret 1",
             "a sentry of the first make, where you stand", false},
            {"/turret2", "Turret 2",
             "a sentry of the second make, where you stand", false},
            {"/turret3", "Turret 3",
             "a sentry of the third make, where you stand", false},
            {"/turretremove", "Take down",
             "your own sentries, taken down", false},
            {"/clearwave", "Clear wave",
             "the standing wave, called off", false},
            {"", "", "", false}}},
          {kPieBlock,
           "Point",
           {{"/pingmark %h Enemy Here!", "Enemy Here!", "", false},
            {"/pingmark %h Tear It Down!", "Tear It Down!", "", false},
            {"/pingmark %h Watch This Spot", "Watch This Spot", "", false},
            {"/pingmark %h Go Here!", "Go Here!", "", false},
            {"/pingmark %h Let's Dig Here", "Let's Dig Here", "", false},
            {"/pingmark %h Help Me Build", "Help Me Build", "", false}}},
          {kPieWorld | kPieDead,
           "Social",
           {{"Affirmative", "", "", false},
            {"Thank You", "", "", false},
            {"Hi!", "", "", false},
            {"Negative", "", "", false},
            {"Sorry!", "", "", false},
            {"Help Me", "", "", false}}},
          {kPieWorld | kPieDead,
           "Tactics",
           {{"Attack!", "", "", false},
            {"Get the Intel!", "", "", false},
            {"Enemy Has the Intel!", "", "", false},
            {"Fall Back!", "", "", false},
            {"Regroup on Me", "", "", false},
            {"Defend the Intel!", "", "", false}}},
          {kPieTeammate,
           "Warn",
           {{"/pm %p Above You!", "Above You!", "", false},
            {"/pm %p On Your Right!", "On Your Right!", "", false},
            {"/pm %p Behind You!", "Behind You!", "", false},
            {"/pm %p Below You!", "Below You!", "", false},
            {"/pm %p Sniper on You!", "Sniper on You!", "", false},
            {"/pm %p On Your Left!", "On Your Left!", "", false}}},
          {kPieTeammate,
           "Social",
           {{"/pm %p Affirmative", "Affirmative", "", false},
            {"/pm %p Thank You", "Thank You", "", false},
            {"/pm %p Hi!", "Hi!", "", false},
            {"/pm %p Negative", "Negative", "", false},
            {"/pm %p Sorry!", "Sorry!", "", false},
            {"/pm %p Help Me", "Help Me", "", false}}},
          {kPieTeammate,
           "Cooperate",
           {{"/pm %p Follow Me", "Follow Me", "", false},
            {"/pm %p Cover Me", "Cover Me", "", false},
            {"/pm %p Let Me Through", "Let Me Through", "", false},
            {"/pm %p Stay Here", "Stay Here", "", false},
            {"/pm %p Boost Me Up", "Boost Me Up", "", false},
            {"/pm %p Help Me Build", "Help Me Build", "", false}}},
          {kPieEnemy,
           "Taunt",
           {{"I See You", "", "", true},
            {"Nice Try", "", "", true},
            {"Miss Me?", "", "", true},
            {"Too Easy", "", "", true},
            {"Behind You...", "", "", true},
            {"Say Goodbye", "", "", true}}},
          {kPiePlayer,
           "Point",
           {{"/pingmark %h Enemy Here!", "Enemy Here!", "", false},
            {"/pingmark %h Tear It Down!", "Tear It Down!", "", false},
            {"/pingmark %h Watch This Spot", "Watch This Spot", "", false},
            {"/pingmark %h Go Here!", "Go Here!", "", false},
            {"/pingmark %h Let's Dig Here", "Let's Dig Here", "", false},
            {"/pingmark %h Help Me Build", "Help Me Build", "", false}}},
        };
        return rings;
    }

    // One string of the menu wire: a 0xFF the client takes for what follows
    // is UTF-8, the bytes, and the NUL that ends them. Every text the menu
    // serves is plain ASCII, which is UTF-8 as it stands.
    static void pie_string(ByteWriter& bw, const char* s) {
        bw.write_u8(0xFF);
        bw.write_cstring(s);
    }

    // The whole menu on the wire, sub 0 of the pie packet, held with the
    // state for a client still loading the map: served whole or refused
    // whole, and a byte too many at the end leaves the client the rings it
    // ships with.
    void send_pie_menu(Player& p) {
        const auto& rings = pie_rings();
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::PieMenu));
        bw.write_u8(0);               // the Menu sub packet
        bw.write_u8(static_cast<uint8_t>(rings.size()));
        for (const PieRingDef& ring : rings) {
            bw.write_u8(ring.contexts);
            bw.write_u8(0);           // the theme own message id: none
            pie_string(bw, ring.theme);
            for (const PieSliceDef& s : ring.slices) {
                bw.write_u8(0);       // the slice own message id: none
                bw.write_u8(s.global ? 1 : 0);
                pie_string(bw, s.text);
                pie_string(bw, s.label);
                pie_string(bw, s.tooltip);
            }
        }
        if (p.loading) p.saved.push_back(bw.data);
        else send(p, bw);
    }

    // The pointing slices land as this: the aimed place in three numbers
    // the client filled in for the %h of its own, then the words. The word
    // is parsed back into a ping, in the colour of the team that said it,
    // in the world and on the minimap alike, at the pace the ping button
    // keeps: kPingInterval between the marks, whatever said them.
    void ping_mark_command(Player& p, const std::string& text) {
        if (!p.alive || !p.playing()) return;
        if (world_time - p.last_ping < kPingInterval) return;
        std::vector<double> ns;
        // The word is kept as it was said, case and all: the numbers parse
        // the same whatever their case, and the words of the reason are
        // the words the wheel drew.
        const std::string cut = trimmed(text.substr(9));
        size_t from = 0;
        for (;;) {
            const size_t sp = cut.find(' ', from);
            const std::string one = sp == std::string::npos
                                        ? cut.substr(from)
                                        : cut.substr(from, sp - from);
            if (!one.empty()) ns.push_back(std::strtod(one.c_str(), nullptr));
            if (ns.size() == 3) break;
            if (sp == std::string::npos) break;
            from = sp + 1;
        }
        std::string reason = "";
        if (ns.size() == 3) {
            const size_t sp = cut.find(' ', from);
            reason = sp == std::string::npos ? "" : cut.substr(sp + 1);
        }
        const bool bad = ns.size() < 3 || std::isnan(ns[0]) ||
                          std::isnan(ns[1]) || std::isnan(ns[2]);
        if (bad || ns[0] < 0.0 || ns[0] > 512.0 || ns[1] < 0.0 ||
            ns[1] > 512.0 || ns[2] < 0.0 || ns[2] > 64.0) {
            system_chat("usage: /pingmark x y z <what to say>", p.id);
            return;
        }
        p.last_ping = world_time;
        if (reason.size() > proto::kTeamplayReasonMax)
            reason.resize(proto::kTeamplayReasonMax);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
        bw.write_u8(1);               // Ping
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_vec(Vec3{static_cast<float>(ns[0]), static_cast<float>(ns[1]),
                          static_cast<float>(ns[2])});
        bw.write_f32(kPingDuration);
        bw.write_u8(1 | 2);           // in the world and on the minimap
        write_color(bw, p.team == 0 ? 0x3070FF : 0x30E040);
        bw.write_u8(0);
        for (char ch : reason) bw.write_u8(static_cast<uint8_t>(ch));
        for (auto& o : players) {
            if (!o || o->loading || !o->ext_teamplay || o->team != p.team)
                continue;
            send(*o, bw);
        }
    }

    // The word said to the one player alone: "/pm #N words", the aimed
    // player the client names for the %p of its own. The words are spoken
    // as the sender team read, to the named player only - the same line
    // the team reads, word for word, minus everybody else.
    void pm_command(Player& p, const std::string& text) {
        const std::string rest = trimmed(text.substr(3));
        size_t digits = 0;
        if (digits < rest.size() && rest[digits] == '#') ++digits;
        const size_t start = digits;
        while (digits < rest.size() && rest[digits] >= '0' &&
               rest[digits] <= '9')
            ++digits;
        if (digits == start || digits >= rest.size() || rest[digits] != ' ') {
            system_chat("usage: /pm <player> <message>", p.id);
            return;
        }
        int dest = 0;
        for (size_t i = start; i < digits; ++i)
            dest = dest * 10 + (rest[i] - '0');
        if (dest < 0 || dest >= kSlots || !players[dest] ||
            !players[dest]->named) {
            system_chat("No such player to tell.", p.id);
            return;
        }
        const std::string message = trimmed(rest.substr(digits + 1));
        if (message.empty()) {
            system_chat("usage: /pm <player> <message>", p.id);
            return;
        }
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::ChatMessage));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(1);               // the team read, to the one alone
        bw.write_cstring(message);
        send(*players[dest], bw);
    }

    void on_chat(Player& p, ByteReader& br) {
        if (!p.named || !br.can_read(2)) return;
        br.read_u8();
        const int type = br.read_u8();
        std::string text;
        while (br.can_read(1)) {
            const char ch = static_cast<char>(br.read_u8());
            if (ch == 0) break;
            text.push_back(ch == '\r' || ch == '\n' ? ' ' : ch);
        }
        if (text.size() > 108) text.resize(108);
        if (!text.empty() && text[0] == '/') {
            if (text == "/kill" && p.alive) kill(p, nullptr, kFallKill);
            else if (text == "/restart" && aim_mode()) {
                reset_run();
                if (p.alive) spawn(p);
            } else if (text == "/clearwave" && tower_defense_mode()) {
                foul_score();
                clear_wave();
            } else if (tower_defense_mode() &&
                       text.rfind("/dosh", 0) == 0) {
                dosh_command(p, text);
            }
            else if ((text == "/money" || text == "/credits") &&
                     tower_defense_mode())
                system_chat(std::to_string(p.zc) + " Zedcreds in hand.", p.id);
            else if (text.rfind("/pingmark", 0) == 0)
                ping_mark_command(p, text);
            else if (text.rfind("/pm", 0) == 0)
                pm_command(p, text);
            else if (tower_defense_mode() &&
                     (text.rfind("/turret", 0) == 0 || text.rfind("/sentry", 0) == 0)) {
                turret_command(p, text);
            } else if (tower_defense_mode() &&
                       text.rfind("/brute", 0) == 0) {
                brute_command(p, text);
            } else if (tower_defense_mode() && text.rfind("/core", 0) == 0) {
                core_command(p, text);
            } else if (tower_defense_mode() && text.rfind("/buy", 0) == 0) {
                store_command(p, text);
            } else if (tower_defense_mode() && text.rfind("/store", 0) == 0) {
                store_command(p, text);
            } else if (tower_defense_mode() &&
                       (text == "/va" || text == "/mapadvance")) {
                map_advance_command(p);
            } else if (tower_defense_mode())
                system_chat("Commands here: /clearwave, /dosh, /kill, "
                            "/turret, /core, /money, /va, /brute, /buy",
                            p.id);
            else system_chat(aim_mode() ? "Commands here: /restart, /kill"
                                        : "Commands here: /kill", p.id);
            return;
        }
        const bool all = type == static_cast<int>(proto::ChatType::All);
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::ChatMessage));
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_u8(all ? 0 : 1);
        bw.write_cstring(text);
        broadcast(bw, false, -1, all ? -2 : p.team);
    }

    // --- lamps (Flashlight, extension 0x32) --------------------------------
    std::vector<bool> lit_lamps() const {
        std::vector<bool> lit(kSlots, false);
        for (const auto& pp : players)
            if (pp && pp->lamp) lit[static_cast<size_t>(pp->id)] = true;
        return lit;
    }
    // To everybody who claimed the extension, the switcher too: that echo is
    // what lights it. Saved for a client still loading, which applies it once
    // its world exists.
    void send_lamp(const Player& owner) {
        ByteWriter bw;
        proto::write_flashlight_light(bw, static_cast<uint8_t>(owner.id), owner.lamp);
        for (auto& pp : players) {
            if (!pp || !pp->ext_flashlight) continue;
            if (pp->loading) pp->saved.push_back(bw.data);
            else send(*pp, bw);
        }
    }
    void on_flashlight(Player& p, ByteReader& br) {
        proto::FlashlightMsg m;
        if (!p.ext_flashlight || !proto::read_flashlight(br, m)) return;
        if (m.sub != proto::FlashlightSub::Light) return;   // the rest are ours to send
        // The dead and the spectators carry no lamp; Kill Action puts one out.
        if (!p.alive || p.spectator()) return;
        if (p.lamp == m.on) return;
        p.lamp = m.on;
        send_lamp(p);
    }

    // Teamplay ping (sub 1), client to server: relayed to every teammate that
    // claimed the extension, the sender filled in by us.
    void on_teamplay(Player& p, ByteReader& br) {
        if (!br.can_read(1) || br.read_u8() != 1) return;
        if (!p.alive || !p.playing()) return;
        if (!br.can_read(21)) return;
        br.read_u8();                 // sender: ours to fill
        const Vec3 at = br.read_vec();
        br.read_f32();
        br.read_u8();
        br.read_u8(); br.read_u8(); br.read_u8();
        br.read_u8();
        std::string reason;
        while (br.can_read(1) && reason.size() < proto::kTeamplayReasonMax)
            reason.push_back(static_cast<char>(br.read_u8()));
        if (!finite3(at) || at.x < 0 || at.x > 512 || at.y < 0 || at.y > 512 || at.z < 0 || at.z > 64) return;
        if (world_time - p.last_ping < kPingInterval) return;
        p.last_ping = world_time;
        ByteWriter bw;
        bw.write_u8(static_cast<uint8_t>(PacketId::Teamplay));
        bw.write_u8(1);
        bw.write_u8(static_cast<uint8_t>(p.id));
        bw.write_vec(at);
        bw.write_f32(kPingDuration);
        bw.write_u8(0);
        write_color(bw, p.team == 0 ? 0x3070FF : 0x30E040);
        bw.write_u8(0);
        for (char ch : reason) bw.write_u8(static_cast<uint8_t>(ch));
        for (auto& o : players) {
            if (!o || o->loading || !o->ext_teamplay || o->team != p.team) continue;
            send(*o, bw);
        }
    }

    // --- the loop ---------------------------------------------------------
    void pump(int wait_ms) {
        ENetEvent ev;
        auto take = [&]() {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT: on_connect(ev.peer, ev.data); break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    if (ev.peer->data) on_disconnect(*static_cast<Player*>(ev.peer->data));
                    ev.peer->data = nullptr;
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    if (ev.peer->data)
                        on_packet(*static_cast<Player*>(ev.peer->data), ev.packet->data, ev.packet->dataLength);
                    enet_packet_destroy(ev.packet);
                    break;
                default: break;
            }
        };
        // Read until the socket is empty: ENet reads at most 256 datagrams a
        // service, and a full read means there may be more.
        uint32_t read_before = host->totalReceivedPackets;
        int r = enet_host_service(host, &ev, static_cast<enet_uint32>(std::max(wait_ms, 0)));
        for (int round = 1;; ++round) {
            while (r > 0) {
                take();
                r = enet_host_check_events(host, &ev);
            }
            if (r < 0 || host->totalReceivedPackets - read_before < 256 || round >= 16) break;
            read_before = host->totalReceivedPackets;
            r = enet_host_service(host, &ev, 0);
        }
        serve_maps();
        chunk_budget = 24;
        for (auto& pp : players)
            if (pp && pp->loading) continue_map(*pp);
        const double t = now_sec();
        int ticks = 0;
        while (t - world_time > kTick && ticks < 30) {
            world_time += kTick;
            world_tick();
            ++ticks;
        }
        if (t - world_time > 1.0) world_time = t;   // a stall: do not try to catch up
        const double period = 1.0 / std::max(1, cfg.update_rate);
        if (t >= next_network) {
            // Behind by more than a period (a stall): start again from now.
            next_network = t - next_network > period ? t + period : next_network + period;
            world_update();
        }
        enet_host_flush(host);
    }
};

// ---------------------------------------------------------------------------

Server::Server(const HostConfig& cfg) : impl_(std::make_unique<Impl>()) { impl_->cfg = cfg; }

Server::~Server() {
    if (impl_->host) {
        for (auto& p : impl_->players)
            if (p && p->peer) enet_peer_disconnect_now(p->peer, 5);
        enet_host_destroy(impl_->host);
    }
    if (impl_->map_job.joinable()) impl_->map_job.join();
    if (impl_->enet_up) enet_deinitialize();
}

bool Server::start(std::string& err) {
    Impl& s = *impl_;
    // WSAStartup on Windows, nothing elsewhere. It counts, so the server's own
    // call and the game's do not undo each other.
    if (enet_initialize() != 0) { err = "cannot start networking"; return false; }
    s.enet_up = true;
    std::string map_err;
    if (!s.load_map(map_err)) { err = map_err; return false; }
    if (s.aim_mode()) {
        s.aim.scenario = s.cfg.scenario;
        s.add_aim_bots();
    } else if (s.tower_defense_mode()) {
        // The corners of the map are the spawns, the middle the core; the map
        // is flat open land everywhere outside the course's corridor. On a
        // .vxl of coastline the cells the settings ask for can be at sea, so
        // each is walked in to the nearest land.
        // The map's own settings, when they named the core's cell, outrank
        // the settings' word: a shipped course knows where its towns stand,
        // whatever a setting the player never touched would have it at.
        if (s.map_core_x >= 0) {
            s.cfg.core_x = s.map_core_x;
            s.cfg.core_y = s.map_core_y;
        }
        s.horde_cfg.core_x = s.cfg.core_x;
        s.horde_cfg.core_y = s.cfg.core_y;
        // The waves deal their own quarters this side: every wave redraws
        // the asked-for centres round the town, spread even and shuffled,
        // so no colour keeps the same corner game long. (The Horde's own
        // default stays the settings' fixed quarters - its tests read
        // them deterministically.) A map that named its spawn sites is
        // spared the dealing: the named ground stands, wave after wave,
        // and each wave draws the fronts it opens from the named pool
        // by lot.
        s.horde_cfg.roam_spawns = true;
        // The map that named the core's foot layer is trusted as it
        // stands: the walk to land would drag the core of a room in the
        // hill out to the surface, the one thing its z exists to prevent.
        if (s.map_core_z < 1 || s.map_core_z > 62)
            s.nearest_land(s.horde_cfg.core_x, s.horde_cfg.core_y);
        // The sites the map named are the whole pool the fronts come
        // from, in the file order, and the wave that opens fewer
        // fronts than the pool holds draws them by lot. A site that
        // named its ground layer is trusted as it stands, the walk to
        // land spared it like the core of the room inside the hill; a
        // site that named none walks to the nearest land, as ever.
        const size_t named_sites = s.map_spawn_x.size();
        if (named_sites > 0) {
            int sites = static_cast<int>(named_sites);
            if (sites > horde::Config::kSpawnSites)
                sites = horde::Config::kSpawnSites;
            s.horde_cfg.spawn_named = true;
            s.horde_cfg.spawn_count = sites;
            for (int k = 0; k < sites; ++k) {
                int x = s.map_spawn_x[static_cast<size_t>(k)];
                int y = s.map_spawn_y[static_cast<size_t>(k)];
                int z = s.map_spawn_z[static_cast<size_t>(k)];
                if (x < 2) x = 2;
                if (y < 2) y = 2;
                if (x > 509) x = 509;
                if (y > 509) y = 509;
                if (z < 1 || z > 62) z = -1;
                if (z < 0) s.nearest_land(x, y);
                s.horde_cfg.spawn_x[k] = x;
                s.horde_cfg.spawn_y[k] = y;
                s.horde_cfg.spawn_z[k] = z;
            }
        } else {
            for (int k = 0; k < horde::Config::kSpawnSites; ++k) {
                int x = s.horde_cfg.spawn_x[k], y = s.horde_cfg.spawn_y[k];
                s.nearest_land(x, y);
                s.horde_cfg.spawn_x[k] = x;
                s.horde_cfg.spawn_y[k] = y;
            }
        }
        // The host's own word for the guns' tables, when it wrote one:
        // towerdefense.toml in the directory the server runs in, seen
        // again at every start of a tower defense world.
        s.load_turrets_tuning();
        // The store's words for its prices, from the same file the pak
        // ships: what the shelf says is what the purse pays.
        s.load_store_items_txt();
        s.build_core_column();
        s.map.to_solid_map(s.phys_map);
        s.horde = std::make_unique<horde::Horde>(s.phys_map, s.horde_cfg);
    } else {
        s.add_targets();
        s.bot_count = training::kLaneCount;
    }
    s.map_cache = std::make_shared<const std::vector<uint8_t>>(s.map.save_compressed());
    s.cache_rev = s.map_rev;
    ENetAddress addr;
    enet_address_set_host(&addr, "127.0.0.1");
    addr.port = s.cfg.port;
    s.host = enet_host_create(&addr, s.cfg.max_players + 2, 1, 0, 0);
    if (!s.host) {
        err = "cannot listen on 127.0.0.1:" + std::to_string(s.cfg.port) + " (is something already using it?)";
        return false;
    }
    // The range coder on the way in only. Clients compress what they send and
    // flag it, so it has to be decoded; what the server sends goes plain,
    // which every ENet peer reads, and over loopback the coder saves nothing.
    {
        ENetCompressor inbound{};
        inbound.context = enet_range_coder_create();
        inbound.decompress = enet_range_coder_decompress;
        inbound.destroy = enet_range_coder_destroy;
        if (inbound.context) enet_host_compress(s.host, &inbound);
        else enet_host_compress_with_range_coder(s.host);
    }
    s.world_time = now_sec();
    s.next_network = s.world_time;
    if (s.horde) {
        s.horde->restart(s.world_time);
        s.system_chat("Build while it is quiet: the first wave comes in twenty seconds.");
        s.load_score_book();
        if (s.score_best > 0) {
            char page[96];
            std::snprintf(page, sizeof page,
                          "The record book holds wave %d on this map.",
                          s.score_best);
            s.system_chat(page);
        }
    }
    s.log("training course on 127.0.0.1:%u, %d world updates a second",
          static_cast<unsigned>(s.cfg.port), s.cfg.update_rate);
    return true;
}

void Server::pump(int wait_ms) { impl_->pump(wait_ms); }

void Server::run(const std::atomic<bool>& stop) {
    // A fixed cycle: take everything that has arrived, run the world, send
    // once. On Windows a sleep lasts a multiple of the system timer, 15.6ms
    // unless somebody asks for finer, which would halve the world updates:
    // this asks for 1ms for as long as the server runs.
#ifdef _WIN32
    timeBeginPeriod(1);
    struct EndPeriod { ~EndPeriod() { timeEndPeriod(1); } } end_period;
#endif
    const double cycle = 1.0 / std::max(120, impl_->cfg.update_rate);
    auto next = std::chrono::steady_clock::now();
    const auto step = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(cycle));
    while (!stop.load(std::memory_order_relaxed)) {
        impl_->pump(0);
        next += step;
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now;   // behind: do not try to catch up
        else std::this_thread::sleep_until(next);
    }
}

int Server::player_count() const {
    int n = 0;
    for (auto& p : impl_->players)
        if (p && p->named) ++n;
    return n;
}
int Server::player_hp(int id) const {
    if (id < 0 || id >= kSlots || !impl_->players[id] || !impl_->players[id]->alive) return -1;
    return impl_->players[id]->hp;
}
bool Server::player_alive(int id) const { return player_hp(id) > 0; }
int Server::player_team(int id) const {
    if (id < 0 || id >= kSlots || !impl_->players[id]) return -2;
    return impl_->players[id]->team;
}
bool Server::player_position(int id, float& x, float& y, float& z) const {
    if (id < 0 || id >= kSlots || !impl_->players[id] || !impl_->players[id]->alive) return false;
    const Vec3& p = impl_->players[id]->body.pos;
    x = p.x, y = p.y, z = p.z;
    return true;
}
bool Server::map_solid(int x, int y, int z) const {
    return VoxelMap::valid(x, y, z) && impl_->map.solid(x, y, z);
}

}  // namespace host

