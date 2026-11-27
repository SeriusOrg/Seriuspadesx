#pragma once
// The Tower Defense store's catalogue: the things a defender can buy with
// Zedcreds, and what each one does. Header-only and dependency free so the
// server, the client and the tests can all read the same table.
//
// Zedcreds are the mode's own coin - earned from kills and cleared waves,
// spent on guns, perks and the core. They are NOT the building blocks in
// your pack: blocks are dug and placed and refilled for free; Zedcreds are
// only ever paid out by the horde and paid in at this store. The client
// draws them as a little voxel cube, the way the mode earns them one
// zombie at a time.
//
// Item indices are the wire's own names. Append new items at the END: the
// state packet sends its own count, so an older client simply shows fewer
// cards while an older server never sends the new ones at all.

#include <cstddef>
#include <cstdint>
#include <string>

namespace training {
namespace store {

// The coin's name, as every screen says it.
constexpr const char* kCurrencyName = "Zedcred";
constexpr const char* kCurrencyPlural = "Zedcreds";
constexpr const char* kCurrencySingular = "Zedcred";

enum Category : uint8_t {
    kGuns = 0,
    kPerks = 1,
    kCore = 2,
    kCategoryCount = 3
};

// What buying an item does on the server. The switch in Server.cpp's
// store_command is the one place these are carried out.
enum Effect : uint8_t {
    kEffectWeapon = 0,     // permanently unlocks a gun (arg is the gun)
    kEffectGunDamage,      // +1 damage band for a gun (arg is the gun)
    kEffectAmmo,           // refill the held gun's reserve
    kEffectGrenades,       // +1 grenade
    kEffectHealth,         // full heal
    kEffectBlocks,         // +10 building blocks
    kEffectMaxHp,          // +25 max health, permanent
    kEffectSpeed,          // +10% move speed, permanent
    kEffectCoreRepair,     // +25 core health
    kEffectCoreMaxHp,      // +25 max core health, permanent
    kEffectTurret,         // place/raise a sentry (arg is level)
    kEffectWeaponSkin,     // own a weapon's dress (arg: gun, or 3 = spade)
};

struct Item {
    const char* sku;        // wire token and chat command: /buy <sku>
    const char* label;      // card caption
    const char* detail;     // one short line under the caption
    int price;              // default, overridable by towerdefense.toml [store]
    uint8_t category;
    uint8_t effect;
    uint8_t arg;            // gun index / turret level / amount, per effect
    uint8_t max_level;      // how many times it may be bought (1 = once)
    uint8_t prereq;         // index of a required item, or 255 for none
    const char* model;      // KV6 thumbnail path, or nullptr for a drawn icon
    const char* icon;        // Gfx/Hotbar sprite fallback, or nullptr
    // A weapon dress rides in its own pak folder, and the client puts
    // that folder in front of the stock paths the skin scripts load:
    // nullptr for everything that is not a dress. A dress also sits in
    // its class's upgrade ladder - tier 1, 2 or 3 - and the class wears
    // the highest tier its owner has bought: 0 for everything that is
    // not a dress.
    const char* dress;
    uint8_t tier;
};

constexpr uint8_t kNone = 255;

// Thumbnails: the stock first-person bodies in the base pak, and the sentry
// heads from the Tower Defense pak. A card's VoxelModelPreview rasterizes one
// of these; a missing file falls back to the card's Hotbar sprite, and a
// missing sprite to a drawn placeholder.
constexpr Item kItems[] = {
    // --- Guns and gun cards -------------------------------------------------
    // The Weapons shelf holds only the weapons themselves and their
    // dresses; the rounds, the ammo and the sentries below keep their
    // wire places on the Perks and Core shelves, for an item's index is
    // its name on the wire and never moves.
    {"rifle", "Rifle", "Standard issue. Always yours.", 0,
     kGuns, kEffectWeapon, 0, 1, kNone,
     "Models/Weapons/Rifle/Weapon.kv6", "Gfx/Hotbar/Rifle.png"},
    {"smg", "SMG", "Fast and light.", 20,
     kGuns, kEffectWeapon, 1, 1, kNone,
     "Models/Weapons/SMG/Weapon.kv6", "Gfx/Hotbar/SMG.png"},
    {"shotgun", "Shotgun", "A close-range answer.", 30,
     kGuns, kEffectWeapon, 2, 1, kNone,
     "Models/Weapons/Shotgun/Weapon.kv6", "Gfx/Hotbar/Shotgun.png"},
    {"rifle_dmg1", "Rifle: heavy round", "+1 damage band, rifle.", 25,
     kPerks, kEffectGunDamage, 0, 1, 0,
     "Models/Weapons/Rifle/Weapon.kv6", "Gfx/Hotbar/Rifle.png"},
    {"rifle_dmg2", "Rifle: heaviest", "Another damage band.", 40,
     kPerks, kEffectGunDamage, 0, 1, 3,
     "Models/Weapons/Rifle/Weapon.kv6", "Gfx/Hotbar/Rifle.png"},
    {"smg_dmg1", "SMG: heavy round", "+1 damage band, SMG.", 30,
     kPerks, kEffectGunDamage, 1, 1, 1,
     "Models/Weapons/SMG/Weapon.kv6", "Gfx/Hotbar/SMG.png"},
    {"smg_dmg2", "SMG: heaviest", "Another damage band.", 45,
     kPerks, kEffectGunDamage, 1, 1, 5,
     "Models/Weapons/SMG/Weapon.kv6", "Gfx/Hotbar/SMG.png"},
    {"shotgun_dmg1", "Shotgun: buck shot", "+1 damage band, shotgun.", 35,
     kPerks, kEffectGunDamage, 2, 1, 2,
     "Models/Weapons/Shotgun/Weapon.kv6", "Gfx/Hotbar/Shotgun.png"},
    {"shotgun_dmg2", "Shotgun: slug", "Another damage band.", 50,
     kPerks, kEffectGunDamage, 2, 1, 7,
     "Models/Weapons/Shotgun/Weapon.kv6", "Gfx/Hotbar/Shotgun.png"},
    {"ammo", "Ammo refill", "Reserve back to full.", 5,
     kPerks, kEffectAmmo, 0, 255, kNone,
     nullptr, "Gfx/Hotbar/Grenade.png"},
    {"grenades", "Grenade", "One more to throw.", 6,
     kPerks, kEffectGrenades, 1, 255, kNone,
     "Models/Weapons/Grenade/Grenade.kv6", "Gfx/Hotbar/Grenade.png"},

    // --- Perks --------------------------------------------------------------
    {"hp1", "Tough skin I", "+25 max health.", 15,
     kPerks, kEffectMaxHp, 25, 1, kNone,
     nullptr, nullptr},
    {"hp2", "Tough skin II", "+25 max health.", 25,
     kPerks, kEffectMaxHp, 25, 1, 11,
     nullptr, nullptr},
    {"hp3", "Tough skin III", "+25 max health.", 40,
     kPerks, kEffectMaxHp, 25, 1, 12,
     nullptr, nullptr},
    {"speed1", "Light feet I", "+10% move speed.", 20,
     kPerks, kEffectSpeed, 10, 1, kNone,
     nullptr, nullptr},
    {"speed2", "Light feet II", "+10% move speed.", 35,
     kPerks, kEffectSpeed, 10, 1, 14,
     nullptr, nullptr},
    {"health", "Field dressing", "Heal to full.", 8,
     kPerks, kEffectHealth, 0, 255, kNone,
     nullptr, nullptr},
    {"blocks", "Block bundle", "+10 building blocks.", 4,
     kPerks, kEffectBlocks, 10, 255, kNone,
     nullptr, nullptr},

    // --- Core ---------------------------------------------------------------
    {"repair", "Patch the core", "Restore 25 core health.", 12,
     kCore, kEffectCoreRepair, 25, 255, kNone,
     nullptr, nullptr},
    {"core_hp1", "Core plating I", "+25 max core health.", 18,
     kCore, kEffectCoreMaxHp, 25, 1, kNone,
     nullptr, nullptr},
    {"core_hp2", "Core plating II", "+25 max core health.", 30,
     kCore, kEffectCoreMaxHp, 25, 1, 19,
     nullptr, nullptr},
    {"core_hp3", "Core plating III", "+25 max core health.", 50,
     kCore, kEffectCoreMaxHp, 25, 1, 20,
     nullptr, nullptr},

    // --- Sentries (the Core shelf) -------------------------------------------
    {"turret1", "Sentry: SMG", "A level 1 pillar.", 5,
     kCore, kEffectTurret, 1, 255, kNone,
     "Models/Turrets/TurretHeadMG1.kv6", nullptr},
    {"turret2", "Sentry: MG", "Raise to level 2.", 10,
     kCore, kEffectTurret, 2, 255, 22,
     "Models/Turrets/TurretHeadMG2.kv6", nullptr},
    {"turret3", "Sentry: Minigun", "Raise to level 3.", 30,
     kCore, kEffectTurret, 3, 255, 23,
     "Models/Turrets/TurretHeadMG3.kv6", nullptr},

    // --- Weapon dresses ------------------------------------------------------
    // The mods the archive carries, a dress each: a model set the client
    // puts on the class's own weapon. Owning the card is the whole of
    // the server's effect; the client reads the ownership and dresses
    // the weapon it holds, the arg saying what - 0 rifle, 1 SMG,
    // 2 shotgun, 3 spade. There is no worn-or-put-away choice to make:
    // a class wears the highest tier its owner has bought, so a class's
    // dresses are tiers of one ladder, each asking for the one below it
    // (prereq), and the stock gun is where every ladder starts. A new
    // skin is a new rung: a tier-2 card with the tier-1 card as its
    // prereq, at a price the ladder's climb should ask.
    {"sv99", "SV99 Forest Camo", "A bolt sniper's dress for the rifle.", 60,
     kGuns, kEffectWeaponSkin, 0, 1, kNone,
     "WeaponMods/SV99_Forest_Camo/Models/Weapons/Rifle/Weapon.kv6",
     nullptr, "WeaponMods/SV99_Forest_Camo/", 1},
    {"minigun", "M134 Minigun", "A handheld minigun's dress for the SMG.", 80,
     kGuns, kEffectWeaponSkin, 1, 1, kNone,
     "WeaponMods/M134_Minigun/Models/Weapons/SMG/Weapon.kv6",
     nullptr, "WeaponMods/M134_Minigun/", 1},
    {"sword", "Bastard Sword", "A bastard sword's dress for the spade.", 40,
     kGuns, kEffectWeaponSkin, 3, 1, kNone,
     "WeaponMods/Bastard_Sword/Models/Weapons/Spade/Spade.kv6",
     nullptr, "WeaponMods/Bastard_Sword/", 1},
};

constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

inline int find_item(const char* sku) {
    for (size_t i = 0; i < kItemCount; ++i)
        if (std::string(sku) == kItems[i].sku) return static_cast<int>(i);
    return -1;
}

constexpr const char* category_name(uint8_t cat) {
    switch (cat) {
        case kGuns:  return "Weapons";
        case kPerks: return "Perks";
        case kCore:  return "Core";
        default:     return "?";
    }
}

} // namespace store
} // namespace training
