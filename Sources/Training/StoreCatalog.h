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
    kEffectWeaponSkin,     // own a weapon's dress (arg: the gun dressed)
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
    // its class's upgrade ladder - tier 1 to 4 - and the class wears
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
    {"rifle", "Rifle", "description coming soon", 0,
     kGuns, kEffectWeapon, 0, 1, kNone,
     "Models/Weapons/Rifle/Weapon.kv6", "Gfx/Hotbar/Rifle.png"},
    {"smg", "SMG", "description coming soon", 20,
     kGuns, kEffectWeapon, 1, 1, kNone,
     "Models/Weapons/SMG/Weapon.kv6", "Gfx/Hotbar/SMG.png"},
    {"shotgun", "Shotgun", "description coming soon", 30,
     kGuns, kEffectWeapon, 2, 1, kNone,
     "Models/Weapons/Shotgun/Weapon.kv6", "Gfx/Hotbar/Shotgun.png"},
    {"rifle_dmg1", "Rifle: heavy round", "description coming soon", 25,
     kPerks, kEffectGunDamage, 0, 1, 0,
     "Models/Weapons/Rifle/Weapon.kv6", "Gfx/Hotbar/Rifle.png"},
    {"rifle_dmg2", "Rifle: heaviest", "description coming soon", 40,
     kPerks, kEffectGunDamage, 0, 1, 3,
     "Models/Weapons/Rifle/Weapon.kv6", "Gfx/Hotbar/Rifle.png"},
    {"smg_dmg1", "SMG: heavy round", "description coming soon", 30,
     kPerks, kEffectGunDamage, 1, 1, 1,
     "Models/Weapons/SMG/Weapon.kv6", "Gfx/Hotbar/SMG.png"},
    {"smg_dmg2", "SMG: heaviest", "description coming soon", 45,
     kPerks, kEffectGunDamage, 1, 1, 5,
     "Models/Weapons/SMG/Weapon.kv6", "Gfx/Hotbar/SMG.png"},
    {"shotgun_dmg1", "Shotgun: buck shot", "description coming soon", 35,
     kPerks, kEffectGunDamage, 2, 1, 2,
     "Models/Weapons/Shotgun/Weapon.kv6", "Gfx/Hotbar/Shotgun.png"},
    {"shotgun_dmg2", "Shotgun: slug", "description coming soon", 50,
     kPerks, kEffectGunDamage, 2, 1, 7,
     "Models/Weapons/Shotgun/Weapon.kv6", "Gfx/Hotbar/Shotgun.png"},
    {"ammo", "Ammo refill", "description coming soon", 5,
     kPerks, kEffectAmmo, 0, 255, kNone,
     nullptr, "Gfx/Hotbar/Grenade.png"},
    {"grenades", "Grenade", "description coming soon", 6,
     kPerks, kEffectGrenades, 1, 255, kNone,
     "Models/Weapons/Grenade/Grenade.kv6", "Gfx/Hotbar/Grenade.png"},

    // --- Perks --------------------------------------------------------------
    {"hp1", "Tough skin I", "description coming soon", 15,
     kPerks, kEffectMaxHp, 25, 1, kNone,
     nullptr, nullptr},
    {"hp2", "Tough skin II", "description coming soon", 25,
     kPerks, kEffectMaxHp, 25, 1, 11,
     nullptr, nullptr},
    {"hp3", "Tough skin III", "description coming soon", 40,
     kPerks, kEffectMaxHp, 25, 1, 12,
     nullptr, nullptr},
    {"speed1", "Light feet I", "description coming soon", 20,
     kPerks, kEffectSpeed, 10, 1, kNone,
     nullptr, nullptr},
    {"speed2", "Light feet II", "description coming soon", 35,
     kPerks, kEffectSpeed, 10, 1, 14,
     nullptr, nullptr},
    {"health", "Field dressing", "description coming soon", 8,
     kPerks, kEffectHealth, 0, 255, kNone,
     nullptr, nullptr},
    {"blocks", "Block bundle", "description coming soon", 4,
     kPerks, kEffectBlocks, 10, 255, kNone,
     nullptr, nullptr},

    // --- Core ---------------------------------------------------------------
    {"repair", "Patch the core", "description coming soon", 12,
     kCore, kEffectCoreRepair, 25, 255, kNone,
     nullptr, nullptr},
    {"core_hp1", "Core plating I", "description coming soon", 18,
     kCore, kEffectCoreMaxHp, 25, 1, kNone,
     nullptr, nullptr},
    {"core_hp2", "Core plating II", "description coming soon", 30,
     kCore, kEffectCoreMaxHp, 25, 1, 19,
     nullptr, nullptr},
    {"core_hp3", "Core plating III", "description coming soon", 50,
     kCore, kEffectCoreMaxHp, 25, 1, 20,
     nullptr, nullptr},

    // --- Sentries (the Core shelf) -------------------------------------------
    {"turret1", "Sentry: SMG", "description coming soon", 5,
     kCore, kEffectTurret, 1, 255, kNone,
     "Models/Turrets/TurretHeadMG1.kv6", nullptr},
    {"turret2", "Sentry: MG", "description coming soon", 10,
     kCore, kEffectTurret, 2, 255, 22,
     "Models/Turrets/TurretHeadMG2.kv6", nullptr},
    {"turret3", "Sentry: Minigun", "description coming soon", 30,
     kCore, kEffectTurret, 3, 255, 23,
     "Models/Turrets/TurretHeadMG3.kv6", nullptr},

    // --- Weapon dresses ------------------------------------------------------
    // The mods the archive carries, a dress each: a model set the client
    // puts on the class's own weapon. Owning the card is the whole of
    // the server's effect; the client reads the ownership and dresses
    // the weapon it holds, the arg saying what - 0 rifle, 1 SMG,
    // 2 shotgun, 3 spade, 4 grenade. There is no worn-or-put-away
    // choice to make: a class wears the highest tier its owner has
    // bought, so a class's dresses are tiers of one ladder, each
    // asking for the one below it (prereq), and the stock gun is where
    // every ladder starts. The rifle and the SMG climb four rungs,
    // their tier 1 a dress of the gun's own (free: a dress priced at
    // zero is granted to every defender at spawn, so the ladder's
    // floor is worn from the first breath); the shotgun and the spade
    // start theirs at tier 2, their stock shape being their tier 1;
    // the grenade has one rung. A tier is looks AND numbers: the
    // stats a class fights with climb the ladder in WeaponTiers.h.
    // The charm a dressed gun shows rides on the tier: bullet at 1,
    // skull at 2, creep at 3 and 4.
    {"rifle_lvl1", "M14 Battle Rifle", "description coming soon", 0,
     kGuns, kEffectWeaponSkin, 0, 1, kNone,
     "WeaponMods/RifleLvl1/Models/Weapons/Rifle/Weapon.kv6",
     nullptr, "WeaponMods/RifleLvl1/", 1},
    {"rifle_lvl2", "Ballista", "description coming soon", 100,
     kGuns, kEffectWeaponSkin, 0, 1, 25,
     "WeaponMods/RifleLvl2/Models/Weapons/Rifle/WeaponNoMagazine.kv6",
     nullptr, "WeaponMods/RifleLvl2/", 2},
    {"rifle_lvl3", "Rifolver", "description coming soon", 150,
     kGuns, kEffectWeaponSkin, 0, 1, 26,
     "WeaponMods/RifleLvl3/Models/Weapons/Rifle/Weapon.kv6",
     nullptr, "WeaponMods/RifleLvl3/", 3},
    {"smg_lvl1", "MP5K", "description coming soon", 0,
     kGuns, kEffectWeaponSkin, 1, 1, kNone,
     "WeaponMods/SMGLvl1/Models/Weapons/SMG/WeaponFP.kv6",
     nullptr, "WeaponMods/SMGLvl1/", 1},
    {"smg_lvl2", "AK-47", "description coming soon", 100,
     kGuns, kEffectWeaponSkin, 1, 1, 28,
     "WeaponMods/SMGLvl2/Models/Weapons/SMG/Weapon.kv6",
     nullptr, "WeaponMods/SMGLvl2/", 2},
    {"smg_lvl3", "M4 Magpul", "description coming soon", 150,
     kGuns, kEffectWeaponSkin, 1, 1, 29,
     "WeaponMods/SMGLvl3/Models/Weapons/SMG/Weapon.kv6",
     nullptr, "WeaponMods/SMGLvl3/", 3},
    {"shotgun_lvl2", "SPAS-12", "description coming soon", 60,
     kGuns, kEffectWeaponSkin, 2, 1, kNone,
     "WeaponMods/ShotgunLvl2/Models/Weapons/Shotgun/Weapon.kv6",
     nullptr, "WeaponMods/ShotgunLvl2/", 2},
    {"shotgun_lvl3", "Remington", "description coming soon", 120,
     kGuns, kEffectWeaponSkin, 2, 1, 31,
     "WeaponMods/ShotgunLvl3/Models/Weapons/Shotgun/WeaponNoPump.kv6",
     nullptr, "WeaponMods/ShotgunLvl3/", 3},
    {"spade_lvl2", "Bastard Sword", "description coming soon", 30,
     kGuns, kEffectWeaponSkin, 3, 1, kNone,
     "WeaponMods/SpadeLvl2/Models/Weapons/Spade/Spade.kv6",
     nullptr, "WeaponMods/SpadeLvl2/", 2},
    {"spade_lvl3", "Lightsaber", "description coming soon", 60,
     kGuns, kEffectWeaponSkin, 3, 1, 33,
     "WeaponMods/SpadeLvl3/Models/Weapons/Spade/Spade.kv6",
     nullptr, "WeaponMods/SpadeLvl3/", 3},
    {"rpg", "RPG-7", "description coming soon", 120,
     kGuns, kEffectWeaponSkin, 4, 1, kNone,
     "WeaponMods/GrenadeRPG/Models/Weapons/Grenade/Grenade.kv6",
     nullptr, "WeaponMods/GrenadeRPG/", 1},
    {"rifle_lvl4", "SV99 Forest Camo", "description coming soon", 200,
     kGuns, kEffectWeaponSkin, 0, 1, 27,
     "WeaponMods/RifleLvl4/Models/Weapons/Rifle/Weapon.kv6",
     nullptr, "WeaponMods/RifleLvl4/", 4},
    {"smg_lvl4", "M134 Minigun", "description coming soon", 200,
     kGuns, kEffectWeaponSkin, 1, 1, 30,
     "WeaponMods/SMGLvl4/Models/Weapons/SMG/Weapon.kv6",
     nullptr, "WeaponMods/SMGLvl4/", 4},
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
