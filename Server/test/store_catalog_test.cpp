// The Tower Defense store's catalogue, as the server, the client and the
// chat commands all read it: the SKUs are unique, the wires' own names match
// the words a player types, every prerequisite names an item that exists and
// is not itself, and every price is one the store can be paid.
#include <Training/Protocol.h>
#include <Training/StoreCatalog.h>
#include <Training/WeaponTiers.h>
#include <Client/StoreParser.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

namespace {
    const training::store::Item& item(size_t i) {
        return training::store::kItems[i];
    }
}

int main() {
    const size_t n = training::store::kItemCount;

    // --- the table is well formed ------------------------------------------
    CHECK(n > 0);
    CHECK(n <= 64);   // the wire carries at most 64 owned pairs
    for (size_t i = 0; i < n; ++i) {
        // A SKU and a label are the words a player and a screen read.
        CHECK(item(i).sku != nullptr && item(i).sku[0] != '\0');
        CHECK(item(i).label != nullptr && item(i).label[0] != '\0');
        // A card is on one of the store's shelves.
        CHECK(item(i).category < training::store::kCategoryCount);
        // An effect is one the server knows how to carry out.
        CHECK(item(i).effect <= training::store::kEffectWeaponSkin);
        // A price is never negative.
        CHECK(item(i).price >= 0);
        // A card may be bought at least once.
        CHECK(item(i).max_level >= 1 || item(i).max_level == 255);
        // A card names a model to rasterize, or a sprite to draw, or it is one
        // of the kinds the store draws its own little voxel cube for.
        const auto eff = item(i).effect;
        const bool drawn = eff == training::store::kEffectMaxHp ||
                           eff == training::store::kEffectSpeed ||
                           eff == training::store::kEffectCoreMaxHp ||
                           eff == training::store::kEffectCoreRepair ||
                           eff == training::store::kEffectHealth ||
                           eff == training::store::kEffectBlocks;
        CHECK(item(i).model != nullptr || item(i).icon != nullptr || drawn);
        // A dress names the pak folder its files ride in, and dresses a
        // gun the wire knows (or the spade, at 3, the grenade, at 4).
        if (eff == training::store::kEffectWeaponSkin) {
            CHECK(item(i).dress != nullptr && item(i).dress[0] != '\0');
            CHECK(item(i).arg <= 4);
        }
        // A dress is a rung of its class's upgrade ladder, and nothing
        // else sits on a ladder: the class wears the highest tier its
        // owner has bought, so a tier is 1 to 4.
        if (eff == training::store::kEffectWeaponSkin)
            CHECK(item(i).tier >= 1 && item(i).tier <= 4);
        else
            CHECK(item(i).tier == 0);
    }

    // --- the SKUs are unique, both as words and as find_item inputs --------
    for (size_t i = 0; i < n; ++i)
        for (size_t k = i + 1; k < n; ++k) {
            CHECK(std::strcmp(item(i).sku, item(k).sku) != 0);
            CHECK(std::strcmp(item(i).label, item(k).label) != 0);
        }

    // --- find_item answers every SKU, and nothing else --------------------
    for (size_t i = 0; i < n; ++i)
        CHECK(training::store::find_item(item(i).sku) == static_cast<int>(i));
    CHECK(training::store::find_item("no_such_card") == -1);
    CHECK(training::store::find_item("") == -1);
    CHECK(training::store::find_item("Turret") == -1);   // the words are exact

    // --- the guns are the three the wire carries ---------------------------
    // The unlock gate reads weapon_open by the gun index the effect names.
    std::vector<int> gun_sku;
    for (size_t i = 0; i < n; ++i)
        if (item(i).effect == training::store::kEffectWeapon) {
            CHECK(item(i).arg < 3);
            gun_sku.push_back(item(i).arg);
        }
    CHECK(gun_sku.size() == 3);
    for (int g : gun_sku) CHECK(g >= 0 && g <= 2);

    // The starter rifle is free, and the rest are not.
    for (size_t i = 0; i < n; ++i)
        if (item(i).effect == training::store::kEffectWeapon &&
            item(i).arg == 0)
            CHECK(item(i).price == 0);

    // --- the prerequisites hang together ------------------------------------
    for (size_t i = 0; i < n; ++i) {
        if (item(i).prereq == training::store::kNone) continue;
        // A prerequisite names an item in the table, and not the card itself.
        CHECK(item(i).prereq < n);
        CHECK(item(i).prereq != i);
        // A card is asked for after the one it needs.
        CHECK(item(i).prereq < i);
    }

    // --- a class's dress tiers are distinct ----------------------------------
    // The class wears the highest tier bought, so two dresses of one
    // class on one tier would put two bests on one gun.
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (item(i).effect == training::store::kEffectWeaponSkin &&
                item(j).effect == training::store::kEffectWeaponSkin &&
                item(i).arg == item(j).arg)
                CHECK(item(i).tier != item(j).tier);

    // --- the ladders the classes climb -----------------------------------
    // WeaponTiers.h: rung 0 the classic table, rungs 1 to 4 the stats the
    // store sells. The server pays its damage out of this table and the
    // client aims, reloads and shows its numbers by it, so the classic row
    // is pinned hard: drift there would change every world the store
    // never stood in.
    for (int g = 0; g < 3; ++g) {
        const auto& stock = training::tiers::kGuns[g][0];
        CHECK(stock.delay > 0.0F && stock.delay < 2.0F);
        CHECK(stock.clip > 0);
        CHECK(stock.stock >= stock.clip);
        CHECK(stock.reload_time > 0.0F);
        for (int d : stock.damage) CHECK(d > 0);
        CHECK(stock.recoil == 1.0F);
        for (int t = 1; t <= 4; ++t) {
            const auto& rung = training::tiers::kGuns[g][t];
            CHECK(rung.delay > 0.0F);
            for (int d : rung.damage) CHECK(d > 0);
            CHECK(rung.recoil > 0.0F);
            if (t == 1)
                continue;
            // A paid rung never shrinks the magazine nor the reserve
            // below the rung below it (the floor may sit under the
            // stock gun: the ladder starts at the class's own shape).
            const auto& prev = training::tiers::kGuns[g][t - 1];
            CHECK(rung.clip >= prev.clip);
            CHECK(rung.stock >= prev.stock);
        }
    }
    // The spade's own ladder: the stock reach at the floor, the longer
    // arm from rung 2, the doubled bite at the top.
    CHECK(training::tiers::kSpade[0].range == 1.0F);
    CHECK(training::tiers::kSpade[1].range == 1.0F);
    CHECK(training::tiers::kSpade[2].range == 1.5F);
    CHECK(training::tiers::kSpade[3].range == 1.5F);
    CHECK(training::tiers::kSpade[3].melee_damage ==
          2 * training::tiers::kSpade[2].melee_damage);
    CHECK(training::tiers::kSpade[3].chip == 2.0F);

    // --- the ladder's floor is free --------------------------------------
    // A dress priced at zero is granted to every defender at spawn, so
    // the first rungs the classes wear are free: exactly the two whose
    // first rung is a dress of the gun's own.
    {
        int free_dresses = 0;
        for (size_t i = 0; i < n; ++i)
            if (item(i).effect == training::store::kEffectWeaponSkin &&
                item(i).price == 0 && item(i).tier == 1)
                ++free_dresses;
        CHECK(free_dresses == 2);
    }

    // --- every shelf is stocked --------------------------------------------
    for (int cat = 0; cat < training::store::kCategoryCount; ++cat) {
        int count = 0;
        for (size_t i = 0; i < n; ++i)
            if (item(i).category == cat) ++count;
        CHECK(count > 0);
        CHECK(training::store::category_name(static_cast<uint8_t>(cat)) !=
              nullptr);
    }

    // --- the currency is one, and it is not the building block -------------
    CHECK(std::string(training::store::kCurrencyName) == "Zedcred");
    CHECK(std::string(training::store::kCurrencyPlural) == "Zedcreds");
    CHECK(std::strcmp(training::store::kCurrencyName, "Block") != 0);

    // --- the wire, read back as it was written -------------------------------
    // The client drops a Store whose bounds say no; the writer's own first
    // word - a fresh player who owns nothing - is eleven bytes, and saying
    // more dropped exactly that one, which is the whole of a new world's
    // store. So the round trip is checked with nothing owned at all, as
    // well as with a purse full of cards.
    for (const std::uint8_t owned_count : {0, 1, 4, 25}) {
        proto::StoreState out;
        out.zedcred = 1234;
        out.wave = 7;
        out.phase = 2;
        out.core_hp = 90;
        out.core_max_hp = 175;
        out.weapon = 2;
        out.grenades = 3;
        out.blocks = 41;
        out.owned_count = owned_count;
        for (std::uint8_t k = 0; k < owned_count; ++k) {
            out.owned[k].index = static_cast<std::uint8_t>((k * 7) % n);
            out.owned[k].level = static_cast<std::uint8_t>(k);
        }
        proto::ByteWriter bw;
        proto::write_store(bw, out);
        proto::ByteReader br(bw.data.data(), bw.data.size());
        proto::StoreState in;
        CHECK(proto::read_store(br, in));
        CHECK(in == out);
        // Nothing left over, and not one byte short either.
        CHECK(!br.can_read(1));
    }

    // A truncated packet is refused, and a foreign sub packet is not a Store.
    {
        proto::StoreState out;
        out.zedcred = 5;
        proto::ByteWriter bw;
        proto::write_store(bw, out);
        const std::size_t whole = bw.data.size();
        for (std::size_t cut = 0; cut < whole; ++cut) {
            proto::ByteReader shortread(bw.data.data(), cut);
            CHECK(!proto::read_store(shortread, out));
        }
    }
    {
        proto::StoreState out;
        out.zedcred = 5;
        proto::ByteWriter bw;
        proto::write_store(bw, out);
        bw.data[1] = static_cast<std::uint8_t>(proto::SmokeSub::Core);
        proto::ByteReader br(bw.data.data(), bw.data.size());
        CHECK(!proto::read_store(br, out));
    }

    // --- the wire as the CLIENT reads it ------------------------------------
    // The client's Smoke dispatcher has already taken the packet id and the
    // sub packet byte by the time the store's turn comes, so it reads the
    // body alone. Reading the whole packet there instead made the reader
    // look at the purse's first two bytes and call them a packet id: every
    // Store arrived and every Store was dropped, so the store's state was
    // never heard and the store's button never appeared. This is that
    // mistake, written down as a test so it cannot come back.
    for (const std::uint8_t owned_count : {0, 1, 4, 25}) {
        proto::StoreState out;
        out.zedcred = 1234;
        out.wave = 7;
        out.phase = 2;
        out.core_hp = 90;
        out.core_max_hp = 175;
        out.weapon = 2;
        out.grenades = 3;
        out.blocks = 41;
        out.owned_count = owned_count;
        for (std::uint8_t k = 0; k < owned_count; ++k) {
            out.owned[k].index = static_cast<std::uint8_t>((k * 7) % n);
            out.owned[k].level = static_cast<std::uint8_t>(k);
        }
        proto::ByteWriter bw;
        proto::write_store(bw, out);

        // The whole packet, as it goes down the wire: id, sub, then body.
        CHECK(bw.data.size() >= 2);
        CHECK(bw.data[0] == static_cast<std::uint8_t>(proto::PacketId::Smoke));
        CHECK(bw.data[1] == static_cast<std::uint8_t>(proto::SmokeSub::Store));

        // The client, exactly: skip the id and the sub, read the body.
        const std::uint8_t* body = bw.data.data() + 2;
        const std::size_t body_len = bw.data.size() - 2;
        proto::ByteReader br(body, body_len);
        proto::StoreState in;
        CHECK(proto::read_store_body(br, in));
        CHECK(in == out);
        CHECK(!br.can_read(1));

        // And the body alone is NOT a whole packet: the reader that wants
        // the id and the sub must refuse it rather than take the purse's
        // low byte for an id.
        proto::ByteReader whole(body, body_len);
        proto::StoreState refused;
        CHECK(!proto::read_store(whole, refused));
    }

    // The body read alone is refused when it is short, at every length:
    // the client's copy is exactly what is left of the packet.
    {
        proto::StoreState out;
        out.zedcred = 5;
        proto::ByteWriter bw;
        proto::write_store(bw, out);
        const std::uint8_t* body = bw.data.data() + 2;
        const std::size_t body_len = bw.data.size() - 2;
        for (std::size_t cut = 0; cut < body_len; ++cut) {
            proto::ByteReader br(body, cut);
            proto::StoreState in;
            CHECK(!proto::read_store_body(br, in));
        }
    }

    // --- the client's own reading, called the way the client calls it ------
    // The one function the game uses to read a store, so that the button
    // which waits for `received` is proved to get it: the state the client
    // ends up with, and the flag that makes the store's button appear.
    for (const std::uint8_t owned_count : {0, 1, 4, 25}) {
        proto::StoreState out;
        out.zedcred = 777;
        out.wave = 3;
        out.phase = 1;
        out.core_hp = 120;
        out.core_max_hp = 150;
        out.weapon = 1;
        out.grenades = 2;
        out.blocks = 30;
        out.owned_count = owned_count;
        for (std::uint8_t k = 0; k < owned_count; ++k) {
            // A stride the catalogue's size shares nothing with, so no
            // two pairs claim one index: level_of would otherwise read
            // the second claim and fail the first (three shared a
            // factor with the eleven dresses the catalogue grew by).
            out.owned[k].index = static_cast<std::uint8_t>((k * 7) % n);
            out.owned[k].level = static_cast<std::uint8_t>(k + 1);
        }
        proto::ByteWriter bw;
        proto::write_store(bw, out);
        const std::uint8_t* body = bw.data.data() + 2;
        const std::size_t body_len = bw.data.size() - 2;

        spades::client::StoreState cs;
        CHECK(spades::client::ParseStoreBody(body, body_len, cs));
        CHECK(cs.received);              // the flag the Store button waits for
        CHECK(cs.zedcreds == out.zedcred);
        CHECK(cs.wave == out.wave);
        CHECK(cs.phase == out.phase);
        CHECK(cs.coreHp == out.core_hp);
        CHECK(cs.coreMaxHp == out.core_max_hp);
        CHECK(cs.weapon == out.weapon);
        CHECK(cs.grenades == out.grenades);
        CHECK(cs.blocks == out.blocks);
        CHECK(cs.ownedCount == owned_count);
        for (std::uint8_t k = 0; k < owned_count; ++k) {
            CHECK(cs.owned[k].index == out.owned[k].index);
            CHECK(cs.owned[k].level == out.owned[k].level);
        }
        // The levels the cards ask about, the way the store asks them.
        for (std::uint8_t k = 0; k < owned_count; ++k)
            CHECK(cs.level_of(out.owned[k].index) == out.owned[k].level);
    }

    // The whole packet, where only the body is wanted, is refused: this is
    // the mistake that kept the store's button hidden, and it must not
    // parse.
    {
        proto::StoreState out;
        out.zedcred = 42;
        proto::ByteWriter bw;
        proto::write_store(bw, out);
        spades::client::StoreState cs;
        CHECK(!spades::client::ParseStoreBody(bw.data.data(), bw.data.size(),
              cs));
        CHECK(!cs.received);
    }

    if (failures == 0) {
        std::printf("store_catalog: all invariants hold (%zu items)\n", n);
        return 0;
    }
    std::printf("store_catalog: %d checks failed\n", failures);
    return 1;
}