#pragma once
// The horde's brain: waves of walking dead that head for a core the player
// defends. Ported from the mode's Lua draft (xrs_td_horde.lua) - the same
// state machine and rules - onto this server's own map, for the training
// course's host to run. The brains only: waves, the flow field and the build
// veto. Physics, players and packets stay in Server.cpp, which steers the
// zombies it owns by asking this where to step.
//
// The field is one walk from the core over the map's standing surfaces, the
// top of every solid run that leaves headroom above it, recomputed a short
// debounce after every edit; a zombie steps to the neighbouring surface whose
// distance is lowest, which is a cheapest path around whatever has been
// built since - a wall's top is one surface, and the floor of a tunnel
// through it is another, so the walk goes under as well as over, and no
// gate an arch leaves standing closes a town. The dead walk, and they dig
// only when their legs fail them at it: besides the legs' own reversible
// walk there is a second field, the spade's walk - the falls up to kFall
// come back into it, and a climb beyond the jump and a drop beyond it
// are each crossed at kDigPenalty steps a block, the time its spade
// takes that block off - and the spade's
// way is consulted only on a licence of the pinned - the stuck clock run
// out, the dig watch run down - so the dead scale a wall only when their
// own legs fail them at it. The legs' own walk keeps its
// crossings reversible - a step of kWalkClimb up or down is a jump, and
// anything more is a wall. And on a map that names its core's layer the
// stair of one block rising from a floor that layer tops is read as
// level outright, the marker's own stair free - so the steps a map lays
// to its core outbid any gallery under them. The walk also keeps to the
// open: a cell hedged by a
// wall on either side of the step is priced dearer than one with a
// shoulder's room both ways, so a way the legs could hug against a wall -
// where a walker jams on its corner - is walked only where the map leaves
// no other way in, and so is a change of level: each block of climb or
// drop a crossing spends is priced dearer than a level step, so where the
// ground offers a flat way around its folds the march takes it and stays
// one pack. On open, level ground the price is one a step, as ever. The
// dead themselves still take no falling damage, but
// the plan never feeds them into a fall it cannot lead them out of - so
// the field takes the stairs and the ramps, and neither climbs nor pours
// down a cliff the map shows it. The veto
// asks the field's own question the other way round: with the cell a player
// wants to build at taken as unwalkable, is there still a route a walker
// could take from a spawn corner to the
// core?  If not, and there was one before, that block would seal the last
// way in, and is refused. When the map is already sealed (or the field is
// out of date), the veto fails open and the tower stands or falls as the
// player builds it; a sealed world stays sealed to the legs, and the
// spade's walk is the way through it: the scout anchors a wave on the
// dig walk's frontier when the legs' walk cannot reach a spawn point at
// all, so a walled town still catches its waves - at their own pace, the
// pace of the spade. The walk the field keeps can
// be read out cell by cell - trace_route, each cell with the surface it was
// walked on - which the host paints at the walk's own height, never on the
// treetops above it, so the way in can be watched while it proves itself.
#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

class SolidMap;

namespace horde {

enum class Phase {
    Prep,          // before wave one: build, while the field is fresh
    Wave,          // zombies walking
    Intermission,  // a wave cleared: the core mends a little
    Victory,       // the last wave cleared
    Defeat         // the core fell
};

struct Config {
    // The cell the zombies walk to.
    int core_x = 256, core_y = 256;
    // The layer of the marker's own top block, as its host reads it of
    // the ground the core stands on; -1 when the host has no word for
    // it (the tests' flat world keeps the old blanket seeding). The
    // walk only starts on the floors this names: every other surface
    // of the core's own column - the hilltop of a cave that sealed its
    // core under the map - owns no distance and starts no walk, for
    // the dead that stand on it are nowhere near the core, whatever
    // the column under their feet keeps.
    int core_top_z = -1;
    // The spawn points the dead come from: the centre of each one's reach.
    // Ten of them now - the map's four corners, and six more spread along
    // its edges - so the later waves can open more fronts than the corners
    // alone. A wave takes as many of them, of the ranked order, as its
    // own number says; a point whose reach the walk never touches raises
    // nothing, whatever its number.
    static constexpr int kSpawnSites = 10;
    int spawn_x[kSpawnSites] = {32, 479, 32, 479, 181, 330, 181, 330, 32, 479};
    int spawn_y[kSpawnSites] = {32, 32, 479, 479, 32, 32, 479, 479, 255, 255};
    // A site the map named may stand under a roof the terrain alone
    // would misname: the third number of its triple is the layer of the
    // ground the wave is asked to stand on, the same trust the core foot
    // layer enjoys (-1 reads the terrain itself).
    int spawn_z[kSpawnSites] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // Whether the map named its spawn sites, and how many it named: the
    // named sites are then the whole pool the fronts come from, exactly
    // as named, no redraw ever moving them, and a wave that opens fewer
    // fronts than the pool holds draws them by lot (see draw_lot). A
    // map that named none keeps the ten built above and the dealing
    // quarters of roam_spawns.
    bool spawn_named = false;
    int spawn_count = kSpawnSites;
    // The spawn box width of earlier days; the clearing is five by five now.
    int spawn_size = 12;
    // Whether each wave deals its fronts a new asking round the town
    // (see Horde::roam): a front keeps its colour and moves its ground
    // between the waves, and the askings spread evenly yet anew around
    // the town every wave - the round dealt the moment the wave before
    // falls, so the intermission's own trail already says where the
    // next wave comes. Off - the tests' deterministic reading - the
    // asked-for centres stand as named above, wave after wave.
    bool roam_spawns = false;
    // The roaming asking's own band: it keeps this far off the map's own
    // edge, and may be drawn this far back along its ray toward the
    // town, so the asking sits somewhere in the ring around the town
    // rather than pinned to the border itself.
    static constexpr int kRoamMargin = 16;
    static constexpr int kRoamPull = 64;
    double prep_seconds = 20.0;
    double intermission_seconds = 12.0;
    // Zombies trickle out, not all at once - though a wave that stands
    // around being born is a wave that looks stalled: quicker than it
    // once kept, so the horde reads as one body rising and walking.
    double spawn_interval = 0.25;
    // The rounds keep coming while the ramp still has something new to
    // show: the points all open by ten, and the packs grow on past the
    // cap. The campaign never ends now: whatever falls, another wave
    // stands up, and no last wave hands the game its Victory. A number
    // keeps the old campaign - a run that wants an end sets it - and
    // zero stays the number the tests lean on when they ask for the
    // rain that never ends.
    int wave_count_max = 0;
    int core_hp = 150;
    // How much of the core's health a cleared wave mends back.
    int core_mend = 25;
    // The safety net: a wave that neither falls nor wins - a pack lost
    // where no licence sees it, a head wedged in a pocket the fields
    // price kindly - would hold the game hostage for as long as it
    // shuffles. Every wave carries a generous clock of its own, and
    // the rare one that stands past it is taken whole, as the wave
    // word takes it; the next stands up in the quiet after, the count
    // unchanged. Zero leaves the net off.
    double wave_safety_seconds = 240.0;
    // How long after an edit the field is recomputed. Two seconds: the
    // reads the licences ask of the field stand still as long as the
    // dig ratio is owed its own two seconds of a road, so a ground
    // that changes under a pack does not flip a licence that has just
    // drawn one - while a breach the pack itself opens is still read
    // before the errand can be asked again.
    double field_period = 2.0;
    // How big a wave is: the waves open fronts as they number, one
    // spawn point more a wave until the points reach their cap - and
    // the dead a wave counts climb their own ramp: five the first
    // wave, five more a wave to the fifth, ten more a wave to the
    // tenth, twenty more a wave ever after. The ramp is cut to the
    // packs each point stands, so a thin front of points field packs
    // as deep as a wide one. The whole of it is capped at
    // kWaveMaxZombies, the world's own budget of the dead - what the
    // slots cannot stand is skipped at the raising, one at a time,
    // and never a wave that waits on it.
    int spawn_point_cap = kSpawnSites;
    int wave_scale = 100;   // the share of the ramp a round walks
    static constexpr int kWaveMaxZombies = 250;
    int wave_target(int wave) const {
        const int w = (std::max)(1, wave);
        int t;
        if (w <= 5)
            t = 5 + 5 * (w - 1);
        else if (w <= 10)
            t = 25 + 10 * (w - 5);
        else
            t = 75 + 20 * (w - 10);
        // The host may stand a round thinner than the ramp counts:
        // the scale is its own word, said between the waves, its
        // floors kept honest however deep it cuts.
        t = t * (std::max)(0, wave_scale) / 100;
        return (std::max)(1, t);
    }
    int wave_points(int wave) const {
        int pool = kSpawnSites;
        if (spawn_named && spawn_count >= 1 && spawn_count < kSpawnSites)
            pool = spawn_count;
        int cap = spawn_point_cap < 1
            ? 1 : (spawn_point_cap > kSpawnSites ? kSpawnSites : spawn_point_cap);
        if (cap > pool) cap = pool;   // fewer named sites than fronts to open
        const int w = (std::max)(1, wave);
        return w < cap ? w : cap;
    }
    int wave_pack(int wave) const {
        const int points = wave_points(wave);
        const int want = (std::max)(1, wave_target(wave));
        return (want + points - 1) / points;   // the ramp cut to decks
    }
    int wave_zombies(int wave) const {
        return (std::min)(kWaveMaxZombies, wave_points(wave) * wave_pack(wave));
    }
    // How the dead cross: the walk is reversible - a step of at most
    // kWalkClimb, up or down, and anything more is a wall. The dead still
    // take no falling damage, but the route never feeds them into a drop
    // it cannot lead them out of: the stairs, the ramps and the notches
    // are the way, and cliffs and pits wall both ways. Beside that walk
    // the dig walk runs, the spade's own: it takes what the legs refuse -
    // a fall may pour up to kFall, and a climb beyond the jump and a
    // drop beyond it are each crossed at kDigPenalty steps a block -
    // and the stair of one rising toward the core's named layer is a
    // level crossing outright, the marker's stair free -
    // what pours off a ledge does not walk back up it, and a hollow
    // under a floor stops outbidding the road around - priced at the
    // balance phase's own
    // say - half the errand it once stood at, that a wall comes down the
    // sooner a wood of trunks and low folds racks its detour up. Rooms
    // without headroom stay unreachable even to the spade: the walk
    // stands on surfaces, and a floor a body cannot stand under is none.
    // The knobs of it, for the balance phase to turn: `dig` is the
    // licence whole (the walk one way, the veto and the anchors stay the
    // legs' own), and dig_seconds is the host's own pace of the work,
    // one voxel of the crossing taken off a dig_seconds - the living
    // digger's own rhythm,
    // three blocks the second. The walk's price of a block is kDigPenalty
    // steps a block, and the two are separate knobs of it: the price says
    // which walls are worth the errand, the pace says how fast a chosen
    // one falls - a spade that works at the player's own speed costs a
    // wave nothing in patience and a defended town exactly its blocks.
    // z grows downward, so a climb is the negative side of the delta.
    // kWalkDrop no longer bounds a crossing: it bounds a spawn. The anchor
    // of a wave stands at its ring's own ground level, never on a perch
    // more than kWalkDrop above it - a wave begun on a canopy lands in
    // foliage its legs cannot cross.
    static constexpr int kWalkDrop = 5, kWalkClimb = 2;
    // The dead stand three tall, and no plan may promise a floor their
    // bodies cannot stand under: kStandHead open voxels overhead is what
    // every counted surface asks. The two the walk once demanded were the
    // paper's own mercy - a low tunnel mouth or a canopy's hem counted a
    // floor there, and the pack that took it piled at a standstill the
    // terrain won, wave on wave. The plan now asks what the births always
    // did, one short of the clear_of four: a floor the dead truly fit.
    static constexpr int kStandHead = 3;
    static constexpr int kFall = 63, kDigPenalty = 6;
    bool dig = true;
    double dig_seconds = 1.0 / 3.0;
    // A crossing that changes floor is a crossing apart: the pack walks
    // best on one level, and every block of climb or drop it spends is
    // priced at kLevelShy steps, so the flood keeps to the flat wherever
    // the map leaves a way around its hills and dips - a fold in the
    // ground is taken only where it is the crossing, and the walk around
    // it is worth kLevelShy steps a block. On level ground the price is
    // nothing: a step a cell, as ever.
    static constexpr int kLevelShy = 8;
    // A tread that spends the whole jump - a move of two blocks in one
    // crossing - pays kJumpShy steps on top of its own blocks of
    // climb: two stairs laid side by side, one of one and one of two,
    // and the stair of one climbs the dearer way only when nothing
    // around pays for the detour, so where both stand the pack climbs
    // one block at a time - and where the tall tread is the only way
    // up, it is walked as ever.
    static constexpr int kJumpShy = 5;
    // How many spawn points a wave comes from is the wave's own number,
    // read at wave_points: one while the route is watched, more as the
    // walk proves itself. The points are ranked by the way the walk
    // reads into each and the wave takes the first few of that order -
    // anything else is a coin toss the terrain wins.
    // How far out from its asked-for centre a corner's spawn point may be
    // sought, in rings: the anchor of a wave is the FAR END of the walk
    // inside that reach - the frontier, wherever the borderlands stop the
    // legs - so the corners a real map names in mountains and sea only
    // point at which quarter's border the wave walks in from.
    static constexpr int kSpawnSearch = 200;
};

// One zombie due at `at`, born standing on a floor of its corner's
// clearing - the site's own floor, or one of the square's around it.
// `corner` is the corner it comes from: the wave walks as one pack per
// corner, the first out in front and the rest following it.
struct SpawnAt {
    int x, y, z;
    double at;
    int corner;
};

// One treaded cell of a route: the cell on the map the line of march is
// painted on, and the surface of it the walk truly runs on. The host builds
// its route one of these at a time, and paints at that height - never on
// the top of whatever column happens to stand over the road.
struct RouteCell {
    int x, y, z;
};

class Horde {
public:
    Horde(const SolidMap& map, const Config& cfg);

    // The clock the host runs on (world_time, seconds). Moves the phases and
    // recomputes the field when it is stale.
    void tick(double now);

    Phase phase() const { return phase_; }
    int wave() const { return wave_; }
    // Seconds until the current phase ends; -1 when it is not a countdown.
    double phase_left(double now) const;
    // Whether the running wave has outlived its own clock (see
    // Config::wave_safety_seconds): latched once it fires, the host
    // reads it a tick and clears the wave as the wave word does. The
    // next birth loads the net again.
    bool safety_net_fired() const { return net_fired_; }

    // Zombies of this wave that are walking or waiting, as the brain counts
    // them: raised by every spawn taken, lowered by every kill the host
    // reports.
    int zombies_up() const { return up_; }
    // Wave done when nothing waits and nothing walks.
    bool wave_cleared() const { return pending_.empty() && up_ <= 0; }
    int core_hp() const { return core_hp_; }
    // How many zombies the last wave could truly stand up: a corner with no
    // way in the walk can take raises nothing, and the host says so when
    // the count falls short of the wave's price.
    int placed() const { return placed_; }
    // The fronts a wave `wave` comes on: its ranked first ranks that the
    // scout anchored, counted in the wave's own points - the ranks the
    // placing raises its packs on, whatever the corners are. The host
    // reads it to say one trail a front, each in its front's own colour:
    // a wave of many fronts is watched as many ways in, not one.
    int wave_fronts(int wave) const;
    // The pool the fronts come from: the whole ten, or the sites a map
    // listed when it named any at all.
    int spawn_pool() const {
        if (!cfg_.spawn_named || cfg_.spawn_count < 1)
            return Config::kSpawnSites;
        return cfg_.spawn_count < Config::kSpawnSites
                   ? cfg_.spawn_count : Config::kSpawnSites;
    }

    // The corner of `rank` in the order the last wave walked its corners
    // in: rank 0 is where the wave comes from, anchored, its route truly
    // ending at the core's doorstep, the shortest first. When the terrain
    // only offers a dead end, that reads as the rank's corner too - the
    // walk takes whatever way the map leaves it.
    int ranked_corner(int rank) const {
        if (rank < 0 || rank > Config::kSpawnSites - 1) return 0;
        if (cfg_.spawn_named && rank < lot_n_) return lot_corner_[rank];
        return rank_corner_[rank];
    }
    // The settings this runs with, for the host's announcements.
    const Config& config() const { return cfg_; }

    // The cells of the zombies due by `now`, taken out of the queue.
    bool take_due(std::vector<SpawnAt>& out, double now);
    // A zombie of the walking count went down.
    void on_zombie_killed();
    // The core took a hit. Its health floors at zero and Defeat follows.
    void on_core_damaged(int amount);
    // The share of the ramp the coming wave stands, in percent.
    void set_wave_scale(int percent) { cfg_.wave_scale = percent; }
    // The core's ceiling raised: the plating a player buys in the store
    // stands, so both the maximum and the health carried rise together
    // and a full-health core gains the whole thing.
    void add_core_max_hp(int amount);
    // A wave wiped by the host's own hand: nothing owed, nothing walking,
    // the core mended whole - and a Defeat called off with it, the
    // waiting back, so the next wave comes when the clock has it.
    // Victory is held: the rounds the settings name are the rounds kept.
    void clear_wave(double now);
    // Everything from the beginning: Prep again, the field as it stands.
    void restart(double now);

    // Whether a block at (x, y, z) may be built: false only when it would
    // seal every route from a spawn corner to the core. One walk asks the
    // one column; the field itself is the debounce's to refresh, not the
    // veto's - a veto that walked it itself asked three floods a brick,
    // and a server that goes quiet over one read to the client as one
    // gone for good.
    bool can_build(int x, int y, int z);
    // The same question for a whole line of blocks at once, its columns
    // given as (x, y) pairs: one walk holds them all out together, where
    // a brick a cell asked a walk each - a flank of pyramid is fifty
    // walks no client should wait out. Asked together, the line cannot
    // slip past the veto what its bricks alone would pass: two that seal
    // between them what neither seals alone are caught in the one ask.
    bool can_build_line(const std::vector<std::pair<int, int>>& columns);
    // The map changed (built or destroyed): the field follows, debounced.
    void on_map_edited();
    // The core moved to a cell already standing there (its column built
    // and aired by its own word): top_z is the layer of its new top
    // block, and the field walks the walkers to the new place at its
    // own next reading, the same debounce an edit has.
    void move_core(int x, int y, int top_z);

    // Where a zombie at cell (x, y) should step: the neighbour of the lowest
    // field distance, which heads to the core around obstacles. False when
    // no step helps (at the core, or nowhere reachable); a straight step
    // towards the core is tried first as the fallback, so a broken field
    // walk is a slow walk, not a standstill.
    bool next_step(int x, int y, int& tx, int& ty);
    // The same advice for a walker whose feet are at z: the read is taken
    // from the floor the body truly stands on, not the cell's cheapest one
    // - which can be a tunnel's floor the legs can never reach from where
    // they are, and a walker sent chasing it paces between the cell it
    // holds and the cell it is told to want, forever. Every neighbour's
    // candidate floor is measured against THAT footing, so the step
    // handed back is one the legs can make without letting go of the
    // ground. False on the same terms as the two-argument read; the host
    // falls back to that one when the feet are in the air or on a floor
    // the walk has no opinion about. The surface the crossing runs onto
    // is handed back in tz as well: the floor the advice is priced
    // against, footing against footing - and the stair the field prices
    // one jump says so in it, for a tread two above the feet is a climb
    // owed its own vault, not a standstill at a face left to the fling.
    // The straight line the broken field walks and the core cell both
    // keep tz at what the caller reached in with: no word of the walk,
    // no price, no vault.
    bool next_step(int x, int y, int z, int& tx, int& ty, int& tz);
    // The spade's advice: the same downhill read taken on the dig walk's
    // own field, the one that crosses what the legs refuse - a fall may
    // pour and a climb may be spaded through, both priced the dig's own
    // dear prices - so the step handed back is not always one the legs
    // can make: `tz` is the floor the crossing runs onto, and a climb of
    // more than kWalkClimb over the walker's feet is the host's own
    // signal to draw the spade, and a drop is walked off - the dead fear
    // no landing. False where the dig walk is silent, or downhill has
    // nothing to say; nothing is steered towards the core on a straight
    // line here, for the dig advice is only asked where the legs' advice
    // has already failed, and a straight line through a wall is how the
    // wall got its crowd. Consulted only on a licence of the pinned.
    bool next_dig_step(int x, int y, int z, int& tx, int& ty, int& tz);
    // Whether a cell is within `cells` of the core in the plan - the
    // height is no part of it; the teeth add the column's level, the
    // host's zombie_at_core, so a walker stands ON things, not near them.
    bool near_core(int x, int y, int cells) const;
    // The advice of the hunt: the same downhill read taken on a field
    // the hunt owns, flooded from the person the host names by
    // hunt_follow. The crossings and their prices are the same walk, so
    // a hunter walks around a wall as the pack does, never through it.
    // At the hunted cell no step helps; off the hunt field the advice
    // heads straight for the person, terrain permitting - a slow press,
    // not a standstill. False when no person is hunted, or nothing
    // helps.
    bool hunt_step(int x, int y, int& tx, int& ty);
    // The person the hunt walks to, told every tick by the host: the
    // field follows at its own cadence, one flood while the person
    // keeps their cell, a flood the moment they take another. An
    // invalid person stands the field down entirely.
    void hunt_follow(int px, int py, double now);
    // The probe readout for the host: the distance the hunt field
    // holds for the column a hunter stands in, kUnreachable when
    // the walk has no word about it - the number the map debug
    // asks of a lost chase.
    int hunt_reach(int x, int y) const;

    // The anchor the last wave walked in from at `corner`: on a map that
    // named its sites, the named ground itself - the first cell the walk
    // reaches at the asked layer within the sweep - the point wherever it
    // stands, the ground beside it wherever it does not; on any other map
    // the frontier of the walk within kSpawnSearch rings of the centre
    // its corner asked of, at its own ground level, kept from the wave
    // and its placing. False when the corner reach holds no footing (a
    // corner with no anchor raises nothing).
    bool corner_anchor(int corner, int& x, int& y) const;
    // The anchors of the corners and the ranking of their ways in: the
    // reading the waves place themselves by, and the one the host paints
    // the way in from while the world waits or walks. place_wave asks it
    // before the wave stands up; tick keeps it fresh through the waiting
    // phases, wherever the field under it was recomputed. Asked in the
    // open, for the tests read the anchors outright.
    void scout();
    // The way in from a cell, read off the walk: the cells one at a time,
    // each a legal crossing downhill, until the core's doorstep, each with
    // the surface the walk runs on it at. The route handed back is the
    // pack's line of march - the host paints it so the way can be watched.
    // False when the cell is not on the walk at all.
    bool trace_route(int x, int y, std::vector<RouteCell>& out);
    // The ground a carried pack may stand on: the floor of the cell the
    // legs themselves count, opened under the clearing a wave is born
    // in - five square, four open high. The last resort the host keeps
    // for the pack the terrain pins rides the road to this ground only
    // (see teleport_pinned on the host side): a mercy that lands on
    // honest legs-ground never over a gate. -1 when the cell holds no
    // floor the dead can be stood on.
    int stand_floor(int x, int y) const;
    // The same read off the dig walk's own field: the crossing downhill
    // may be a wall's top or a drop, for the dig walk takes them - the
    // route handed back is the spade's line of march, painted the same
    // way, that a walled town may see where its fences will be gnawed.
    // False on the same terms.
    bool trace_dig(int x, int y, std::vector<RouteCell>& out);

    // For tests: the field's distance from the core, or -1 unreachable.
    int distance(int x, int y) const;
    // The field's distance of the floor a body at z stands on - the walk's
    // own measure of whether a walker is getting anywhere, read at the
    // feet. The cell's cheapest floor can be one the legs never touch;
    // this one is the floor they are on, or -1 when no floor of the
    // column near z is on the walk.
    int distance(int x, int y, int z) const;
    // The same read off the spade's own field: the dig walk's way home
    // from the floor a body at z stands on, or -1 when no floor of the
    // column near z is on that walk. The walker the spade leads crosses
    // ground the legs price no way over, and its progress is the dig
    // walk's own count, never the silence of the field it was never on.
    int dig_distance(int x, int y, int z) const;

private:
    // The most standing surfaces one column may carry: a wall's top and the
    // floor of the tunnel through it are two; anything stacking deeper than
    // this is not stood on today.
    static constexpr int kSurfMax = 4;

    // The clearing a wave is born in: this many cells each way around the
    // site (so five by five), four voxels high.
    static constexpr int kRoomHalf = 2;

    void recompute();
    int ground(int x, int y);
    // One cell's standing surfaces: every solid run top with the two voxels
    // above it open. fill_surfaces scans the column once; the rest read the
    // cache. surface() gives the k-th surface's z, surf_count how many the
    // column carries; best_state gives the surface with the lowest field
    // distance (or -1 when the cell is off the walk).
    void fill_surfaces(int x, int y);
    int surf_count(int x, int y);
    int surface(int x, int y, int k);
    int best_state(int x, int y) const;
    // The best surface of the hunt, the same read on the hunt field.
    int best_hunt_state(int x, int y) const;
    // The dig walk's own read of the same two: the surface with the lowest
    // dig distance, and the reached floor that meets the feet at z.
    int best_dig_state(int x, int y) const;
    int near_dig_state(int x, int y, int z) const;
    // The standing surface of the column the walk has reached that meets
    // the feet at z: of the floors that carry the body, the one this side
    // of its soles. -1 when no floor the field reaches is near the
    // asking height. Ties keep the cheaper floor.
    int near_state(int x, int y, int z) const;
    // A body's standing room at the given floor: the walk demands three
    // open voxels, the three the dead stand tall, and a birth asks four,
    // a breath of margin over the plan. A birthplace asks it of its own
    // column and of every side, so no wave member rises inside a wall.
    bool clear_of(int x, int y, int z) const;
    // The clearing a wave is born in: five cells square, four voxels
    // high, every column of the square open above the floor at z. The
    // road names its site; the square gives the wave its room - the map
    // has no voxels past its edges, so a border cell's missing side is sky.
    bool box_clear(int x, int y, int z) const;
    bool reach_spawn(const std::vector<int32_t>& dist) const;
    void flood(std::vector<int32_t>& dist, int block_x, int block_y, bool dig,
               int seed_x = -1, int seed_y = -1,
               // further columns held out of the walk together, as
               // sorted cell indices: the line veto's one ask
               const std::vector<int32_t>* held = nullptr);
    // The asked-for centres, dealt at the horde's birth and again the
    // moment each wave falls when roam_spawns is on: the corners'
    // quarters shuffle round the compass and each centre is drawn
    // inside its own quarter's ring band around the town, so the
    // wave's fronts spread round it wherever the terrain then lets
    // their ways run - and the intermission that follows a fall
    // already paints the trail of the round the next wave stands in.
    // Informs the scout's reading and the veto's (both read
    // roam_x_/roam_y_).
    void roam();
    // The fronts of the wave the reading prepares, drawn from the named
    // pool by lot when the wave opens fewer of them than the pool holds
    // (a map that named no site keeps the scout ranking whole).
    void draw_lot();
    void begin_wave(double now);
    void place_wave(double now);

    const SolidMap& map_;
    Config cfg_;
    Phase phase_ = Phase::Prep;
    int wave_ = 0;
    int up_ = 0;
    int core_hp_;
    double phase_until_ = 0.0;
    std::vector<SpawnAt> pending_;
    // The field: a distance per standing surface (kSurfMax of them a cell),
    // and the z of every surface the columns carry, scanned once a recompute
    // (surf_n_ of -1: this column is not scanned yet).
    std::vector<int32_t> dist_;
    // The spade's own field: the same surfaces, the dig walk's own prices
    // on the crossings between them. Its distance is never higher than
    // the legs' own, and it is only read on the licences of the pinned.
    std::vector<int32_t> dig_dist_;
    // The hunt field: the same surfaces, the same prices, flooded from
    // the person the host names instead of the core, at most once each
    // kHuntRefresh while the person holds their cell, and the moment
    // they take another. One walk for every hunter, as the core field
    // is one walk for every grunt.
    std::vector<int32_t> hunt_dist_;
    int hunt_x_ = -1, hunt_y_ = -1;
    double hunt_at_ = -1e9;
    static constexpr double kHuntRefresh = 1.0;
    std::vector<int16_t> surf_z_;
    std::vector<int8_t> surf_n_;
    bool field_dirty_ = false;
    double field_at_ = 0.0;
    // Whether the anchors and the ranking still stand on the field as it
    // was last recomputed: false once the field moved, scouted again by
    // the next tick's waiting phase or the next wave's placing.
    bool scouted_ = false;
    // The safety net clock: the moment the running wave outlives,
    // -1 while none runs and while the net is off. net_fired_ latches
    // the take - one clear a stalled wave, reloaded at the next birth.
    double wave_deadline_at_ = -1.0;
    bool net_fired_ = false;
    // Where the wave last walked in from, one anchor a spawn point (-1
    // for none): the route the host paints is traced from here.
    int anchor_x_[Config::kSpawnSites] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int anchor_y_[Config::kSpawnSites] = {0};
    // The layer the anchor itself stands on, the reading scout chose of
    // its cell: on a map that named its sites, the ground the name asked
    // of, whatever floor the walk likes better there - the wave rises
    // on it, not on the roof the legs reach first. -1 for none.
    int anchor_z_[Config::kSpawnSites] = {-1, -1, -1, -1, -1,
                                        -1, -1, -1, -1, -1};
    // The spawn points in the order the last wave walked them (rank 0
    // first), and how many zombies that wave truly stood up.
    int rank_corner_[Config::kSpawnSites] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int placed_ = 0;
    // The asked-for centres the scout sweeps round and the veto reads:
    // the settings' own until the horde roams them (see roam()). The
    // deterministic tests get the settings' quarters; the game deals
    // its own, at birth and again as each wave falls.
    int roam_x_[Config::kSpawnSites] = {};
    int roam_y_[Config::kSpawnSites] = {};
    // The fronts drawn by lot for the wave draw_lot prepared: the corners
    // a named pool wave truly comes from, in scout order between them,
    // kept across field edits so a walking wave never re deals the set
    // it already placed.
    int lot_corner_[Config::kSpawnSites] = {};
    int lot_n_ = 0;
    int lot_wave_ = -1;
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace horde
