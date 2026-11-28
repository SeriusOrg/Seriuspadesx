// The horde's brain, the body of it. See Horde.h for what it is and how the
// veto, the field and the waves decide; the comments here are about the how.

#include "Horde.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>

#include "SolidMap.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace horde {

namespace {

constexpr int kMapW = SolidMap::SIZE_X, kMapH = SolidMap::SIZE_Y;
constexpr int kUnreachable = -1;
// The wall-shy price a crossing pays for every shoulder its landing is
// hedged by: one wall at the shoulder is a squeeze, two a corridor - both
// walked when nothing else runs, never hugged where the map leaves room.
// Two a side keeps a long hug dearer than the few steps out to a road
// with a shoulder free both ways, the way the legs read a corner: a way
// pressed against a wall is where they jam.
constexpr int kWallShy = 2;

size_t cell_index(int x, int y) {
    return static_cast<size_t>(y) * kMapW + x;
}

}  // namespace

// Starts in Prep with the countdown unarmed (zero, so the first tick would
// begin wave one); the host calls restart(now) to arm it. The field is
// computed right away - it is asked for before the first tick.
Horde::Horde(const SolidMap& map, const Config& cfg) : map_(map), cfg_(cfg) {
    core_hp_ = cfg_.core_hp;
    // The asked-for centres begin as the settings name them (the tests'
    // fixed quarters); the first round is dealt over them at birth and a
    // new one as each wave falls, when the config asks for it, so the
    // prep's own trail already points where the wave truly comes from.
    for (int c = 0; c < Config::kSpawnSites; ++c) {
        roam_x_[c] = cfg_.spawn_x[c];
        roam_y_[c] = cfg_.spawn_y[c];
    }
    if (cfg_.roam_spawns) roam();
    recompute();
}

int Horde::distance(int x, int y) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return kUnreachable;
    // The cell's best-reached surface, whatever floor it stands on.
    int best = kUnreachable;
    const int32_t* d = &dist_[cell_index(x, y) * kSurfMax];
    for (int k = 0; k < kSurfMax; ++k)
        if (d[k] != kUnreachable && (best == kUnreachable || d[k] < best))
            best = d[k];
    return best;
}

// --- the standing surfaces ---------------------------------------------------

// A column is not one floor. Every solid run top with kStandHead open voxels
// above it is a surface the dead can stand on - the outside of a wall top is
// one, and so is the floor of a tunnel through it - and the field fills them
// all, so the walk goes under as well as over, and no gate an arch leaves
// open closes a town. The three the dead stand tall is the mercy the plan
// owes them: the pair it once asked counted floors under low ceilings their
// bodies could never take, and the packs the paper fed there piled at the
// mouth, wave on wave. The surfaces are scanned once a recompute and cached;
// a column asked for between an edit and the refresh serves a step a tick
// early, nothing worse.
void Horde::fill_surfaces(int x, int y) {
    const size_t i = cell_index(x, y);
    const uint64_t col = map_.column(x, y);
    int n = 0;
    int prev_solid = 0;
    for (int z = 0; z < SolidMap::SIZE_Z; ++z) {
        const int solid = static_cast<int>((col >> z) & 1u);
        if (solid && !prev_solid) {
            // A solid run opens at z: its top voxel, standable only with
            // kStandHead open voxels above it - the dead stand three
            // tall. z counts down, so the sky sits at the lower z and
            // the band above the surface runs z-kStandHead to z-1.
            if (z >= Config::kStandHead &&
                ((col >> (z - Config::kStandHead)) &
                 ((1ull << Config::kStandHead) - 1u)) == 0) {
                surf_z_[i * kSurfMax + n] = static_cast<int16_t>(z);
                if (++n == kSurfMax) break;
            }
        }
        prev_solid = solid;
    }
    surf_n_[i] = static_cast<int8_t>(n);
}

int Horde::surf_count(int x, int y) {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return 0;
    const size_t i = cell_index(x, y);
    if (surf_n_[i] < 0) fill_surfaces(x, y);
    return surf_n_[i];
}

int Horde::surface(int x, int y, int k) {
    if (k < 0 || k >= surf_count(x, y)) return -1;
    return surf_z_[cell_index(x, y) * kSurfMax + k];
}

// The surface the field reaches best: the walk's own floor of the cell,
// what a zombie there stands on and the paint truly runs at. Ties keep
// the lowest slot; -1 when no surface of the cell is on the walk.
int Horde::best_state(int x, int y) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return -1;
    int best = kUnreachable, bk = -1;
    const int32_t* d = &dist_[cell_index(x, y) * kSurfMax];
    for (int k = 0; k < kSurfMax; ++k)
        if (d[k] != kUnreachable && (best == kUnreachable || d[k] < best)) {
            best = d[k];
            bk = k;
        }
    return bk;
}

// The best surface of the hunt: the lowest of the hunt distances the
// column carries. The field is empty until a hunt names a person, and
// no column is asked before one does.
int Horde::best_hunt_state(int x, int y) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return -1;
    if (hunt_dist_.empty()) return -1;
    int best = kUnreachable, bk = -1;
    const int32_t* d = &hunt_dist_[cell_index(x, y) * kSurfMax];
    for (int k = 0; k < kSurfMax; ++k)
        if (d[k] != kUnreachable && (best == kUnreachable || d[k] < best)) {
            best = d[k];
            bk = k;
        }
    return bk;
}

// The dig walk's own two reads of the same: best_dig_state the surface of
// the lowest dig distance, near_dig_state the reached floor that meets
// the feet at z. The dig field reaches everywhere the legs' field does
// and further; its reads are asked only for the advice the legs cannot
// give.
int Horde::best_dig_state(int x, int y) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return -1;
    int best = kUnreachable, bk = -1;
    const int32_t* d = &dig_dist_[cell_index(x, y) * kSurfMax];
    for (int k = 0; k < kSurfMax; ++k)
        if (d[k] != kUnreachable && (best == kUnreachable || d[k] < best)) {
            best = d[k];
            bk = k;
        }
    return bk;
}

int Horde::near_dig_state(int x, int y, int z) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return -1;
    const size_t i = cell_index(x, y);
    const int32_t* dd = &dig_dist_[i * kSurfMax];
    int bk = -1, bdz = 0;
    for (int k = 0; k < kSurfMax; ++k) {
        if (dd[k] == kUnreachable) continue;
        const int dz = std::abs(static_cast<int>(surf_z_[i * kSurfMax + k]) - z);
        if (bk < 0 || dz < bdz || (dz == bdz && dd[k] < dd[bk])) {
            bk = k;
            bdz = dz;
        }
    }
    return bk;
}

// The column's first surface, its topmost floor: the level the anchors
// filter their rings on.
int Horde::ground(int x, int y) {
    return surface(x, y, 0);
}

// A body's standing room, the host's own measure of it. The walk asks
// kStandHead open voxels of a floor - the three the dead stand tall - and
// half a block wide either way, and one raised where that does not fit is
// one raised inside a wall - the pocket a cliff face keeps, the shelf
// under a lintel, a tunnel's bore - never to leave it. A spawn floor at
// z asks four open voxels above it, its own column's and every side's;
// the map has no voxels past its edges, so a border cell's missing side
// is sky. The field keeps its own three: the walk is a plan; this is a
// birth, and the birth keeps its breath of margin over the plan.
bool Horde::clear_of(int x, int y, int z) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    if (z < 4) return false;  // not the room above, even in the open sky
    const uint64_t window = 0xFull << (z - 4);
    if (map_.column(x, y) & window) return false;
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
        const int nx = x + kDx[k], ny = y + kDy[k];
        if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
        if (map_.column(nx, ny) & window) return false;
    }
    return true;
}

// The clearing a wave is born in: five cells square, four voxels high, the
// whole square open above the floor at z. A body fits where it stands; the
// clearing is the wave's room to be born scattered instead of stacked, and
// to step out of in any direction the road reads. The map has no voxels
// past its edges, so the square of a border cell is open to the sky on
// that side.
bool Horde::box_clear(int x, int y, int z) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    if (z < 4) return false;  // not the room above, even in the open sky
    const uint64_t window = 0xFull << (z - 4);
    for (int ny = y - kRoomHalf; ny <= y + kRoomHalf; ++ny)
        for (int nx = x - kRoomHalf; nx <= x + kRoomHalf; ++nx) {
            if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
            if (map_.column(nx, ny) & window) return false;
        }
    return true;
}

// The ground the last resort lands on: the floor the legs count, opened
// under the birth clearing - a carried pack stands only on honest
// legs-ground, the road it read, never over a gate or down a hole. -1
// when the cell holds no floor the dead may be stood on.
int Horde::stand_floor(int x, int y) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return -1;
    const int k = best_state(x, y);
    if (k < 0) return -1;
    const int z = surf_z_[cell_index(x, y) * kSurfMax + k];
    return box_clear(x, y, z) ? z : -1;
}

// --- the field ---------------------------------------------------------------

// Also see Horde.h. A walk from the core over the map's standing
// surfaces: the distance of every floor the walking dead could reach, the
// floor of a tunnel under its arch as much as the arch's top. Without `dig`
// it is the crossing both the build veto asks with and the zombies
// themselves walk, and the crossing is reversible: kWalkClimb a jump, and
// a jump alone, up as down - what the legs take down they have to take
// back up, so the walk prices no pit as a way in it cannot lead out of;
// the dead themselves still fear no landing, but no plan
// sends them off one. Stairs, ramps and notches are the way, and cliffs
// and pits are walls both ways. With `dig` set it is the richer walk a
// spade-wielding zombie could make: the falls come back, and any climb,
// each block beyond the jump priced at kDigPenalty steps - about the
// time its spade takes that block off - the walk the anchors fall back
// on when the legs' own cannot reach a way in at all.
// `block_x` and `block_y` name one column held out of the walk, the
// veto's candidate; -1 for none. 4-neighbours: corners are where zombies
// jam on pillars, not where they cross. The edges are not all one step
// wide, so the walk is a Dijkstra, not the breadth-first queue it once
// was - and while it prices a crossing it asks after the shoulders: a
// landing hedged by a wall at either side of the step pays kWallShy a
// shoulder, so of two ways the walk takes the one with room both ways
// wherever the map offers it - and it keeps to one level while it is at
// it: a change of floor pays kLevelShy a block, so where the ground
// offers a flat way around its folds, the march takes the flats and
// stays one pack, where a walk that rolls over every ripple comes apart
// on them. Every surface of a cell is a state of its
// own: which floor a walker stands on is decided where it crosses, not
// where it starts.
void Horde::flood(std::vector<int32_t>& dist, int block_x, int block_y,
                   bool dig, int seed_x, int seed_y,
                   const std::vector<int32_t>* held) {
    dist.assign(static_cast<size_t>(kMapW) * kMapH * kSurfMax, kUnreachable);
    // Whether a column is held out of this walk: the one ask's own
    // column, or one of the many a line brings in its sorted set.
    const auto held_out = [&](int x, int y) {
        if (x == block_x && y == block_y) return true;
        if (!held) return false;
        return std::binary_search(held->begin(), held->end(),
                                  static_cast<int32_t>(cell_index(x, y)));
    };
    const int cx = cfg_.core_x, cy = cfg_.core_y;
    if (cx < 0 || cx >= kMapW || cy < 0 || cy >= kMapH) return;
    if (held_out(cx, cy)) return;
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    using QE = std::pair<int32_t, int32_t>;  // (steps so far, state id)
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    const auto state = [&](int x, int y, int k) -> size_t {
        return cell_index(x, y) * kSurfMax + k;
    };
    // The core may carry a column, the marker the clients draw: its ground
    // sits steps above the plain, and no walk starts there. The cells beside
    // it are seeded alongside it, so the field still spreads from the
    // neighbourhood the horde only has to reach to bite.
    auto seed_one = [&](int x, int y, int k, int d) {
        if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return;
        if (held_out(x, y)) return;
        const size_t s = state(x, y, k);
        if (dist[s] == kUnreachable) {
            dist[s] = d;
            open.emplace(d, static_cast<int32_t>(s));
        }
    };
    auto seed = [&](int x, int y, int d) {
        for (int k = 0; k < surf_count(x, y); ++k) seed_one(x, y, k, d);
    };
    // A hunt seeds this differently: the hunted person stands wherever
    // they stand, level or hilltop, every surface of their column
    // joins at zero, and the blanket of their four neighbours joins at
    // one - the walk to them spreads from wherever they stand, and
    // the prices of the crossings make the rest of the way honest.
    if (seed_x >= 0 && seed_y >= 0) {
        seed(seed_x, seed_y, 0);
        for (int k = 0; k < 4; ++k)
            seed(seed_x + kDx[k], seed_y + kDy[k], 1);
    } else {
    // The core's own floors, the ones the walk starts on: every surface
    // of its column, as it always was - save when the host named the
    // marker's top layer: then only the floors at that level do. A cave
    // seals its core under the hill, and the hill's own top is no place
    // at the core: whoever stands on it is a world of rock away from
    // the bite, and no distance may call that spot zero, or the whole
    // hilltop gathers at one that never opens.
    const int core_n = surf_count(cx, cy);
    int core_z[kSurfMax];
    int nq = 0;
    for (int k = 0; k < core_n; ++k) {
        const int z = surf_z_[cell_index(cx, cy) * kSurfMax + k];
        if (cfg_.core_top_z >= 0 && std::abs(z - cfg_.core_top_z) > 1)
            continue;
        core_z[nq++] = z;
        seed_one(cx, cy, k, 0);
    }
    // The cells beside it are seeded alongside it - but only on the
    // floors that truly stand beside the core: a step from one of its
    // own floors, or a fall off it into honest air. The flat world that
    // has no word for the marker's level keeps the blanket it always
    // had, every storey of its four neighbours; a cave keeps the
    // hilltop above the room OFF the field, or the hill the core hides
    // under would sit one step from it on paper, and the dead would
    // walk its roof as though they were home.
    if (cfg_.core_top_z < 0) {
        for (int k = 0; k < 4; ++k) seed(cx + kDx[k], cy + kDy[k], 1);
    } else if (nq > 0) {
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + kDx[k], ny = cy + kDy[k];
            if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
            if (nx == block_x && ny == block_y) continue;
            for (int m = 0; m < surf_count(nx, ny); ++m) {
                const int nz = surf_z_[cell_index(nx, ny) * kSurfMax + m];
                bool beside = false;
                for (int j = 0; j < nq && !beside; ++j) {
                    const int dz = nz - core_z[j];
                    if (dz >= -Config::kWalkClimb && dz <= Config::kFall)
                        beside = true;
                }
                if (beside) seed_one(nx, ny, m, 1);
            }
        }
    } else {
        // The marker buried past reach: no floor of its column stands
        // anywhere the dead could stand. The seeds fall to the room
        // beside it instead - the deepest floor of each neighbour, the
        // ring the horde bites from - and the hill above stays what it
        // is: ground, nowhere near the core it hides.
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + kDx[k], ny = cy + kDy[k];
            if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
            if (nx == block_x && ny == block_y) continue;
            const int n = surf_count(nx, ny);
            int deep = -1, deep_z = -1;
            for (int m = 0; m < n; ++m) {
                const int nz = surf_z_[cell_index(nx, ny) * kSurfMax + m];
                if (deep < 0 || nz > deep_z) {
                    deep_z = nz;
                    deep = m;
                }
            }
            if (deep >= 0) seed_one(nx, ny, deep, 1);
        }
    }
    }   // the hunt seeds closed; the core seeds close above
    while (!open.empty()) {
        const QE top = open.top();
        open.pop();
        const int d = top.first;
        const size_t s = static_cast<size_t>(top.second);
        if (d != dist[s]) continue;  // a cheaper way came later
        const size_t i = s / kSurfMax;
        const int k = static_cast<int>(s % kSurfMax);
        const int x = static_cast<int>(i) % kMapW;
        const int y = static_cast<int>(i) / kMapW;
        const int z = surf_z_[i * kSurfMax + k];
        for (int q = 0; q < 4; ++q) {
            const int nx = x + kDx[q], ny = y + kDy[q];
            if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
            if (held_out(nx, ny)) continue;
            const size_t ni = cell_index(nx, ny);
            const int n = surf_count(nx, ny);
            for (int m = 0; m < n; ++m) {
                // z grows downward: dz is a drop and -dz a climb. Each of
                // the neighbour's surfaces is a crossing of its own - the
                // road under the arch and the road over its top are one
                // move apart only in a bird's flight, never in the legs'.
                const int dz = surf_z_[ni * kSurfMax + m] - z;
                int nd = d + 1;
                if (dig) {
                    // The spade's walk: the falls come back (a drop up to
                    // kFall, the dead fear no landing) and a climb beyond
                    // the jump is crossed at kDigPenalty steps a block -
                    // about the time its spade takes that block off - so
                    // a wall is a way in only when the way around it is
                    // the longer errand. The steps the legs could take
                    // keep their own level-shy price, so the dig walk
                    // still prefers its flats and a pack on it stays one
                    // pack. The drop beyond the jump pays the same as
                    // the rise: see the branch below for the honesty.
                    if (dz > Config::kFall) continue;
                    if (dz > Config::kWalkClimb) {
                        // THE FALL PAYS ITS PASSAGE: what pours off a
                        // ledge does not walk back up it, and where rock
                        // stands between the two floors the spade opens
                        // it the same way it takes a wall down. So the
                        // drop beyond the jump is crossed at the climb's
                        // own price a block, and the hollow a map sits
                        // over - the pocket a pyramid keeps under its
                        // floor - stops outbidding the honest road
                        // around: a way in that read free was read as
                        // the way, and the breakers it poured there
                        // stood under the marker with no wall to work
                        // and no road the mercy counts. A fall through
                        // open air still costs the body nothing; the
                        // price is the field's own honesty about the
                        // way back.
                        nd += Config::kDigPenalty * (dz - Config::kWalkClimb);
                    } else if (dz < -Config::kWalkClimb) {
                        nd += Config::kDigPenalty * (-dz - Config::kWalkClimb);
                    } else {
                        // THE STAIR TO THE MARKER IS FREE: a map that
                        // names its core's layer owes the one block of
                        // rise under that layer a level crossing - the
                        // spade reads the stair to its marker at the
                        // flat price, so the steps a map lays to it
                        // outbid the gallery under them and the pack
                        // keeps to a way it can walk back down.
                        int lvl = Config::kLevelShy * (dz < 0 ? -dz : dz);
                        if ((dz == -1 || dz == 1) && cfg_.core_top_z >= 0 &&
                            z + (dz > 0 ? dz : 0) >= cfg_.core_top_z)
                            lvl = 0;
                        nd += lvl;
                    }
                } else {
                    // Reversible: what the legs take down they take back up.
                    if (dz > Config::kWalkClimb || dz < -Config::kWalkClimb) continue;
                    // The march keeps one level where it can: each block of
                    // climb or drop a crossing spends is kLevelShy steps
                    // dearer than a level one, so a fold of the ground is
                    // crossed only where it is the crossing - the pack that
                    // walks one floor is one pack, where a march that rolls
                    // up and down every ripple comes apart on them.
                    // THE STAIR TO THE MARKER IS FREE on the legs' side
                    // too: where a map names its core's layer the one
                    // block of rise under it is a level crossing, so
                    // both walks read the same stair the same way and
                    // the pack the legs walk stays the pack the spade
                    // would walk.
                    int lvl = Config::kLevelShy * (dz < 0 ? -dz : dz);
                    if ((dz == -1 || dz == 1) && cfg_.core_top_z >= 0 &&
                        z + (dz > 0 ? dz : 0) >= cfg_.core_top_z)
                        lvl = 0;
                    nd += lvl;
                    // The tall tread is one crossing but a whole arc: the
                    // jump a climb of kWalkClimb rides pays kJumpShy steps
                    // over its blocks, so the stair of one beats the stair
                    // of two wherever both are laid. The tall tread stays
                    // the way where it is the only one - or where the way
                    // around costs more than the arc.
                    if (dz == -Config::kWalkClimb || dz == Config::kWalkClimb)
                        nd += Config::kJumpShy;
                }
                // Keeps to the open: the landing's two shoulder columns,
                // the sides the step runs past - room asks the same
                // kStandHead voxels the walk demands overhead. A map's
                // edge is sky; on open ground this price is nothing and
                // the walk stays a step a cell, as the traces and the
                // tests have read it.
                const int z2 = surf_z_[ni * kSurfMax + m];
                const uint64_t room =
                    ((1ull << Config::kStandHead) - 1ull) << (z2 - Config::kStandHead);
                const uint64_t side_a =
                    kDx[q] != 0 ? map_.column(nx, ny + 1) : map_.column(nx + 1, ny);
                const uint64_t side_b =
                    kDx[q] != 0 ? map_.column(nx, ny - 1) : map_.column(nx - 1, ny);
                if (side_a & room) nd += kWallShy;
                if (side_b & room) nd += kWallShy;
                const size_t ns = state(nx, ny, m);
                if (dist[ns] != kUnreachable && dist[ns] <= nd) continue;
                dist[ns] = nd;
                open.emplace(nd, static_cast<int32_t>(ns));
            }
        }
    }
}

// Can a wave still walk in? The anchors are the far end of the walk within
// kSpawnSearch rings of the corners, so that is the reach the veto
// protects: with the candidate block held out, does the walk still stand
// on one cell a wave could raise from, anywhere in a corner's reach?
// Every corner the map names is asked whole, whatever order the waves have
// been walking them in; a way in nobody can anchor is not the veto's to
// protect.
bool Horde::reach_spawn(const std::vector<int32_t>& dist) const {
    const auto on_walk = [&](int x, int y) {
        if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
        const int32_t* d = &dist[cell_index(x, y) * kSurfMax];
        for (int k = 0; k < kSurfMax; ++k)
            if (d[k] != kUnreachable) return true;
        return false;
    };
    // The named pool holds the fronts there are; a site beyond it is a
    // corner the map never asked for.
    for (int c = 0; c < spawn_pool(); ++c) {
        const int cx = roam_x_[c], cy = roam_y_[c];
        for (int r = 0; r < Config::kSpawnSearch; ++r) {
            for (int y = cy - r; y <= cy + r; ++y) {
                if (y == cy - r || y == cy + r) {
                    for (int x = cx - r; x <= cx + r; ++x)
                        if (on_walk(x, y)) return true;
                } else if (on_walk(cx - r, y) || on_walk(cx + r, y)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Horde::recompute() {
    const size_t cells = static_cast<size_t>(kMapW) * kMapH;
    surf_z_.assign(cells * kSurfMax, -1);
    surf_n_.assign(cells, -1);  // -1: not yet scanned
    // The walk without the spade: what legs can cross, and stairs when they
    // cannot. And the spade's own field beside it: the same surfaces, the
    // crossings the legs refuse taken at their price, so the host has a
    // way home to hand wherever the legs' walk falls silent, for the
    // licences of the pinned to ask. One recompute fills both.
    flood(dist_, -1, -1, false);
    flood(dig_dist_, -1, -1, true);
    field_dirty_ = false;
    // The hunt walk goes stale in the same breath: the next hunt_follow
    // floods it from wherever its person stands on the new ground.
    hunt_at_ = -1e9;
    // The reading on the old field is stale now: the next waiting tick
    // (or the next wave) reads the anchors off this one again.
    scouted_ = false;
}

void Horde::on_map_edited() { field_dirty_ = true; }

void Horde::move_core(int x, int y, int top_z) {
    cfg_.core_x = x;
    cfg_.core_y = y;
    cfg_.core_top_z = top_z;
    field_dirty_ = true;
}

// --- walking ----------------------------------------------------------------

bool Horde::next_step(int x, int y, int& tx, int& ty) {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    if (surf_count(x, y) <= 0) return false;  // nowhere to stand
    const int bk = best_state(x, y);
    const int here =
        bk < 0 ? kUnreachable : dist_[cell_index(x, y) * kSurfMax + bk];
    const int z = surface(x, y, bk < 0 ? 0 : bk);
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    // The advice weighs the step itself. The flood recorded in the dist
    // here what being entered from each neighbour cost it - the move,
    // the levels it climbed or dropped, the jump a tall tread rides,
    // the shoulders the landing brushed. The walker steps back the way
    // that bargain read cheapest, never through a pricier step toward
    // a cheaper dist it can read across a fold: a stair walked midway
    // no longer leaps the face for the top it sees over its treads.
    const uint64_t room =
        ((1ull << Config::kStandHead) - 1ull) << (z - Config::kStandHead);
    const uint64_t side_ax = map_.column(x, y + 1);
    const uint64_t side_bx = map_.column(x, y - 1);
    const uint64_t side_ay = map_.column(x + 1, y);
    const uint64_t side_by = map_.column(x - 1, y);
    int best = -1, bx = -1, by = -1;
    for (int k = 0; k < 4; ++k) {
        const int nx = x + kDx[k], ny = y + kDy[k];
        if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
        const int base = cell_index(nx, ny) * kSurfMax;
        for (int m = 0; m < surf_count(nx, ny); ++m) {
            const int nd = dist_[base + m];
            if (nd < 0) continue;
            // The field prices the crossing: without the dig it takes only
            // what legs cross both ways - a jump, up or down, nothing
            // taller - so the neighbour handed back is always a step the
            // walker can make and unmake, and the surface handed down with
            // it is one the legs truly reach.
            const int dz = surface(nx, ny, m) - z;
            if (dz > Config::kWalkClimb || dz < -Config::kWalkClimb) continue;
            int price = 1 + Config::kLevelShy * (dz < 0 ? -dz : dz);
            if (dz == -Config::kWalkClimb || dz == Config::kWalkClimb)
                price += Config::kJumpShy;
            if (kDx[k] != 0) {
                if (side_ax & room) price += kWallShy;
                if (side_bx & room) price += kWallShy;
            } else {
                if (side_ay & room) price += kWallShy;
                if (side_by & room) price += kWallShy;
            }
            const int score = nd + price;
            if (here >= 0 && score > here) continue;
            if (best < 0 || score < best) {
                best = score;
                bx = nx;
                by = ny;
            }
        }
    }
    // At the core's own cell, no step helps; that is the zombie's business.
    if (here == 0) return false;
    // Off the field: head straight for the core, terrain permitting - a slow
    // walk around a broken field, not a standstill.
    if (bx < 0) {
        const int cx = cfg_.core_x, cy = cfg_.core_y;
        if (x == cx && y == cy) return false;
        const int sx = std::abs(cx - x) >= std::abs(cy - y) ? (cx > x ? 1 : -1) : 0;
        const int sy = sx == 0 ? (cy > y ? 1 : -1) : 0;
        if (sx != 0 && surf_count(x + sx, y) > 0) {
            tx = x + sx;
            ty = y;
            return true;
        }
        if (sy != 0 && surf_count(x, y + sy) > 0) {
            tx = x;
            ty = y + sy;
            return true;
        }
        if (sx != 0 && surf_count(x, y + (cy > y ? 1 : -1)) > 0) {
            tx = x;
            ty = y + (cy > y ? 1 : -1);
            return true;
        }
        return false;
    }
    tx = bx;
    ty = by;
    return true;
}

// The advice of the hunt: the same weighing read the pack next_step
// makes, taken on the hunt field - the neighbour of the lowest hunt
// distance the footing of the walker prices cheapest, so the walk
// around a wall is the walk to the person wherever the map keeps one.
// At the hunted cell no step helps: the teeth and the pool are the
// business of the hunter there. Off the field, straight for the person,
// terrain permitting - the errand still runs, and silence is a press,
// not a standstill. False when no person is hunted, nowhere is stood,
// or nothing helps: the host stands its own answers on that.
bool Horde::hunt_step(int x, int y, int& tx, int& ty) {
    if (hunt_x_ < 0 || hunt_y_ < 0) return false;
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    if (surf_count(x, y) <= 0) return false;  // nowhere to stand
    const int bk = best_hunt_state(x, y);
    const int here = bk < 0 ? kUnreachable
                            : hunt_dist_[cell_index(x, y) * kSurfMax + bk];
    const int z = surface(x, y, bk < 0 ? 0 : bk);
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    const uint64_t room =
        ((1ull << Config::kStandHead) - 1ull) << (z - Config::kStandHead);
    const uint64_t side_ax = map_.column(x, y + 1);
    const uint64_t side_bx = map_.column(x, y - 1);
    const uint64_t side_ay = map_.column(x + 1, y);
    const uint64_t side_by = map_.column(x - 1, y);
    int best = -1, bx = -1, by = -1;
    for (int k = 0; k < 4; ++k) {
        const int nx = x + kDx[k], ny = y + kDy[k];
        if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
        const int base = cell_index(nx, ny) * kSurfMax;
        for (int m = 0; m < surf_count(nx, ny); ++m) {
            const int nd = hunt_dist_[base + m];
            if (nd < 0) continue;
            const int dz = surface(nx, ny, m) - z;
            if (dz > Config::kWalkClimb || dz < -Config::kWalkClimb) continue;
            int price = 1 + Config::kLevelShy * (dz < 0 ? -dz : dz);
            if (dz == -Config::kWalkClimb || dz == Config::kWalkClimb)
                price += Config::kJumpShy;
            if (kDx[k] != 0) {
                if (side_ax & room) price += kWallShy;
                if (side_bx & room) price += kWallShy;
            } else {
                if (side_ay & room) price += kWallShy;
                if (side_by & room) price += kWallShy;
            }
            const int score = nd + price;
            if (here >= 0 && score > here) continue;
            if (best < 0 || score < best) {
                best = score;
                bx = nx;
                by = ny;
            }
        }
    }
    if (here == 0) return false;   // at the person: its own business
    if (bx < 0) {
        if (x == hunt_x_ && y == hunt_y_) return false;
        const int sx = std::abs(hunt_x_ - x) >= std::abs(hunt_y_ - y)
                          ? (hunt_x_ > x ? 1 : -1) : 0;
        const int sy = sx == 0 ? (hunt_y_ > y ? 1 : -1) : 0;
        if (sx != 0 && surf_count(x + sx, y) > 0) {
            tx = x + sx;
            ty = y;
            return true;
        }
        if (sy != 0 && surf_count(x, y + sy) > 0) {
            tx = x;
            ty = y + sy;
            return true;
        }
        if (sx != 0 && surf_count(x, y + (hunt_y_ > y ? 1 : -1)) > 0) {
            tx = x;
            ty = y + (hunt_y_ > y ? 1 : -1);
            return true;
        }
        return false;
    }
    tx = bx;
    ty = by;
    return true;
}

// The hunt field follows the person the host names: a flood each cell
// the person takes, and one a second while they hold it. The edits of
// the map stale the walk with everything else, the shared recompute does
// the standing of it, and an unnamed person stands the field down so
// the hunters read silence and press their own way.
void Horde::hunt_follow(int px, int py, double now) {
    if (px < 0 || py < 0 || px >= kMapW || py >= kMapH) {
        hunt_x_ = -1;
        hunt_y_ = -1;
        return;
    }
    if (px == hunt_x_ && py == hunt_y_ &&
        now - hunt_at_ < kHuntRefresh) return;
    hunt_x_ = px;
    hunt_y_ = py;
    hunt_at_ = now;
    flood(hunt_dist_, -1, -1, false, px, py);
}

// The best the hunt field says about the column underfoot, asked by
// the host probe of a chase: the walk holds no word about columns it
// never reached, and the silence reads aloud there.
int Horde::hunt_reach(int x, int y) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return kUnreachable;
    const int bk = best_hunt_state(x, y);
    return bk < 0
               ? kUnreachable
               : hunt_dist_[cell_index(x, y) * kSurfMax + bk];
}

// The standing surface of the column the walk has reached that meets the
// feet at z: of the floors that could carry the body, the one this side
// of its soles. Only the floors the field reaches are asked - a column
// may carry others, but the walk has no opinion about floors it never
// joined, and the advice read from one would be guesswork. Ties keep the
// cheaper floor; -1 when nothing the walk reaches is near the asking
// height. The scan the surfaces cache got is not asked for here: a floor
// with a distance was joined through it, so its z is already in the
// book - which is what lets this read stay const.
// The read stays ANCHORED on the ground underfoot: a floor reached
// through solid rock - the room sealed under a hill, the storey under
// a deck the walk crosses - is a floor the body is not standing on, and
// a distance borrowed from it paces a walker at a wall the whole map
// long, licensed by nothing. No floor further from the asking height
// than a crossing the legs make is asked: the honest silence, -1, is
// the word the stuck licence is granted by.
int Horde::near_state(int x, int y, int z) const {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return kUnreachable;
    const size_t i = cell_index(x, y);
    const int32_t* d = &dist_[i * kSurfMax];
    int bk = -1, bdz = 0;
    for (int k = 0; k < kSurfMax; ++k) {
        if (d[k] == kUnreachable) continue;
        const int dz = std::abs(static_cast<int>(surf_z_[i * kSurfMax + k]) - z);
        if (dz > Config::kWalkClimb) continue;   // anchored underfoot
        if (bk < 0 || dz < bdz || (dz == bdz && d[k] < d[bk])) {
            bk = k;
            bdz = dz;
        }
    }
    return bk;
}

// The advice for a walker whose feet are at z: the same downhill read as
// the cell's own next_step, taken from the floor the body stands on
// instead of the cell's cheapest one. A column is not one floor - the
// field walks a tunnel's floor as gladly as the bush standing over its
// mouth - and the cheapest floor can be one the legs never touch from
// where they are: the way down to it is a wall, and the way the walker
// knows, back the way it came, is downhill from there, so a leader sent
// chasing its own column's bargain paces between the cell it holds and
// the cell it is told to want, forever, moving all the while. From the
// footing the step is honest: every candidate floor of the neighbour is
// measured against the ground underfoot, and the neighbour may promise
// less than the tunnel's bargain and still be the true way. False on the
// same terms as the two-argument read, which the host falls back to when
// the feet are in the air or on a floor the walk has no opinion about.
// The floor the chosen crossing runs onto rides along in tz, so the
// host knows the climb the advice is asking for; the straight line and
// the core cell say nothing by it, and leave tz as they found it.
bool Horde::next_step(int x, int y, int zfoot, int& tx, int& ty, int& tz) {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    if (surf_count(x, y) <= 0) return false;  // nowhere to stand
    const int rk = near_state(x, y, zfoot);
    const int here =
        rk < 0 ? kUnreachable : dist_[cell_index(x, y) * kSurfMax + rk];
    const int z = surface(x, y, rk < 0 ? 0 : rk);
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    // The advice weighs the step itself, as the two-arg read does: the
    // dist here stands entered at what each neighbour paid to step in
    // - the move, the levels, the jump, the shoulders - and the walker
    // steps back the way that bargain read cheapest.
    const uint64_t room =
        ((1ull << Config::kStandHead) - 1ull) << (z - Config::kStandHead);
    const uint64_t side_ax = map_.column(x, y + 1);
    const uint64_t side_bx = map_.column(x, y - 1);
    const uint64_t side_ay = map_.column(x + 1, y);
    const uint64_t side_by = map_.column(x - 1, y);
    int best = -1, bx = -1, by = -1, bz = z;
    for (int k = 0; k < 4; ++k) {
        const int nx = x + kDx[k], ny = y + kDy[k];
        if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
        const int base = cell_index(nx, ny) * kSurfMax;
        for (int m = 0; m < surf_count(nx, ny); ++m) {
            const int nd = dist_[base + m];
            if (nd < 0) continue;
            // The crossing stays priced against the footing: the read
            // promises a step the legs can make from the ground they are
            // on, the same both ways the field ever demands.
            const int dz = surface(nx, ny, m) - z;
            if (dz > Config::kWalkClimb || dz < -Config::kWalkClimb) continue;
            int price = 1 + Config::kLevelShy * (dz < 0 ? -dz : dz);
            if (dz == -Config::kWalkClimb || dz == Config::kWalkClimb)
                price += Config::kJumpShy;
            if (kDx[k] != 0) {
                if (side_ax & room) price += kWallShy;
                if (side_bx & room) price += kWallShy;
            } else {
                if (side_ay & room) price += kWallShy;
                if (side_by & room) price += kWallShy;
            }
            const int score = nd + price;
            if (here >= 0 && score > here) continue;
            if (best < 0 || score < best) {
                best = score;
                bx = nx;
                by = ny;
                bz = surface(nx, ny, m);
            }
        }
    }
    // At the core's own cell, no step helps; that is the zombie's business.
    if (here == 0) return false;
    // Off the field, or holding a floor nothing near is cheaper than:
    // head straight for the core, terrain permitting - a slow walk
    // around a broken field, not a standstill. The aid prices nothing
    // as a climb, and says as much by leaving tz alone.
    if (bx < 0) {
        const int cx = cfg_.core_x, cy = cfg_.core_y;
        if (x == cx && y == cy) return false;
        const int sx = std::abs(cx - x) >= std::abs(cy - y) ? (cx > x ? 1 : -1) : 0;
        const int sy = sx == 0 ? (cy > y ? 1 : -1) : 0;
        if (sx != 0 && surf_count(x + sx, y) > 0) {
            tx = x + sx;
            ty = y;
            return true;
        }
        if (sy != 0 && surf_count(x, y + sy) > 0) {
            tx = x;
            ty = y + sy;
            return true;
        }
        if (sx != 0 && surf_count(x, y + (cy > y ? 1 : -1)) > 0) {
            tx = x;
            ty = y + (cy > y ? 1 : -1);
            return true;
        }
        return false;
    }
    tx = bx;
    ty = by;
    tz = bz;
    return true;
}

// The spade's advice: a downhill read on the dig walk's own field, taken
// from the floor the body stands on, exactly the legs' own read but for
// the crossings the legs refuse - a fall may pour off of, a climb beyond
// the jump is the host's signal to draw the spade (the crossing's own
// surface is handed back in `tz`, and the walker's work is to take its
// top off until what is left is a jump). No straight-line fallback here:
// the advice is only asked where the legs' advice has already failed, and
// a straight line is how a wall gathers its crowd. False where the dig
// walk is silent at the feet, or downhill has nothing to hand back.
bool Horde::next_dig_step(int x, int y, int zfoot, int& tx, int& ty, int& tz) {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    if (surf_count(x, y) <= 0) return false;  // nowhere to stand
    const int rk = near_dig_state(x, y, zfoot);
    const int here =
        rk < 0 ? kUnreachable : dig_dist_[cell_index(x, y) * kSurfMax + rk];
    if (here <= 0) return false;   // off the dig walk, or the core's own cell
    const int z = surface(x, y, rk < 0 ? 0 : rk);
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    int best = -1, bx = -1, by = -1, bz = -1;
    for (int k = 0; k < 4; ++k) {
        const int nx = x + kDx[k], ny = y + kDy[k];
        if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
        const int base = cell_index(nx, ny) * kSurfMax;
        for (int m = 0; m < surf_count(nx, ny); ++m) {
            const int nd = dig_dist_[base + m];
            if (nd < 0 || nd >= here) continue;  // downhill only
            if (best < 0 || nd < best) {
                best = nd;
                bx = nx;
                by = ny;
                bz = surface(nx, ny, m);
            }
        }
    }
    if (bx < 0) return false;   // the doorstep is behind: nothing to advise
    tx = bx;
    ty = by;
    tz = bz;
    return true;
}

// The field's distance of the floor a body at z stands on: the walk read
// at the feet, the number the host keeps window by window to tell a
// walker that moves from a walker that gets somewhere.
int Horde::distance(int x, int y, int z) const {
    const int k = near_state(x, y, z);
    if (k < 0) return kUnreachable;
    return dist_[cell_index(x, y) * kSurfMax + k];
}

// The spade's own read of the same: the dig walk's way home from the
// floor the feet stand on - the measure the walker following a crossing
// the spade advised is judged by, for the legs' own field is silent on
// the ground that crossing crosses, and a walker scored there would
// read as standing still all the way home.
int Horde::dig_distance(int x, int y, int z) const {
    const int k = near_dig_state(x, y, z);
    if (k < 0) return kUnreachable;
    return dig_dist_[cell_index(x, y) * kSurfMax + k];
}

bool Horde::near_core(int x, int y, int cells) const {
    return std::abs(x - cfg_.core_x) <= cells && std::abs(y - cfg_.core_y) <= cells;
}

bool Horde::corner_anchor(int corner, int& x, int& y) const {
    // The world has ten sites to come from, not four: the guard was a
    // four-corner world's own, and the ten-site world outgrew it - a
    // corner past the third was anchored by the scout all the same and
    // this read denied it, so a front on a high-numbered corner could
    // raise its wave and still never be told to the host's own reading.
    if (corner < 0 || corner >= Config::kSpawnSites) return false;
    if (anchor_x_[corner] < 0) return false;
    x = anchor_x_[corner];
    y = anchor_y_[corner];
    return true;
}

// The wave comes on this many anchored fronts, the ranked first of them
// the wave's own points hold: the same count the placing raises its
// packs on, read where the painting asks it - the host says one trail a
// front, and here is how many trails a wave of `wave` says.
int Horde::wave_fronts(int wave) const {
    // A named pool draws its fronts by lot: the count the wave truly
    // opens is the drawn ones whose anchors stand.
    if (cfg_.spawn_named) {
        int fronts = 0;
        for (int r = 0; r < lot_n_; ++r) {
            if (anchor_x_[lot_corner_[r]] < 0) break;
            ++fronts;
        }
        return fronts;
    }
    const int nc = cfg_.wave_points(wave);
    int fronts = 0;
    for (int r = 0; r < nc && anchor_x_[rank_corner_[r]] >= 0; ++r) ++fronts;
    return fronts;
}

// The way in, one tread a cell: from wherever it starts the walk goes
// downhill on the field, each lower surface a legal crossing (the flood
// only ever joins surfaces a jump connects, up or down), until no lower
// surface is left - the core itself when its column is not in the way,
// and the doorstep beside it when it is, which is where the horde stops
// to bite. Each tread carries the surface it was walked on at, for the
// host paints the line of march at the walk's own height, never on the
// top of whatever column happens to stand over the road. On open ground
// the field's prices are one a step, so a traced route is the walk's
// distance long one cell of it a step; a step hedged by a wall pays its
// shoulders dearer and the trace follows those prices downhill all the
// same. The cap is a guard against a broken field, not a route that is
// expected.
bool Horde::trace_route(int x, int y, std::vector<RouteCell>& out) {
    out.clear();
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    int k = best_state(x, y);
    if (k < 0) return false;
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    const size_t guard = static_cast<size_t>(kMapW) * kMapH;
    while (out.size() < guard) {
        const int z = surface(x, y, k);
        out.push_back(RouteCell{x, y, z});
        const int here = dist_[cell_index(x, y) * kSurfMax + k];
        int best = -1, bx = -1, by = -1, bk = -1;
        for (int q = 0; q < 4; ++q) {
            const int nx = x + kDx[q], ny = y + kDy[q];
            if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
            const int base = cell_index(nx, ny) * kSurfMax;
            for (int m = 0; m < surf_count(nx, ny); ++m) {
                const int nd = dist_[base + m];
                if (nd < 0 || nd >= here) continue;  // downhill only
                const int dz = surface(nx, ny, m) - z;
                if (dz > Config::kWalkClimb || dz < -Config::kWalkClimb) continue;
                if (best < 0 || nd < best) {
                    best = nd;
                    bx = nx;
                    by = ny;
                    bk = m;
                }
            }
        }
        if (bx < 0) break;  // the doorstep: no lower crossing from here
        x = bx;
        y = by;
        k = bk;
    }
    return true;
}

// The same walk traced on the spade's own field: the crossings downhill
// may be wall tops and drops the legs refuse, which is the point - the
// host paints this route when the legs' own trace comes back empty, that
// a walled town sees which of its fences the dead mean to take down. The
// treads run at the dig walk's own height, wall tops included, and the
// guard is the same guard against the same broken field.
bool Horde::trace_dig(int x, int y, std::vector<RouteCell>& out) {
    out.clear();
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
    int k = best_dig_state(x, y);
    if (k < 0) return false;
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    const size_t guard = static_cast<size_t>(kMapW) * kMapH;
    while (out.size() < guard) {
        const int z = surface(x, y, k);
        out.push_back(RouteCell{x, y, z});
        const int here = dig_dist_[cell_index(x, y) * kSurfMax + k];
        int best = -1, bx = -1, by = -1, bk = -1;
        for (int q = 0; q < 4; ++q) {
            const int nx = x + kDx[q], ny = y + kDy[q];
            if (nx < 0 || nx >= kMapW || ny < 0 || ny >= kMapH) continue;
            const int base = cell_index(nx, ny) * kSurfMax;
            for (int m = 0; m < surf_count(nx, ny); ++m) {
                const int nd = dig_dist_[base + m];
                if (nd < 0 || nd >= here) continue;  // downhill only
                if (best < 0 || nd < best) {
                    best = nd;
                    bx = nx;
                    by = ny;
                    bk = m;
                }
            }
        }
        if (bx < 0) break;
        x = bx;
        y = by;
        k = bk;
    }
    return true;
}

// --- the veto ----------------------------------------------------------------

// One flood, the candidate column held out. The map is not touched here:
// the candidate only hides that column from the walk. The field the
// zombies use is the debounce's to refresh, not the veto's: a veto that
// walked it itself asked three floods a brick, and a server that goes
// quiet over one read to the client as one gone for good. The fail-open
// read below leans on that debounced field, then - a map sealed or
// opened a breath ago is read a breath late, and the tower keeps its
// own counsel either way.
bool Horde::can_build(int x, int y, int z) {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return true;
    if (z < 0 || z >= SolidMap::SIZE_Z) return true;
    std::vector<int32_t> with_block;
    flood(with_block, x, y, false);
    if (reach_spawn(with_block)) return true;
    // Fail open: sealed before this block too - the tower stands or falls on
    // its own, refusing nothing that changes nothing.
    if (!reach_spawn(dist_)) return true;
    // This block would have sealed the last way in.
    return false;
}

// The line's own ask: every column held out together, one walk for the
// whole line, where a brick a cell asked a walk each - a flank of
// pyramid is fifty walks no client should wait out. Refused, the whole
// line is: which brick of the many was the one that sealed the way is
// not a question worth a walk a brick to answer, and a line that seals
// between its bricks what none of them seals alone is caught here,
// where the ask a cell never caught it.
bool Horde::can_build_line(const std::vector<std::pair<int, int>>& columns) {
    std::vector<int32_t> held;
    held.reserve(columns.size());
    for (const auto& c : columns) {
        if (c.first < 0 || c.first >= kMapW ||
            c.second < 0 || c.second >= kMapH)
            return true;   // off the field: never the veto's to ask
        held.push_back(static_cast<int32_t>(cell_index(c.first, c.second)));
    }
    std::sort(held.begin(), held.end());
    held.erase(std::unique(held.begin(), held.end()), held.end());
    if (held.empty()) return true;
    std::vector<int32_t> with_line;
    flood(with_line, -1, -1, false, -1, -1, &held);
    if (reach_spawn(with_line)) return true;
    // Fail open, as the one brick does.
    if (!reach_spawn(dist_)) return true;
    return false;
}

// --- the waves ----------------------------------------------------------------

// A wave walks in from the far end of the walk. Every corner is asked for
// its anchor: the scan sweeps the rings around its asked-for centre out to
// kSpawnSearch and keeps the ground-level cell the field reaches LAST -
// the walk's frontier. The nearest cell of the walk could sit inside the
// very town the core is defended in, a jump from the gate, and such a
// wave never rings the walls nor walks a way worth painting; the frontier
// is where the legs run out, the feet of the border cliffs and the
// outside of whatever walls the map raises around the core, and the wave
// walks in from there. The corners a real map names
// sit in border mountains and sea, and a corner whose whole reach the
// walk never touches has no anchor, and its share of the wave is simply
// not raised.
//
// And when the legs' own reading anchors nothing at all - a town walled
// whole, no corner's reach anywhere on the walk - the whole reading is
// asked again of the spade's own field: the frontier stands outside the
// same walls on the dig walk's own prices, a way in for every sealed
// gate, and the wave comes to take the walls down at the pace they are
// priced. That is the only reading the dig walk owns: a single way the
// legs can take keeps the reading theirs.
//
// The corners are then RANKED by the way the walk reads into each: the
// route traced from an anchor either ends at the core's doorstep or dies
// against a climb the legs cannot make - in a walk that cannot dig, a
// cliff on the way in is a dead end, however freely the field spills over
// its drop on the far side. A route that ends where the horde bites wins
// its corner the wave's first turn, the shortest way first of all; a dead
// end waits behind whatever works; no anchor waits last. The wave comes
// from the first corners of that order: systematically the way in the
// terrain leaves, never a corner number the settings happened to name.
// This reading is asked on its own, apart from any wave: scout() below
// is it, and the waiting host paints the way in by it through Prep and
// Intermission, while place_wave stands a wave on it before the wave
// walks. The wave is born in a clearing at the road's end: the road is walked
// the way it runs, from its far end toward the core, and the first cell
// that opens a floor the walk itself reaches, within two of the road's
// own height there, under a square of five by five cells four voxels
// high, names the site. The wave rises scattered on that square's
// floors - the leader on the site, the rest each on a floor of its own
// within two of the site's - and a road that nowhere finds its clearing
// piles on the anchor's own floor, whatever room its neighbours give:
// players pass through players in this game, and the crowd's push
// splits the pile apart as it walks. The anchors are kept. A map that
// named its sites stands held above that road reading: the named
// ground is the birthplace and its asked layer the floor, the road
// keeps its own counsel, the clearing a bonus, the pile the honest
// room.
// The asking dealt again, a wave at a time: the asked-for centre of
// every corner is redrawn the moment the wave before falls, so a front
// keeps its colour but not its ground - the orange way in moves between
// the waves, and no colour camps one quarter of the world the whole game
// long - and, dealt at the fall, the round is on the table before the
// intermission it precedes: the trail the mending town paints shows the
// quarters the next wave will truly come from, not the ones the last
// did. The spread round the town stays even all the same: the compass
// is cut in as many sectors as there are corners, the sectors are dealt
// to the corners shuffled, and a corner's centre is drawn inside its
// own sector alone, so the wave's fronts - the ranked first of the dealt
// hands - stand wide apart wherever the terrain then lets their ways
// run. The centre only points; the anchor itself is still the walk's
// own frontier within the centre's reach (scout()), and the veto reads
// the same centres (reach_spawn), so a wall that would seal the ways
// this wave asked for is still refused, wherever the asking moved.
void Horde::roam() {
    std::uniform_real_distribution<double> bit(0.0, 1.0);
    // A map that named its sites keeps them exactly where it named them:
    // no asking is redrawn, the named ground stands, wave after wave.
    // The reading is still spent at the fall of a wave, so the next
    // scout reads the field as the new wave walks it and draws the
    // fronts of that wave from the named pool by lot (see draw_lot).
    if (cfg_.spawn_named) {
        scouted_ = false;
        return;
    }
    // The sectors, dealt shuffled: corner c holds sector deal[c].
    int deal[Config::kSpawnSites];
    for (int c = 0; c < Config::kSpawnSites; ++c) deal[c] = c;
    std::shuffle(deal, deal + Config::kSpawnSites, rng_);
    const double cx = cfg_.core_x, cy = cfg_.core_y;
    const double m = Config::kRoamMargin;
    for (int c = 0; c < Config::kSpawnSites; ++c) {
        // The corner's own sector, pointed anywhere within it but never
        // across its borders into the next.
        const double sector = (deal[c] + 0.15 + 0.70 * bit(rng_)) *
                              6.283185307179586 / Config::kSpawnSites;
        const double dx = std::cos(sector), dy = std::sin(sector);
        // How far the ray runs before it steps outside the border band;
        // a core at the band's own brink still gets a place to ask.
        double run = 1.0e9;
        if (dx > 1.0e-6) run = (std::min)(run, (kMapW - m - cx) / dx);
        else if (dx < -1.0e-6) run = (std::min)(run, (m - cx) / dx);
        if (dy > 1.0e-6) run = (std::min)(run, (kMapH - m - cy) / dy);
        else if (dy < -1.0e-6) run = (std::min)(run, (m - cy) / dy);
        if (run < 24.0) run = 24.0;
        // A pull back along the ray, drawn: the asking sits somewhere in
        // the ring around the town rather than pinned to the band itself.
        double t = run - bit(rng_) * Config::kRoamPull;
        if (t < 24.0) t = 24.0;
        int x = static_cast<int>(cx + dx * t);
        int y = static_cast<int>(cy + dy * t);
        if (x < static_cast<int>(m)) x = static_cast<int>(m);
        if (y < static_cast<int>(m)) y = static_cast<int>(m);
        if (x > kMapW - 1 - static_cast<int>(m)) x = kMapW - 1 - static_cast<int>(m);
        if (y > kMapH - 1 - static_cast<int>(m)) y = kMapH - 1 - static_cast<int>(m);
        roam_x_[c] = x;
        roam_y_[c] = y;
    }
    // A new asking: the reading that stood on the old centres is spent,
    // and the scout - the intermission's own reading, before the wave
    // stands up - reads it fresh.
    scouted_ = false;
}

// The fronts of the wave the reading prepares, on a map that named its
// sites: the wave that opens fewer fronts than the named pool holds
// takes its pick of the anchored ones by lot, and the drawn fronts keep
// the scout order between them, the shortest way in walking first -
// every named point takes its turn from wave to wave, none camped and
// none forgotten. The lot is drawn for the wave the reading prepares
// (the one walking when one is, the coming one otherwise) and it stands
// across field edits, for the fronts a wave already placed must never
// be re dealt under its own feet.
void Horde::draw_lot() {
    const int target = phase_ == Phase::Wave ? wave_ : wave_ + 1;
    if (lot_wave_ == target) return;
    const int nc = cfg_.wave_points(target);
    lot_n_ = 0;
    for (int r = 0; r < spawn_pool(); ++r)
        if (anchor_x_[rank_corner_[r]] >= 0)
            lot_corner_[lot_n_++] = rank_corner_[r];
    if (lot_n_ > nc) {
        std::shuffle(lot_corner_, lot_corner_ + lot_n_, rng_);
        const auto rank_of = [&](int corner) {
            for (int r = 0; r < spawn_pool(); ++r)
                if (rank_corner_[r] == corner) return r;
            return Config::kSpawnSites;
        };
        std::sort(lot_corner_, lot_corner_ + nc,
                  [&](int a, int b) { return rank_of(a) < rank_of(b); });
        lot_n_ = nc;
    }
    lot_wave_ = target;
}

void Horde::scout() {
    if (field_dirty_) recompute();
    // The anchors, every spawn point: the far end of the walk, the frontier.
    // The scan sweeps the rings around each point's asked-for centre out
    // to kSpawnSearch and keeps the cell the field reaches LAST, the walk's
    // longest way in, wherever the terrain stops the legs. A ring can
    // stand at many heights - a skirt of cliff over the plain, a canopy
    // over open ground - and a wave begun on a perch lands in foliage its
    // legs cannot cross, so only the cells at the ring's own ground
    // level, the height most of its cells stand at, are candidates, never
    // one perched more than kWalkDrop above it. Ties keep the first cell
    // the scan reads. A map that named its sites carries its own meaning
    // instead: the point named a birthplace, not a centre to read a
    // frontier from, so the sweep stops on the first cell the walk
    // reaches at the asked layer - the point itself wherever it stands,
    // the ground beside it wherever it does not. The anchor keeps the
    // layer it reads there, the other half of the name: whichever floor
    // the walk likes sooner stands in for a candidate read, never for
    // the floor the wave rises on.
    std::vector<int> rungs_x, rungs_y, rungs_g;
    // One reading of the anchors and their ranking, leg walk or dig walk
    // by the field it is handed: the dig pass asks the same questions of
    // the same terrain on the spade's own field, so the sealed town that
    // anchors nothing at all on the legs' pass still gets its fronts -
    // anchored on the dig walk's own frontier, ranked by the dig walk's
    // own arrival, the wave to walk them at the spade's own pace.
    const auto pass = [&](const std::vector<int32_t>& fld, bool dig_read) {
        // A cell the pass's field reaches, wherever it opens a floor to
        // stand on.
        const auto stands = [&](int x, int y) {
            if (x < 0 || x >= kMapW || y < 0 || y >= kMapH) return false;
            if (surf_count(x, y) <= 0) return false;
            const int32_t* d = &fld[cell_index(x, y) * kSurfMax];
            for (int k = 0; k < kSurfMax; ++k)
                if (d[k] >= 0) return true;
            return false;
        };
        // The pass's own distance of a cell: its best state's.
        const auto best_len = [&](int x, int y) {
            int best = kUnreachable;
            const int32_t* d = &fld[cell_index(x, y) * kSurfMax];
            for (int k = 0; k < kSurfMax; ++k)
                if (d[k] >= 0 && (best < 0 || d[k] < best)) best = d[k];
            return best;
        };
        // A site whose settings named a ground layer reads its
        // candidates off that layer instead: the surface closest to
        // the named one within kWalkDrop - the room under a roof, the
        // shelf under a canopy - and a column that carries no floor
        // of the asking notes nothing, whatever its tops stand at.
        int ask = -1;
        const auto note = [&](int x, int y) {
            if (!stands(x, y)) return;
            int g = ground(x, y);
            if (ask >= 0) {
                g = -1;
                const int ns = surf_count(x, y);
                int go = Config::kWalkDrop + 1;
                for (int k = 0; k < ns; ++k) {
                    const int z2 = surface(x, y, k);
                    const int off = z2 > ask ? z2 - ask : ask - z2;
                    if (off > Config::kWalkDrop) continue;
                    if (g < 0 || off < go) {
                        g = z2;
                        go = off;
                    }
                }
                if (g < 0) return;
            }
            rungs_x.push_back(x);
            rungs_y.push_back(y);
            rungs_g.push_back(g);
        };
        const int pool = spawn_pool();
        for (int c = 0; c < Config::kSpawnSites; ++c) {
            anchor_x_[c] = -1;
            anchor_y_[c] = 0;
            anchor_z_[c] = -1;
            if (c >= pool) continue;
            ask = cfg_.spawn_z[c];
            const int cx = roam_x_[c], cy = roam_y_[c];
            int best_d = -1;
            for (int r = 0; r < Config::kSpawnSearch; ++r) {
                rungs_x.clear();
                rungs_y.clear();
                rungs_g.clear();
                // The ring's cells in reading order: the full rows on the
                // frame's top and bottom, the side pair between them.
                for (int y = cy - r; y <= cy + r; ++y) {
                    if (y == cy - r || y == cy + r) {
                        for (int x = cx - r; x <= cx + r; ++x) note(x, y);
                    } else {
                        note(cx - r, y);
                        note(cx + r, y);
                    }
                }
                if (rungs_x.empty()) continue;
                // The ring's ground level: the height most of its candidates
                // stand at, ties to the lower ground.
                std::vector<int> levels(rungs_g);
                std::sort(levels.begin(), levels.end());
                int level = levels.front(), best_n = 0, run_n = 0;
                for (size_t k = 0; k < levels.size(); ++k) {
                    if (k > 0 && levels[k] != levels[k - 1]) run_n = 0;
                    ++run_n;
                    if (run_n >= best_n) {
                        best_n = run_n;
                        level = levels[k];
                    }
                }
                for (size_t k = 0; k < rungs_x.size(); ++k) {
                    if (std::abs(rungs_g[k] - level) > Config::kWalkDrop) continue;
                    const int d = best_len(rungs_x[k], rungs_y[k]);
                    // A map that named its sites named a birthplace, not
                    // a centre to read a frontier from: the anchor is the
                    // named ground itself - the first cell within the
                    // sweep the walk reaches at the asked layer, the
                    // point wherever it stands, the ground beside it
                    // wherever it does not.
                    if (cfg_.spawn_named) {
                        if (d >= 0) {
                            anchor_x_[c] = rungs_x[k];
                            anchor_y_[c] = rungs_y[k];
                            anchor_z_[c] = rungs_g[k];
                            break;
                        }
                        continue;
                    }
                    if (d > best_d) {
                        best_d = d;
                        anchor_x_[c] = rungs_x[k];
                        anchor_y_[c] = rungs_y[k];
                        anchor_z_[c] = rungs_g[k];
                    }
                }
                // The named ground found, the sweep stops here: the
                // unnamed corners keep reading their frontier out to the
                // whole reach, as ever.
                if (cfg_.spawn_named && anchor_x_[c] >= 0) break;
            }
        }
        // The ranking: each anchored corner's way in, read out loud.
        struct Ranked {
            int corner, end, dist;
            bool anchored;
        };
        std::vector<RouteCell> trace;
        Ranked ways[Config::kSpawnSites];
        for (int c = 0; c < pool; ++c) {
            ways[c].corner = c;
            ways[c].anchored = anchor_x_[c] >= 0;
            ways[c].end = ways[c].dist = 0;
            if (ways[c].anchored) {
                if (dig_read) trace_dig(anchor_x_[c], anchor_y_[c], trace);
                else trace_route(anchor_x_[c], anchor_y_[c], trace);
                ways[c].dist = best_len(anchor_x_[c], anchor_y_[c]);
                ways[c].end = trace.empty()
                                  ? kUnreachable
                                  : best_len(trace.back().x, trace.back().y);
            }
        }
        std::sort(ways, ways + pool, [](const Ranked& a, const Ranked& b) {
            if (a.anchored != b.anchored) return a.anchored;
            if (!a.anchored) return a.corner < b.corner;
            // A way in that arrives beats one that dies against a cliff.
            const bool a_arrives = a.end <= 1, b_arrives = b.end <= 1;
            if (a_arrives != b_arrives) return a_arrives;
            // Between ways that both arrive, the shorter way walks first: how
            // far a trace ends says whether it arrives, not how far it ran,
            // and ranking the two by that end sent the wave down the longer
            // way merely because it stepped onto the marker's shoulders.
            if (a.dist != b.dist) return a.dist < b.dist;
            if (a.end != b.end) return a.end < b.end;
            return a.corner < b.corner;  // the map reads NE, NW, SE, SW last
        });
        for (int c = 0; c < pool; ++c) rank_corner_[c] = ways[c].corner;
        for (int c = 0; c < pool; ++c)
            if (anchor_x_[c] >= 0) return true;   // the pass found a way in
        return false;
    };
    // The legs' pass first: the frontier of the walk that decides the
    // veto and the steering. A town the walls sealed whole - the legs'
    // pass anchors nothing anywhere - is handed to the spade's pass, its
    // frontier outside the same walls and its crossings priced like its
    // blocks: the wave still comes, at the pace the pricing says.
    if (!pass(dist_, false) && cfg_.dig) pass(dig_dist_, true);
    scouted_ = true;
    // The named pool, when there is one, draws the fronts of the wave
    // this reading prepares before anyone asks for them.
    if (cfg_.spawn_named) draw_lot();
}

// The wave stands on the scout's reading: the first of the ranked corners
// walks the way in. n zombies rise, spaced out by the spawn interval, the
// first of them on the clearing's site and the rest on the square's
// floors - the leader leads, the rest hold at its heel - and the anchors
// are the scout's, so the host paints the route the wave truly takes.
void Horde::place_wave(double now) {
    if (!scouted_) scout();   // the reading only moves with the field
    if (cfg_.spawn_named) draw_lot();
    const int n = cfg_.wave_zombies(wave_);
    const int nc = cfg_.wave_points(wave_);
    // The fronts the wave stands on: on a named pool the drawn lot
    // (draw_lot), elsewhere the first of the ranked ways, anchored.
    int ranks = 0;
    if (cfg_.spawn_named) {
        for (int r = 0; r < lot_n_; ++r) {
            if (anchor_x_[lot_corner_[r]] < 0) break;
            ++ranks;
        }
    } else {
        for (int r = 0; r < nc && anchor_x_[rank_corner_[r]] >= 0; ++r) ++ranks;
    }
    // The wave stands up only where the walk truly comes in, and it is
    // born in a clearing, not in a pocket: the road is walked from its
    // far end toward the core, and the first cell whose floor - one the
    // walk itself reaches, within kRoomTol of the height the road runs
    // at there, for a valloned map keeps no flats - opens a square of
    // five by five cells, four voxels high, above it, names the site.
    // Where no cell of the road does, the anchor's own floor is the
    // site and its neighbours give what room they have: the pile.
    struct Site {
        bool found = false;
        int x = 0, y = 0, z = 0;
        std::vector<int> mx, my, mz;
    };
    Site sites[Config::kSpawnSites];
    static constexpr int kRoomTol = 2;  // a floor may sit this far off its cell's road height
    const auto ground_of = [&](int x, int y) {
        const int bk = best_state(x, y);
        return bk >= 0 ? surface(x, y, bk) : surface(x, y, 0);
    };
    // The birthplaces inside the clearing: the site itself, then ring by
    // ring every column of the square that carries its own floor within
    // kRoomTol of the site's and a body's room standing on it - the road
    // need not reach them; the leader leads them out.
    const auto fill_site = [&](Site& s) {
        s.mx.clear();
        s.my.clear();
        s.mz.clear();
        s.mx.push_back(s.x);
        s.my.push_back(s.y);
        s.mz.push_back(s.z);
        for (int r = 1; r <= kRoomHalf; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (dy > -r && dy < r && dx > -r && dx < r) continue;  // the ring only
                    const int vx = s.x + dx, vy = s.y + dy;
                    if (vx < 0 || vx >= kMapW || vy < 0 || vy >= kMapH) continue;
                    int bz = -1, bo = 0;
                    const int ns = surf_count(vx, vy);
                    for (int k = 0; k < ns; ++k) {
                        const int z2 = surface(vx, vy, k);
                        const int off = z2 > s.z ? z2 - s.z : s.z - z2;
                        if (off > kRoomTol) continue;
                        if (!clear_of(vx, vy, z2)) continue;
                        if (bz < 0 || off < bo) { bz = z2; bo = off; }
                    }
                    if (bz >= 0) {
                        s.mx.push_back(vx);
                        s.my.push_back(vy);
                        s.mz.push_back(bz);
                    }
                }
    };
    const auto raise_site = [&](int c) {
        Site& s = sites[c];
        if (s.found) return;
        s.found = true;
        const int ax = anchor_x_[c], ay = anchor_y_[c];
        s.x = ax;
        s.y = ay;
        // A map that named its sites named a floor as well: the anchor
        // holds the site and its own layer the floor, whichever floor
        // the walk reaches sooner there - a reached roof must not
        // steal the birth from the rooms the name asked of, and the
        // road does not walk the wave away from its named ground. The
        // clearing stays a bonus: the birth scatters where the rooms
        // have the headroom, the pile is the room they have.
        if (cfg_.spawn_named && anchor_z_[c] >= 0) {
            s.z = anchor_z_[c];
            fill_site(s);
            return;
        }
        s.z = ground_of(ax, ay);
        std::vector<RouteCell> road;
        if (trace_route(ax, ay, road)) {
            for (const RouteCell& t : road) {
                // This cell's floors the walk reaches, within kRoomTol of
                // the road's own height there: the road's own level first,
                // ties to the lower slot.
                int cand[kSurfMax], off[kSurfMax], nf = 0;
                for (int k = 0; k < kSurfMax; ++k) {
                    const int cz = surface(t.x, t.y, k);
                    if (cz < 0) break;
                    if (dist_[cell_index(t.x, t.y) * kSurfMax + k] < 0) continue;
                    const int o = cz > t.z ? cz - t.z : t.z - cz;
                    if (o > kRoomTol) continue;
                    int p = nf++;
                    while (p > 0 && (off[p - 1] > o || (off[p - 1] == o && cand[p - 1] > cz))) {
                        off[p] = off[p - 1];
                        cand[p] = cand[p - 1];
                        --p;
                    }
                    off[p] = o;
                    cand[p] = cz;
                }
                bool found = false;
                for (int q = 0; q < nf && !found; ++q) {
                    if (!box_clear(t.x, t.y, cand[q])) continue;
                    s.x = t.x;
                    s.y = t.y;
                    s.z = cand[q];
                    found = true;
                }
                if (found) break;
            }
        }
        fill_site(s);
    };
    pending_.clear();
    for (int i = 0; i < n && ranks > 0; ++i) {
        const int corner = cfg_.spawn_named ? lot_corner_[i % ranks]
                                            : rank_corner_[i % ranks];
        raise_site(corner);
        const Site& s = sites[corner];
        // The leader takes the site itself and its road; the rest fill
        // the clearing around it, and only when the clearing runs out of
        // floors does one pile on another - never on the leader, whom
        // the whole wave follows.
        const size_t nm = s.mx.size();
        size_t mi = 0;
        if (i > 0 && nm > 1) mi = 1 + static_cast<size_t>((i - 1) % (static_cast<int>(nm) - 1));
        SpawnAt a{s.mx[mi], s.my[mi], s.mz[mi],
                  now + i * cfg_.spawn_interval, corner};
        pending_.push_back(a);
    }
    std::sort(pending_.begin(), pending_.end(),
              [](const SpawnAt& a, const SpawnAt& b) { return a.at < b.at; });
    placed_ = static_cast<int>(pending_.size());
}

void Horde::begin_wave(double now) {
    wave_ += 1;
    phase_ = Phase::Wave;
    // The net loads at birth: one generous clock the wave may not
    // outlive, none at all when the net is off.
    net_fired_ = false;
    wave_deadline_at_ = cfg_.wave_safety_seconds > 0.0
                            ? now + cfg_.wave_safety_seconds : -1.0;
    place_wave(now);
}

bool Horde::take_due(std::vector<SpawnAt>& out, double now) {
    out.clear();
    bool any = false;
    while (!pending_.empty() && pending_.front().at <= now) {
        out.push_back(pending_.front());
        pending_.erase(pending_.begin());
        any = true;
    }
    if (any) up_ += static_cast<int>(out.size());
    return any;
}

void Horde::on_zombie_killed() {
    if (up_ > 0) up_ -= 1;
}

void Horde::on_core_damaged(int amount) {
    core_hp_ = (std::max)(0, core_hp_ - amount);
    if (core_hp_ == 0) {
        phase_ = Phase::Defeat;
        pending_.clear();
    }
}

void Horde::add_core_max_hp(int amount) {
    if (amount <= 0)
        return;
    // The ceiling and the carried health rise together: plating bought on
    // a full core is worth the whole of it, and a wounded one is mended by
    // the same step. The ceiling lives on the config, so the wave
    // announcements that read it read what stands now.
    cfg_.core_hp += amount;
    core_hp_ = (std::min)(cfg_.core_hp, core_hp_ + amount);
}

void Horde::clear_wave(double now) {
    pending_.clear();
    up_ = 0;
    core_hp_ = cfg_.core_hp;
    // The net is spent with the wave it held: whether the host took
    // the wave by the hand or the word, the next birth reloads it.
    net_fired_ = false;
    wave_deadline_at_ = -1.0;
    if (phase_ == Phase::Defeat) {
        phase_ = Phase::Intermission;
        phase_until_ = now + cfg_.intermission_seconds;
    }
}

void Horde::restart(double now) {
    phase_ = Phase::Prep;
    wave_ = 0;
    up_ = 0;
    core_hp_ = cfg_.core_hp;
    pending_.clear();
    net_fired_ = false;
    wave_deadline_at_ = -1.0;
    phase_until_ = now + cfg_.prep_seconds;
    recompute();
}

double Horde::phase_left(double now) const {
    if (phase_ == Phase::Prep || phase_ == Phase::Intermission)
        return phase_until_ - now;
    return -1.0;
}

void Horde::tick(double now) {
    if (field_dirty_ && now - field_at_ >= cfg_.field_period) {
        recompute();
        field_at_ = now;
    }
    switch (phase_) {
        case Phase::Prep:
            if (!scouted_) scout();
            if (now >= phase_until_) begin_wave(now);
            break;
        case Phase::Wave:
            if (wave_deadline_at_ >= 0.0 && !net_fired_ &&
                now >= wave_deadline_at_) {
                // Once the clock runs out the latch holds: the host
                // clears the wave, and only the next birth loads the
                // net again - a clear, one wave, one take.
                net_fired_ = true;
            }
            if (wave_cleared()) {
                // 0 asks the impossible: no wave is the last, whatever
                // clears another follows - the debugging season's own
                // endless rain, at the count the world can no longer
                // raise once the ramp has topped out.
                if (cfg_.wave_count_max > 0 && wave_ >= cfg_.wave_count_max) {
                    phase_ = Phase::Victory;
                    break;
                }
                phase_ = Phase::Intermission;
                phase_until_ = now + cfg_.intermission_seconds;
                core_hp_ = (std::min)(cfg_.core_hp, core_hp_ + cfg_.core_mend);
                // The next wave's asking is dealt the moment this one
                // falls, not when the next stands up: the intermission
                // that follows - the town's one quiet moment to build
                // in - already paints the trail of the quarters the
                // next wave will truly come from.
                if (cfg_.roam_spawns) roam();
            }
            break;
        case Phase::Intermission:
            if (!scouted_) scout();
            if (now >= phase_until_) begin_wave(now);
            break;
        case Phase::Victory:
        case Phase::Defeat:
            break;
    }
}

}  // namespace horde
