// The horde brain's checks (Sources/Training/Horde.cpp), built on request
// with the other zs_tests and run by CTest. No server, no ENet: a SolidMap
// built cell by cell, and the facts the waves, the field and the veto must
// hold on it.
//
// The map is a plain: ground with its top at z = 32. One solid voxel is no
// ground at all, so the ground plane is a slab, z = 32 and z = 33 solid in
// every column. A wall is the same three voxels higher, its top at z = 29.
// An arch is a wall breached by a tunnel, its lintel high enough to leave
// the plain's floor below: two standing surfaces in one column, the road
// over the cap and the road through the tunnel, and the walk takes
// whichever the legs can. One crossing is asked of it: the walk of the
// legs is reversible - a step of two is a jump, up or down, and anything
// more is a wall, for the legs alone cannot dig. But the dead carry a
// spade, and beside the legs' walk a second field prices what the spade
// could cross - a fall of up to kFall, a climb or a drop beyond the
// jump at kDigPenalty steps a block - consulted only on the licences
// of the pinned: the stuck
// clock run out where the legs' walk is silent, the watch run down
// where no ground is gained, so a wall is crossed only when the walk
// has already failed the dead at it, and a sealed town still anchors
// its waves on the spade's own frontier.
// The veto asks the same legs' walk the
// other way round, with the block a player wants
// held out of it, and the route the host paints is read off it cell by
// cell. The waves walk in from the far end of the walk, the borderlands
// the terrain stops the legs in: the anchor of a corner is the longest
// way its reach holds.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "SolidMap.h"
#include "Training/Horde.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("  ok: %s\n", what);
    } else {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

constexpr int GROUND_TOP = 32;
constexpr int WALL_TOP = GROUND_TOP - 3;  // three stacked: a wall, not a jump

void slab(SolidMap& map, int x, int y) {
    map.set_block(x, y, GROUND_TOP + 1, true);
    map.set_block(x, y, GROUND_TOP, true);
}

void plain(SolidMap& map) {
    for (int y = 0; y < SolidMap::SIZE_Y; ++y)
        for (int x = 0; x < SolidMap::SIZE_X; ++x) slab(map, x, y);
}

// The square ring of Chebyshev radius `r` around (cx, cy), closed everywhere
// except up to two gap cells named. `g2x < 0` means no second gap.
struct Ring {
    int cx, cy, r;
    int g1x, g1y;
    int g2x = -1, g2y = -1;
};

void ring(SolidMap& map, const Ring& w) {
    for (int y = w.cy - w.r; y <= w.cy + w.r; ++y)
        for (int x = w.cx - w.r; x <= w.cx + w.r; ++x) {
            const int dx = x > w.cx ? x - w.cx : w.cx - x;
            const int dy = y > w.cy ? y - w.cy : w.cy - y;
            if (dx != w.r && dy != w.r) continue;  // the ring only
            if (x == w.g1x && y == w.g1y) continue;
            if (w.g2x >= 0 && x == w.g2x && y == w.g2y) continue;
            map.set_block(x, y, WALL_TOP + 2, true);
            map.set_block(x, y, WALL_TOP + 1, true);
            map.set_block(x, y, WALL_TOP, true);
        }
}

horde::Config horde_cfg(int cx, int cy) {
    horde::Config cfg;
    cfg.core_x = cx;
    cfg.core_y = cy;
    cfg.spawn_x[0] = 32; cfg.spawn_y[0] = 32;
    cfg.spawn_x[1] = 479; cfg.spawn_y[1] = 32;
    cfg.spawn_x[2] = 32; cfg.spawn_y[2] = 479;
    cfg.spawn_x[3] = 479; cfg.spawn_y[3] = 479;
    // The six newer spawn points the Config carries are sent beyond any
    // ring the search can walk out: these rounds stay the four corners'
    // own, as they always were.
    for (int k = 4; k < horde::Config::kSpawnSites; ++k) {
        cfg.spawn_x[k] = 712;
        cfg.spawn_y[k] = 712;
    }
    cfg.spawn_size = 12;
    cfg.prep_seconds = 10.0;
    cfg.intermission_seconds = 5.0;
    cfg.spawn_interval = 0.5;
    cfg.wave_count_max = 2;
    cfg.core_hp = 150;
    cfg.core_mend = 25;
    cfg.field_period = 0.5;
    return cfg;
}

void test_field() {
    std::printf("[field] distances and steps on the open plain\n");
    SolidMap map;
    plain(map);
    horde::Horde h(map, horde_cfg(256, 256));
    check(h.distance(256, 256) == 0, "the core is distance 0");
    check(h.distance(255, 256) == 1, "a step aside is distance 1");
    check(h.distance(32, 32) > 400, "a far corner is a long walk");
    check(h.distance(-1, 0) < 0 && h.distance(512, 0) < 0, "outside is nowhere");
    int tx = 0, ty = 0;
    check(h.next_step(100, 100, tx, ty), "next_step answers on open ground");
    check(h.distance(tx, ty) < h.distance(100, 100), "next_step heads downhill");
    check(!h.next_step(256, 256, tx, ty), "at the core, no step helps");

    // The crossing rules of the SPEC: the walk is reversible - a jump is
    // two blocks either way, and taller than a jump is a wall both ways.
    // Piles and pits on the plain say so.
    {
        SolidMap rough;
        plain(rough);
        // A pile of two at (200, 200): its top is a jump away.
        rough.set_block(200, 200, 31, true);
        rough.set_block(200, 200, 30, true);
        // A pile of three at (210, 210): a wall, however it is looked at.
        rough.set_block(210, 210, 31, true);
        rough.set_block(210, 210, 30, true);
        rough.set_block(210, 210, 29, true);
        // A shallow pit at (220, 220): the walk no longer pours in, for
        // four down has no way back up. The slab is taken out first - the
        // blocks below are what a pit is floored with.
        rough.set_block(220, 220, 33, false);
        rough.set_block(220, 220, 32, false);
        rough.set_block(220, 220, 37, true);
        rough.set_block(220, 220, 36, true);
        // A deep pit at (230, 230): six down is shut the same way.
        rough.set_block(230, 230, 33, false);
        rough.set_block(230, 230, 32, false);
        rough.set_block(230, 230, 39, true);
        rough.set_block(230, 230, 38, true);
        horde::Horde hh(rough, horde_cfg(256, 256));
        check(hh.distance(200, 200) >= 0, "a pile of two is jumped onto");
        check(hh.distance(210, 210) < 0, "a pile of three is a wall: no dig, no way");
        check(hh.distance(220, 220) < 0, "a pit four deep: the walk keeps its feet");
        check(hh.distance(230, 230) < 0, "a pit six deep is shut to the walk as well");
        // The dead in a pit are still the server's to move: the field is
        // their plan, and with no plan of its own the fallback's straight
        // line names the next leg's step - a fall survived is not a
        // route the walk owes them.
        int px = 0, py = 0;
        check(hh.next_step(220, 220, px, py) && (px != 220 || py != 220),
              "from a pit off the field, the fallback names a way");
        check(hh.next_step(230, 230, px, py) && (px != 230 || py != 230),
              "from the deep pit, the straight line walks it out");
    }

    // The core's column, three blocks standing on it: its top is a walkable
    // cell nothing steps onto - no jump reaches it now - but the field has
    // to spread from the cells beside it - that is where the horde stops to
    // bite.
    {
        SolidMap marked;
        plain(marked);
        for (int k = 1; k <= 3; ++k)
            marked.set_block(256, 256, GROUND_TOP - k, true);
        horde::Horde hh(marked, horde_cfg(256, 256));
        check(hh.distance(256, 256) >= 0, "the column's top is on the field");
        check(hh.distance(255, 256) == 1, "beside the column is one step from the core");
        check(hh.distance(255, 255) == 2 || hh.distance(257, 256) == 2,
              "the field spreads around the column");
        check(hh.distance(32, 32) > 400, "the far corners are still a long walk");
    }
}

// v178ap: the advice tells the crossing itself: the two-block tread the
// field prices one jump is handed back with the surface it runs onto,
// so the walker under it takes the lip the moment it arrives. The lane
// the walls shut faces its stair, the fall stays walked straight off,
// the pile of three keeps the walk it has always been, and the silence
// of the pit and of the core leaves tz as it came in.
void test_vault() {
    std::printf("[vault] the crossing surface is handed back\n");
    SolidMap map;
    plain(map);
    // A lane shut by three-high walls both sides, a stair of two at
    // its head: the walls hedged the corridor, the tread prices a whole
    // arc, and the priced advice walks the lane out the way it came.
    for (int i = 195; i <= 200; ++i) {
        map.set_block(i, 255, 31, true);
        map.set_block(i, 255, 30, true);
        map.set_block(i, 255, 29, true);
        map.set_block(i, 257, 31, true);
        map.set_block(i, 257, 30, true);
        map.set_block(i, 257, 29, true);
    }
    map.set_block(200, 256, 31, true);
    map.set_block(200, 256, 30, true);
    // A pile of three off the road: what a jump never clears. And a pit
    // four deep: ground the walk owes no opinion on, the fallback alone.
    map.set_block(100, 100, 31, true);
    map.set_block(100, 100, 30, true);
    map.set_block(100, 100, 29, true);
    map.set_block(220, 220, 33, false);
    map.set_block(220, 220, 32, false);
    map.set_block(220, 220, 37, true);
    map.set_block(220, 220, 36, true);
    horde::Horde h(map, horde_cfg(256, 256));
    int tx = -1, ty = -1, tz = 999;
    check(h.next_step(199, 256, 32, tx, ty, tz) && tx == 198 && ty == 256
              && tz == 32,
          "the priced step sends the shut lane out the way it came");
    check(h.next_step(200, 256, 30, tx, ty, tz) && tx == 201 && ty == 256
              && tz == 32,
          "off the tread the crossing is a fall, not a climb");
    check(h.distance(100, 100, 29) < 0,
          "the top of a pile of three is no floor of the walk");
    tz = 12345;
    check(h.next_step(220, 220, 36, tx, ty, tz) && tz == 12345,
          "the fallback of the pit prices no climb, and keeps tz as it was");
    tz = 54321;
    check(!h.next_step(256, 256, 32, tx, ty, tz) && tz == 54321,
          "at the core no step helps, and tz keeps what it came in with");
}

void test_stair_price() {
    std::printf("[stair] the tall tread against the stair of one\n");
    // A plateau four high, shut on all sides, the core on its top: the
    // LostValley pyramid shape. Twin stairs side by side on its west
    // face - the stair of one, three treads deep, and beside it the
    // tall tread of two. The climb of two costs more than the arc it
    // rides, so the stair of one is the way the walker takes.
    {
        SolidMap map;
        plain(map);
        for (int x = 180; x <= 232; ++x)
            for (int y = 200; y <= 312; ++y)
                for (int z = 28; z <= 31; ++z)
                    map.set_block(x, y, z, true);
        for (int i = 0; i < 3; ++i)
            for (int z = 28; z <= 30 - i; ++z)
                map.set_block(180 + i, 257, z, false);
        for (int z = 28; z <= 29; ++z)
            map.set_block(180, 255, z, false);
        horde::Horde h(map, horde_cfg(206, 256));
        int x = 150, y = 256, z = 32, grey = 0, black = 0, climbs = 0;
        for (int steps = 0; (x != 206 || y != 256) && steps < 400; ++steps) {
            int tx, ty, tz;
            if (!h.next_step(x, y, z, tx, ty, tz)) break;
            if (tx == 180 && ty == 257) ++grey;
            if (tx == 180 && ty == 255) ++black;
            if (z - tz == horde::Config::kWalkClimb) ++climbs;
            x = tx;
            y = ty;
            z = tz;
        }
        check(x == 206 && y == 256,
              "the walker reaches the core on the plateau");
        check(grey == 1 && black == 0,
              "the stair of one is the stair taken");
        check(climbs == 0,
              "no tread of the walk is a climb of two blocks");
    }
    // The same plateau, the tall stair alone: carved without its rival
    // it is the only way up, and the walk takes it as ever.
    {
        SolidMap map;
        plain(map);
        for (int x = 180; x <= 232; ++x)
            for (int y = 200; y <= 312; ++y)
                for (int z = 28; z <= 31; ++z)
                    map.set_block(x, y, z, true);
        for (int z = 28; z <= 29; ++z)
            map.set_block(180, 255, z, false);
        horde::Horde h(map, horde_cfg(206, 256));
        int x = 150, y = 256, z = 32, black = 0, climbs = 0;
        for (int steps = 0; (x != 206 || y != 256) && steps < 400; ++steps) {
            int tx, ty, tz;
            if (!h.next_step(x, y, z, tx, ty, tz)) break;
            if (tx == 180 && ty == 255) ++black;
            if (z - tz == horde::Config::kWalkClimb) ++climbs;
            x = tx;
            y = ty;
            z = tz;
        }
        check(x == 206 && y == 256,
              "the tall stair serves where it is the only stair");
        check(black == 1 && climbs == 2,
              "its two treads are climbed, two blocks a tread");
        int tx = -1, ty = -1, tz = 999;
        check(h.next_step(179, 255, 32, tx, ty, tz) && tx == 180 && ty == 255
                  && 32 - tz == horde::Config::kWalkClimb,
              "its first tread hands its climb back, two above the feet");
    }
}

void test_spawn_named() {
    std::printf("[spawn] a named site anchors on its own ground\n");
    // A map that named its sites: the anchor of each is the named ground
    // itself, never again the frontier a 200-ring sweep holds out to the
    // border of the world - the point only named the centre of the
    // reading before, and the reading walked away from it.
    {
        SolidMap map;
        plain(map);
        horde::Config cfg = horde_cfg(256, 256);
        cfg.spawn_named = true;
        cfg.spawn_count = 2;
        cfg.spawn_x[0] = 120; cfg.spawn_y[0] = 256; cfg.spawn_z[0] = 32;
        cfg.spawn_x[1] = 240; cfg.spawn_y[1] = 100; cfg.spawn_z[1] = 32;
        horde::Horde h(map, cfg);
        h.scout();
        int x = -1, y = -1;
        check(h.corner_anchor(0, x, y) && x == 120 && y == 256,
              "the first named site anchors on the point it named");
        check(h.corner_anchor(1, x, y) && x == 240 && y == 100,
              "the second named site anchors on its own ground too");
        check(h.distance(120, 256) >= 0 && h.distance(240, 100) >= 0,
              "both named sites stand on the walk itself");
    }
    // A site named inside walls the legs cannot cross: the sweep stops on
    // the first ring that stands, the field ground just outside the
    // sealed pocket, and never walks away out to the border of the map.
    {
        SolidMap map;
        plain(map);
        for (int x = 297; x <= 303; ++x)
            for (int y = 297; y <= 303; ++y)
                if (x == 297 || x == 303 || y == 297 || y == 303)
                    for (int z = 28; z <= 31; ++z)
                        map.set_block(x, y, z, true);
        horde::Config cfg = horde_cfg(256, 256);
        cfg.spawn_named = true;
        cfg.spawn_count = 1;
        cfg.spawn_x[0] = 300; cfg.spawn_y[0] = 300; cfg.spawn_z[0] = 32;
        horde::Horde h(map, cfg);
        h.scout();
        int x = -1, y = -1;
        check(h.corner_anchor(0, x, y),
              "the pocketed site anchors on the walk it can stand");
        const int gx = x > 300 ? x - 300 : 300 - x;
        const int gy = y > 300 ? y - 300 : 300 - y;
        check(x >= 0 && y >= 0 && h.distance(x, y) >= 0
                  && (gx > 3 || gy > 3),
              "its anchor stands outside the sealed pocket");
        check(x >= 0 && y >= 0 && gx <= 4 && gy <= 4,
              "its anchor stands on the nearest ring, not the border");
    }
    // A reached roof must not steal the birth: the rooms under it stand
    // sealed to the legs, a stair outside gives the walk its short way
    // up onto the roof, and the wave still rises on the layer the name
    // asked of, inside - the leader on the named point, the pack on the
    // rooms around it.
    {
        SolidMap map;
        plain(map);
        for (int x = 296; x <= 304; ++x)
            for (int y = 296; y <= 304; ++y) {
                const int fx = x > 300 ? x - 300 : 300 - x;
                const int fy = y > 300 ? y - 300 : 300 - y;
                if (fx != 4 && fy != 4) continue;  // the walls, no door
                for (int z = 26; z <= 31; ++z)
                    map.set_block(x, y, z, true);
            }
        for (int x = 296; x <= 304; ++x)
            for (int y = 296; y <= 304; ++y) {
                map.set_block(x, y, 24, true);
                map.set_block(x, y, 25, true);
            }
        for (int z = 30; z <= 33; ++z)
            map.set_block(307, 300, z, true);
        for (int z = 28; z <= 33; ++z)
            map.set_block(306, 300, z, true);
        for (int z = 26; z <= 33; ++z)
            map.set_block(305, 300, z, true);
        horde::Config cfg = horde_cfg(256, 256);
        cfg.spawn_named = true;
        cfg.spawn_count = 1;
        cfg.spawn_x[0] = 300; cfg.spawn_y[0] = 300; cfg.spawn_z[0] = 32;
        horde::Horde h(map, cfg);
        h.restart(0.0);
        h.tick(10.5);
        std::vector<horde::SpawnAt> due;
        h.take_due(due, 100.0);
        check(due.size() == 5, "the named site raises its whole pack");
        bool on_asked = true, inside = true;
        for (const horde::SpawnAt& s : due) {
            if (s.z != GROUND_TOP) on_asked = false;
            if (s.x < 297 || s.x > 303 || s.y < 297 || s.y > 303)
                inside = false;
        }
        check(on_asked,
              "every walker rises on the layer the name asked of");
        check(inside,
              "every walker rises inside, not on the reached roof");
        check(!due.empty() && due.front().x == 300 && due.front().y == 300
                  && due.front().z == GROUND_TOP,
              "the leader rises on the named point itself");
    }
    // The unnamed world keeps its own reading: the frontier of the
    // corner reach - the border of the map itself on an open plain.
    {
        SolidMap map;
        plain(map);
        horde::Horde h(map, horde_cfg(256, 256));
        h.scout();
        int x = -1, y = -1;
        check(h.corner_anchor(0, x, y) && x == 0 && y == 0,
              "the unnamed corner still anchors at its frontier");
    }
}

void test_veto() {
    std::printf("[veto] the last gap cannot close by itself\n");
    // One ring, two gaps: closing one still leaves the other.
    {
        SolidMap map;
        plain(map);
        ring(map, Ring{256, 256, 10, 266, 256, 256, 246});
        horde::Horde h(map, horde_cfg(256, 256));
        check(h.distance(32, 32) >= 0, "the plain crosses the ring through its gaps");
        check(h.can_build(266, 256, 31), "closing one of two gaps is allowed");
    }
    // One ring, one gap, dead in the cross the four corners' reach leaves
    // empty: the gap is the only way in, and the veto refuses its brick -
    // with the gap held out, no corner's reach stands anywhere on the
    // walk.
    {
        SolidMap map;
        plain(map);
        ring(map, Ring{256, 256, 6, 262, 256});
        horde::Horde h(map, horde_cfg(256, 256));
        check(h.can_build(258, 258, 31), "a block well inside the ring is allowed");
        check(!h.can_build(262, 256, 31), "the last gap refuses its closing block");
        check(!h.can_build(262, 256, 0) && !h.can_build(262, 256, 63),
              "the veto asks the same question at every height");
    }
    // The ring shut for real: no climb, no dig, the walk is sealed out of
    // the core, and the map was sealed before the block, so the veto fails
    // open and the fallback walks a straight line.
    {
        SolidMap map;
        plain(map);
        ring(map, Ring{256, 256, 6, -1, -1});
        horde::Horde h(map, horde_cfg(256, 256));
        h.on_map_edited();
        h.tick(2.0);  // past the debounce: the recomputed field is shut out
        check(h.distance(270, 256) < 0, "the shut ring walls the core off");
        check(h.can_build(270, 256, 31), "with the core already sealed, the veto fails open");
        int tx = 0, ty = 0;
        check(h.next_step(270, 256, tx, ty) && (tx != 270 || ty != 256),
              "off the shut field, the fallback still moves a leg");
    }
}

void test_waves() {
    std::printf("[waves] the state machine walks its rounds\n");
    SolidMap map;
    plain(map);
    horde::Horde h(map, horde_cfg(256, 256));
    h.restart(0.0);
    check(h.phase() == horde::Phase::Prep, "restart arms Prep");
    h.tick(5.0);
    check(h.phase() == horde::Phase::Prep, "half the prep is still Prep");
    h.tick(10.5);
    check(h.phase() == horde::Phase::Wave && h.wave() == 1, "prep over: wave one");
    std::vector<horde::SpawnAt> due;
    check(h.take_due(due, 10.7), "the wave has zombies waiting");
    check(due.size() == 1 && h.zombies_up() == 1, "the first is due and counted");
    h.take_due(due, 100.0);
    check(h.zombies_up() == 5, "wave one is one pack of five");
    bool on_ground = true;
    bool one_corner = true;
    for (const horde::SpawnAt& s : due) {
        if (s.z != GROUND_TOP) on_ground = false;  // due holds the last four
        if (s.corner != h.ranked_corner(0)) one_corner = false;
    }
    check(on_ground, "spawn cells stand on the ground they were picked for");
    check(one_corner && h.ranked_corner(0) == 3,
          "one point open: every zombie walks out of the point the walk ranks first");
    check(!h.wave_cleared(), "five walking is not a cleared wave");
    h.on_core_damaged(50);
    check(h.phase() == horde::Phase::Wave && h.core_hp() == 100, "the core takes a hit");
    for (int i = 0; i < 5; ++i) h.on_zombie_killed();
    check(h.wave_cleared(), "all five down clears the wave");
    h.tick(100.5);
    check(h.phase() == horde::Phase::Intermission, "a cleared wave turns to Intermission");
    check(h.core_hp() == 125, "the core mends by 25 between waves");
    h.tick(106.0);
    check(h.phase() == horde::Phase::Wave && h.wave() == 2, "intermission over: wave two");
    h.take_due(due, 200.0);
    check(h.zombies_up() == 10, "wave two is two packs of five");
    for (int i = 0; i < 10; ++i) h.on_zombie_killed();
    h.tick(200.5);
    check(h.phase() == horde::Phase::Victory, "the last wave cleared is Victory");
    h.restart(1000.0);
    check(h.phase() == horde::Phase::Prep && h.wave() == 0, "restart resets the rounds");
    h.tick(1010.5);
    check(h.phase() == horde::Phase::Wave, "the rounds go on after a restart");
    h.on_core_damaged(999);
    check(h.phase() == horde::Phase::Defeat && h.core_hp() == 0, "the core gone is Defeat");
    h.clear_wave(2000.0);
    check(h.phase() == horde::Phase::Intermission && h.core_hp() == 150,
          "a wiped wave mends the core whole and calls the defeat off");
    h.tick(2006.0);
    check(h.phase() == horde::Phase::Wave && h.wave() == 2,
          "the rounds go on after the call-off");
}

void test_growth() {
    std::printf("[growth] the wave size curve\n");
    horde::Config cfg;
    check(cfg.wave_zombies(1) == 5, "wave one: the first five");
    check(cfg.wave_zombies(2) == 10, "wave two: five more on the way");
    check(cfg.wave_zombies(5) == 25, "wave five: the ramp of five ends");
    check(cfg.wave_zombies(10) == 80, "wave ten: ten points, packs cut deep");
    check(cfg.wave_zombies(11) == 100, "wave eleven: the ramp of ten stacks");
    check(cfg.wave_zombies(30) == 250, "far out: capped at the world's budget");
}

void test_route() {
    std::printf("[route] the way in, painted cell by cell\n");
    // The plain: from corner zero's anchor the route runs downhill to the
    // core, one cell a step, and is exactly the walk's distance long.
    {
        SolidMap map;
        plain(map);
        horde::Horde h(map, horde_cfg(256, 256));
        h.restart(0.0);
        h.tick(10.5);  // prep over: the wave is placed, its anchor kept
        int ax = -1, ay = -1;
        check(h.corner_anchor(0, ax, ay), "corner zero keeps its anchor");
        // The fronts the paintings ask by name: one a point, the anchored
        // ranks the wave's own number takes, never more than the map holds.
        check(h.wave_fronts(1) == 1, "wave one walks one front");
        check(h.wave_fronts(4) == 4, "wave four walks the four anchored fronts");
        check(h.wave_fronts(6) == 4, "no more fronts than the anchored four");
        check(h.wave_fronts(20) == 4, "past every cap the fronts hold at the anchors");
        check(h.distance(ax, ay) >= 0, "the anchor stands on the field");
        std::vector<horde::RouteCell> cells;
        check(h.trace_route(ax, ay, cells), "the route traces from the anchor");
        check(!cells.empty() && cells.front().x == ax && cells.front().y == ay,
              "the route starts on the anchor");
        check(static_cast<int>(cells.size()) == h.distance(ax, ay) + 1,
              "the route is exactly the walk's distance long");
        bool steps_ok = true;
        for (size_t i = 1; i < cells.size(); ++i) {
            const int dx = cells[i].x - cells[i - 1].x;
            const int dy = cells[i].y - cells[i - 1].y;
            if (std::abs(dx) + std::abs(dy) != 1) steps_ok = false;
            if (h.distance(cells[i].x, cells[i].y) !=
                h.distance(cells[i - 1].x, cells[i - 1].y) - 1)
                steps_ok = false;
        }
        check(steps_ok, "every step of the route is one downhill cell");
        check(h.distance(cells.back().x, cells.back().y) == 0,
              "the route ends at the core");
        check(!h.trace_route(-1, 0, cells), "off the map, no route traces");
    }
    // A ring with one gap around the core: the only way in passes the gap,
    // and the traced route goes through it.
    {
        SolidMap map;
        plain(map);
        ring(map, Ring{256, 256, 10, 266, 256});
        horde::Horde h(map, horde_cfg(256, 256));
        h.restart(0.0);
        h.tick(10.5);
        int ax = -1, ay = -1;
        check(h.corner_anchor(0, ax, ay), "the gap ring still leaves an anchor");
        std::vector<horde::RouteCell> cells;
        check(h.trace_route(ax, ay, cells), "the route traces to the ringed core");
        bool passes_gap = false;
        for (const horde::RouteCell& c : cells)
            if (c.x == 266 && c.y == 256) passes_gap = true;
        check(passes_gap, "the route enters through the gap");
    }
    // The arch: walls the walk cannot climb, pierced by a tunnel whose
    // lintel leaves the plain's floor below it - the wall's cap is one
    // standing surface, the tunnel's floor another, and the way in runs
    // under it. The treads of the route through the gate carry the
    // tunnel's floor height, never the wall's cap, so the paint lands
    // where the feet go; and one block of a tunnel wide enough changes
    // nothing.
    {
        SolidMap map;
        plain(map);
        for (int y = 250; y <= 262; ++y)
            for (int x = 250; x <= 262; ++x) {
                const bool frame = (x == 250 || x == 262 || y == 250 || y == 262);
                if (!frame) continue;
                const bool gate = (x == 262 && y >= 254 && y <= 258);
                if (!gate) {
                    // Walls three high on the plain: a climb the legs refuse.
                    map.set_block(x, y, WALL_TOP, true);
                    map.set_block(x, y, WALL_TOP + 1, true);
                    map.set_block(x, y, WALL_TOP + 2, true);
                } else {
                    // The gate: a lintel above the plain - three open
                    // voxels under it, the road the three-tall dead take.
                    map.set_block(x, y, WALL_TOP - 2, true);
                    map.set_block(x, y, WALL_TOP - 1, true);
                }
            }
        horde::Horde h(map, horde_cfg(256, 256));
        check(h.distance(270, 256) >= 0, "the tunnel is a way in the field takes");
        check(h.distance(260, 256) >= 0, "the field stands inside the walls too");
        check(h.can_build(262, 256, 31),
              "one column of a five wide gate closes nothing");
        h.restart(0.0);
        h.tick(10.5);
        // Every corner walks in through the one gate: the south east's
        // route crosses it on its way to the core.
        int ax = -1, ay = -1;
        check(h.corner_anchor(3, ax, ay), "corner three anchors outside the walls");
        check(ax > 262 && ay > 262, "the anchor stands beyond the gate, not inside");
        std::vector<horde::RouteCell> cells;
        check(h.trace_route(ax, ay, cells), "the route traces under the arch");
        bool through = false, on_floor = true;
        for (const horde::RouteCell& c : cells)
            if (c.x == 262 && c.y >= 254 && c.y <= 258) {
                through = true;
                if (c.z != GROUND_TOP) on_floor = false;
            }
        check(through && on_floor, "the wave enters by the tunnel, walking its floor");
        check(h.placed() == 5, "the wave stands up outside the walls");
    }
    // The dead stand three tall: the two open voxels the walk once
    // demanded overhead let the paper count floors their bodies could
    // never stand under, and the packs the plan fed there piled at the
    // mouths of low tunnels, wave on wave. A lintel that leaves the
    // third voxel open counts the floor again: the walk only promises
    // what a body can take.
    {
        SolidMap map;
        plain(map);
        map.set_block(260, 256, GROUND_TOP - 3, true);  // a lintel leaving
                                                        // two open beneath
        horde::Horde h(map, horde_cfg(256, 256));
        check(h.distance(260, 256) < 0,
              "a two-voxel gap is no standing room for the three-tall dead");
        map.set_block(260, 256, GROUND_TOP - 3, false);
        horde::Horde h2(map, horde_cfg(256, 256));
        check(h2.distance(260, 256) >= 0,
              "the third voxel cleared: the floor a body fits counts");
    }
    // Two walls the walk cannot cross: a cell shut off the field traces
    // nothing, and a core sealed away still anchors its corner - wherever
    // the field still stands, in the ring's inside or out on the plain.
    {
        SolidMap map;
        plain(map);
        ring(map, Ring{100, 100, 6, -1, -1});  // the core sealed in
        horde::Horde h(map, horde_cfg(100, 100));
        std::vector<horde::RouteCell> cells;
        check(!h.trace_route(130, 100, cells), "a cell off the field traces nothing");
        h.restart(0.0);
        h.tick(10.5);
        int ax = 0, ay = 0;
        check(h.corner_anchor(0, ax, ay), "a sealed core still anchors its corner");
        check(std::max(std::abs(ax - 100), std::abs(ay - 100)) <= 5,
              "the anchor stands where the field still does: inside the ring");
        check(h.ranked_corner(0) == 0,
              "the only corner on the field is the one the wave takes");
    }
    // Nothing anywhere: a core sealed away and every corner far beyond the
    // field's reach - no anchor for anyone, and the wave stands down
    // instead of clearing itself in silence. The spade is the problem's
    // own: with the licence on, the sealed town anchors on the spade's
    // frontier instead, so this round reads the legs alone.
    {
        SolidMap map;
        plain(map);
        ring(map, Ring{100, 100, 6, -1, -1});
        horde::Config cfg = horde_cfg(100, 100);
        cfg.spawn_x[0] = 460; cfg.spawn_y[0] = 460;  // the near corner gone too
        cfg.dig = false;  // the licence off: no spade, no frontier
        horde::Horde h(map, cfg);
        h.restart(0.0);
        h.tick(10.5);
        check(h.placed() == 0, "every corner out of the walk's reach: nothing stands up");
        std::vector<horde::SpawnAt> due;
        check(!h.take_due(due, 100.0) && h.zombies_up() == 0, "nothing streams");
        check(h.wave_cleared(), "the wave stands down");
    }
    {
        SolidMap map;
        plain(map);
        // A wall 200 cells out around corner zero: the search never sees a
        // cell of the field, so the corner stays unanchored - and the wave
        // turns to a corner the walk can truly reach instead of standing
        // down.
        ring(map, Ring{32, 32, 200, -1, -1});
        horde::Horde h(map, horde_cfg(256, 256));
        h.restart(0.0);
        h.tick(10.5);
        int ax = 0, ay = 0;
        check(!h.corner_anchor(0, ax, ay), "a corner the field never reaches holds no anchor");
        check(h.ranked_corner(0) == 3,
              "a sealed corner is passed over for one the walk reaches");
        check(h.placed() == 5, "the wave stands up elsewhere");
        std::vector<horde::SpawnAt> due;
        check(h.take_due(due, 100.0), "the wave still streams");
        bool from_rank_three = true;
        for (const horde::SpawnAt& s : due)
            if (s.corner != 3) from_rank_three = false;
        check(from_rank_three && h.zombies_up() == 5,
              "the whole wave walks in from the corner the walk found");
        check(!h.wave_cleared(), "a wave that found its way in is not cleared");
    }
    // A mesa twenty high with the core on top: the walk is reversible, so
    // the cliff walls both ways and keeps the field on the mesa's own top.
    // The dead still fear no landing, but the plan never pours them off
    // one - and no corner's reach holds a cell of the walk, so the wave
    // stands down until the map itself is opened. The legs are the only
    // walker in this round: with the spade licensed, the mesa's cliff is
    // climbable at a price and the wave would anchor past it.
    {
        SolidMap map;
        plain(map);
        for (int y = 255; y <= 257; ++y)
            for (int x = 255; x <= 257; ++x)
                for (int k = 1; k <= 20; ++k)
                    map.set_block(x, y, GROUND_TOP - k, true);
        horde::Config cfg = horde_cfg(256, 256);
        cfg.dig = false;  // the licence off: the cliff holds
        horde::Horde h(map, cfg);
        check(h.distance(258, 256) < 0,
              "a cliff of twenty is a wall both ways down");
        check(h.distance(256, 256) == 0 && h.distance(255, 256) == 1,
              "the field stands on the mesa's own top");
        h.restart(0.0);
        h.tick(10.5);
        int ax = -1, ay = -1;
        check(!h.corner_anchor(0, ax, ay), "a shut mesa anchors no corner");
        check(h.placed() == 0, "nothing stands up anywhere around it");
        std::vector<horde::SpawnAt> due;
        check(!h.take_due(due, 100.0) && h.zombies_up() == 0, "nothing streams");
        check(h.wave_cleared(), "the wave stands down");
    }
    // The perch the wave refuses: a tower the field cannot climb stands on
    // the corner's asked-for centre, and those legs have one standing
    // ground left in their reach - the mesa's own top, the cliff keeping
    // the field off the plain. The wave anchors there, a step from the
    // tower - never the tower itself - for begun on a perch it lands in
    // foliage the legs cannot cross.
    {
        SolidMap map;
        plain(map);
        for (int y = 255; y <= 257; ++y)
            for (int x = 255; x <= 257; ++x)
                for (int k = 1; k <= 20; ++k)
                    map.set_block(x, y, GROUND_TOP - k, true);
        for (int k = 1; k <= 24; ++k)
            map.set_block(258, 258, GROUND_TOP - k, true);  // unclimbable
        horde::Config cfg = horde_cfg(256, 256);
        cfg.spawn_x[0] = 258;
        cfg.spawn_y[0] = 258;
        horde::Horde h(map, cfg);
        h.restart(0.0);
        h.tick(10.5);
        int ax = -1, ay = -1;
        check(h.corner_anchor(0, ax, ay), "the corner anchors beside its tower");
        check(ax == 257 && ay == 257,
              "the anchor is the mesa's top beside the tower, never the tower");
    }
}

// The spade's own walk: a second field priced dear, consulted only on
// the licences of the pinned, and a sealed town that still anchors,
// raises and traces its waves on it.
void test_dig() {
    std::printf("[dig] the spade's walk, priced for the pinned\n");
    // The open plain: the legs walk it free, and the spade prices it at
    // the legs' own rate - a field with no crossing to save saves
    // nothing, and a shortcut logic on open ground would see ghosts.
    {
        SolidMap map;
        plain(map);
        horde::Horde h(map, horde_cfg(256, 256));
        check(h.distance(210, 210) >= 0, "the plain's walk runs free");
        check(h.dig_distance(210, 210, GROUND_TOP) ==
                  h.distance(210, 210, GROUND_TOP),
              "no gain on the plain: the spade walk prices it the same");
    }
    // The sealed ring: the legs are shut out, the spade's way crosses its
    // wall, the wave still stands up, and the advice at the wall's foot
    // names the crossing's own top - a climb of three, the signal to dig.
    {
        SolidMap map;
        plain(map);
        ring(map, Ring{256, 256, 6, -1, -1});
        horde::Horde h(map, horde_cfg(256, 256));
        check(h.distance(270, 256) < 0, "the legs' walk is shut out");
        check(h.dig_distance(270, 256, GROUND_TOP) >= 0,
              "with the legs silent, the spade walk still names a way home");
        int tx = 0, ty = 0, tz = 0;
        check(h.next_dig_step(270, 256, GROUND_TOP, tx, ty, tz) &&
                  (tx != 270 || ty != 256) && tz == GROUND_TOP,
              "mid-field, the dig advice walks the plain toward the wall");
        check(h.next_dig_step(263, 256, GROUND_TOP, tx, ty, tz) &&
                  tx == 262 && ty == 256 && tz == WALL_TOP,
              "at the wall's foot it names the crossing's own top");
        // The sealed town still catches its waves: the anchors stand on
        // the spade's own frontier.
        h.restart(0.0);
        h.tick(10.5);
        check(h.phase() == horde::Phase::Wave && h.placed() > 0,
              "a walled town still stands its wave up");
        check(h.wave_fronts(1) == 1 && h.wave_fronts(4) == 4,
              "the sealed town's fronts are named for the painting");
        int ax = -1, ay = -1;
        check(h.corner_anchor(0, ax, ay) && ax >= 0 && ay >= 0,
              "the sealed town's wave anchors at the border");
        std::vector<horde::RouteCell> cells;
        check(h.trace_dig(ax, ay, cells) && !cells.empty(),
              "the spade's route traces to the core");
        bool over_the_wall = false;
        for (const horde::RouteCell& rc : cells)
            if (rc.z < GROUND_TOP) over_the_wall = true;
        check(over_the_wall, "the traced way crosses over the wall's top");
        // The licence off: the same sealed town raises nothing.
        horde::Config off_cfg = horde_cfg(256, 256);
        off_cfg.dig = false;
        horde::Horde off(map, off_cfg);
        check(off.dig_distance(270, 256, GROUND_TOP) >= 0,
              "the licence off: the counsel stands, the hand is stayed");
        off.restart(0.0);
        off.tick(10.5);
        check(off.phase() == horde::Phase::Wave && off.placed() == 0,
              "the licence off: the sealed town raises nothing");
        check(off.wave_fronts(1) == 0,
              "the licence off: no front is named, nothing is painted");
    }
    // The buried core: sealed in a room dug out of a solid mass under
    // the plain, its ceiling closed. The blank world without the marker
    // seeds the whole column for its own, and the roof above the core
    // reads distance zero - the dead gather on it and nothing ever
    // opens. The marker names the core's top layer, and only the floors
    // at that level, and the ones that truly stand beside them, hold
    // the seed: the roof is a world of rock away, silent to the legs,
    // and the spade's field hands the gathered a crossing down through
    // it - the core's own column, named at the marker's level, the
    // fall across sealed rock opened by the spade top down, a voxel a
    // stroke. Once the shaft stands open the advice keeps its name and
    // the fall is honest air the walker takes - and the buried room
    // raises its wave all the same, anchored on the spade's frontier,
    // the plain above.
    {
        SolidMap map;
        plain(map);
        for (int y = 253; y <= 259; ++y)
            for (int x = 253; x <= 259; ++x)
                for (int z = 34; z <= 39; ++z)
                    map.set_block(x, y, z, true);  // the mass the room hides in
        for (int y = 255; y <= 257; ++y)
            for (int x = 255; x <= 257; ++x)
                for (int z = 34; z <= 38; ++z)
                    map.set_block(x, y, z, false);  // the room: five open
                                                  // voxels, its floor at 39
        map.set_block(256, 256, 37, true);   // the core's own blocks kept,
        map.set_block(256, 256, 38, true);   // the marker names its top, 37 -
                                             // headroom three deep, a floor the
                                             // three-tall dead could stand on
        horde::Config cfg = horde_cfg(256, 256);
        cfg.core_top_z = 37;
        horde::Horde h(map, cfg);
        check(h.distance(270, 256, GROUND_TOP) < 0,
              "a named marker buried: the ground above is no place at the core");
        check(h.distance(255, 256, GROUND_TOP) < 0,
              "the roof above a buried core: the legs are silent");
        check(h.dig_distance(255, 256, GROUND_TOP) > 0,
              "the spade walk keeps its own count on the roof");
        check(h.dig_distance(256, 256, 37) == 0,
              "the marker reads zero on the spade walk too");
        int tx = -1, ty = -1, tz = -1;
        check(h.next_dig_step(255, 256, GROUND_TOP, tx, ty, tz) &&
                  tx == 256 && ty == 256 && tz == 37,
              "the advice on the roof names the core's own column and level");
        check(map.solid(tx, ty, GROUND_TOP),
              "the promised fall crosses sealed rock: the spade opens it");
        // The shaft worked the way the spade works it, top down through
        // the ceiling: the crossing stands in honest air now, and the
        // advice keeps its name.
        for (int z = GROUND_TOP; z <= 36; ++z)
            map.set_block(256, 256, z, false);
        h.on_map_edited();
        h.tick(2.0);
        check(h.next_dig_step(255, 256, GROUND_TOP, tx, ty, tz) &&
                  tx == 256 && ty == 256 && tz == 37,
              "through open air the advice keeps naming the room below");
        // The buried room raises its wave: anchored on the spade's own
        // frontier, the plain above.
        h.restart(0.0);
        h.tick(10.5);
        check(h.placed() == 5, "the buried room still stands its wave up");
        std::vector<horde::SpawnAt> due;
        check(h.take_due(due, 100.0) && h.zombies_up() == 5,
              "the buried wave streams from the plain");
    }
}

// The dealt asking: with roam_spawns the waves redraw their asked-for
// centres round the town, spread in shuffled compass sectors - and the
// reading keeps its own rules wherever the asking moved: the anchor is
// the walk's frontier within the asking's reach, far from the core, and
// the wave still rises on the ground it was dealt. The Horde's default
// stays off, so the rounds above read their fixed corners as ever.
void test_roam() {
    std::printf("[roam] the quarters dealt as each wave falls, spread round the town\n");
    SolidMap map;
    plain(map);
    horde::Config cfg = horde_cfg(256, 256);
    cfg.roam_spawns = true;
    horde::Horde h(map, cfg);
    h.restart(0.0);
    h.tick(10.5);
    check(h.phase() == horde::Phase::Wave && h.wave() == 1,
          "the dealt wave begins on time");
    int ax = -1, ay = -1;
    check(h.corner_anchor(h.ranked_corner(0), ax, ay),
          "the dealt asking still anchors a way in");
    check(ax >= 0 && ay >= 0 && ax < SolidMap::SIZE_X && ay < SolidMap::SIZE_Y,
          "the dealt anchor stands on the map");
    check(h.distance(ax, ay) > 100,
          "the dealt way in is far from the core, whatever quarter drew it");
    // The round wave one stood in, every corner of it, kept for the
    // comparison the fall must turn.
    int first_x[horde::Config::kSpawnSites];
    int first_y[horde::Config::kSpawnSites];
    int anchored = 0;
    for (int c = 0; c < horde::Config::kSpawnSites; ++c) {
        first_x[c] = -1;
        first_y[c] = -1;
        if (h.corner_anchor(c, first_x[c], first_y[c])) ++anchored;
    }
    check(anchored > 0, "every quarter of the dealt round anchors a way in");
    std::vector<horde::SpawnAt> due;
    h.take_due(due, 100.0);
    bool on_ground = true;
    for (const horde::SpawnAt& s : due)
        if (s.z != GROUND_TOP) on_ground = false;
    check(!due.empty() && on_ground,
          "the wave rises on the ground it was dealt");
    for (size_t i = 0; i < due.size(); ++i) h.on_zombie_killed();
    check(h.wave_cleared(), "the dealt wave clears like any other");
    h.tick(100.5);
    check(h.phase() == horde::Phase::Intermission, "the dealt wave turns");
    // The next round is dealt at the fall itself, so the intermission
    // the town builds in already reads it: one tick for the mending
    // phase's own scout, then the anchors say the quarters the NEXT wave
    // will stand in - the trail painted between the waves is the true
    // one, not the wave before's.
    h.tick(100.6);
    bool moved = false;
    for (int c = 0; c < horde::Config::kSpawnSites; ++c) {
        int bx = -1, by = -1;
        if (!h.corner_anchor(c, bx, by)) continue;
        if (bx != first_x[c] || by != first_y[c]) moved = true;
    }
    check(moved, "the intermission already reads the next wave's round");
    h.tick(106.0);
    check(h.phase() == horde::Phase::Wave && h.wave() == 2,
          "wave two comes with a deal of its own");
    ax = ay = -1;
    check(h.corner_anchor(h.ranked_corner(0), ax, ay),
          "wave two's own deal anchors a way in");
    check(h.distance(ax, ay) > 100,
          "wave two's way in is far from the core too");
}

}  // namespace

int main() {
    test_field();
    test_vault();
    test_stair_price();
    test_spawn_named();
    test_veto();
    test_waves();
    test_route();
    test_growth();
    test_dig();
    test_roam();
    if (g_failures == 0) {
        std::printf("horde: all checks passed\n");
        return 0;
    }
    std::printf("horde: %d failed\n", g_failures);
    return 1;
}
