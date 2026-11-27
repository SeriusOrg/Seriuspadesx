#pragma once
// The Tower Defense weapon ladders: the numbers a class fights with at
// each rung of the ladder the store sells. Header-only and dependency
// free, like the catalogue beside it, so the server, the client and the
// tests all read the same table - the server pays the damage out of it,
// the client aims, reloads and shows its numbers by it, and the two
// must never disagree about what a gun does.
//
// Rung 0 is the stock gun: the training range and every world the store
// never stood in fight with it, so its row is the classic table,
// untouched. Rungs 1 to 3 are the ladder - the first rung is the class's
// own starting shape (free, granted at spawn for the classes whose
// first rung is a dress), and each rung above it is bought with the
// one below it as its prerequisite.

namespace training {
namespace tiers {

    // The guns, one row a rung: rifle 0, SMG 1, shotgun 2.
    struct GunSpec {
        float delay;        // seconds between shots
        int clip;           // rounds the magazine holds
        int stock;          // rounds the reserve starts at
        float reload_time;  // seconds a reload takes (slow: per shell)
        bool slow;          // true when the reload fills one shell at a time
        int damage[4];      // torso, head, arms, legs
        float recoil;       // the view kick, as a share of the stock gun's
    };

    constexpr GunSpec kGuns[3][4] = {
        {   // rifle: the M14, the Ballista, the SV99. The M14 is the
            // class's own starting shape: its trigger speaks at the
            // base cadence, only its clip and its rounds run lighter.
            {0.5F, 10, 50, 2.5F, false, {49, 100, 33, 33}, 1.0F},
            {0.5F, 7, 35, 2.5F, false, {29, 60, 20, 20}, 1.0F},
            {0.8333F, 10, 50, 2.5F, false, {49, 100, 33, 33}, 1.0F},
            {0.6667F, 11, 55, 2.5F, false, {98, 200, 66, 66}, 1.0F},
        },
        {   // SMG: the AK-47, the M4 Magpul, the M134
            {0.11F, 30, 120, 2.5F, false, {29, 75, 18, 18}, 1.0F},
            {0.11F, 24, 96, 2.5F, false, {23, 60, 14, 14}, 2.0F},
            {0.11F, 40, 160, 2.5F, false, {29, 75, 18, 18}, 0.5F},
            {0.055F, 240, 480, 2.5F, false, {58, 150, 36, 36}, 2.0F},
        },
        {   // shotgun: the stock shape, the SPAS-12, the Remington
            {1.0F, 6, 48, 0.5F, true, {27, 37, 16, 16}, 1.0F},
            {1.0F, 6, 48, 0.5F, true, {22, 30, 13, 13}, 1.0F},
            {1.0F, 8, 64, 0.5F, true, {32, 44, 19, 19}, 1.0F},
            {1.0F, 12, 96, 0.5F, true, {54, 74, 32, 32}, 1.0F},
        },
    };

    // The spade's own ladder, indexed by rung: rung 0 the stock spade
    // (a world the store never stood in), rung 1 the ladder's floor -
    // the stock shape is the spade's first rung, no dress sold for it -
    // rung 2 the Bastard Sword, rung 3 the Lightsaber.
    struct SpadeSpec {
        float range;        // the swing's reach, vs the stock spade
        int melee_damage;   // the swing's damage on what stands in it
        float chip;         // the swing's bite on a block, vs stock
    };

    constexpr SpadeSpec kSpade[4] = {
        {1.0F, 100, 1.0F},   // stock
        {1.0F, 100, 1.0F},   // tier 1: the ladder's floor, the stock shape
        {1.5F, 100, 1.0F},   // tier 2: a longer arm
        {1.5F, 200, 2.0F},   // tier 3: the long arm, the doubled bite
    };

} // namespace tiers
} // namespace training
