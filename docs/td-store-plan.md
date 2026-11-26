# Tower Defense Store UI — Implementation Plan

> Goal (from the Discord suggestion, 25 Nov 2026): replace the chat commands
> (`/turret`, `/money`) with an in-game store UI used for **player upgrades**,
> **gun upgrades** and **core upgrades**. The first deliverable is the store
> UI shell: browsing cards with rendered thumbnails, prices, credits, and a
> Buy/Upgrade action that reaches the server. Deployment of a bought turret
> (instant spawn vs. carry-to-site animation) is explicitly undecided upstream
> and is **out of scope** for step 1.

---

## 0. Decisions to confirm before coding

| # | Question | Recommended default | Why |
|---|---|---|---|
| 1 | Where does the store open from? | New **"Store" button in the in-game Escape menu** (`ClientMenu`), next to Mods. Also bind a hotkey later if wanted. | Mirrors the reference mod-loader integration exactly (`ClientMenu::OnMods`). |
| 2 | Is the store available outside Tower Defense? | **No** — button hidden/disabled unless connected to the local TD host; otherwise show "Only in Tower Defense". | Avoids fake catalogue state on normal servers. Detection: the server sends a store-state packet (step 2); its arrival enables the store. |
| 3 | Who is authoritative for money and purchases? | **The server.** The client UI is a view over authoritative state and sends purchase *requests*. | The server already owns `p.zc`, `turrets`, `horde` and all wave logic; never trust a client-side credit count. |
| 4 | Turret deployment | Keep `/turret` placement as-is for now (buy **and** place in one command). The UI can show a turret card as "Buy + place: aim at a block, then click Buy" — server replies with the placement prompt if the aim is invalid. Picking up/carrying a turret is a later feature. | Matches "idk yet how the turrets deploy will work … def get an animation but not quite sure" — don't block step 1 on an undecided mechanic. |
| 5 | Scope of actual upgrades in this pass | Make the **framework + 1 real example per category** work end-to-end: Gun tier 2→3 (existing upgrade path), a small player upgrade (e.g. +max HP), a core repair (uses existing `horde->on_core_damaged(-n)`-style mending). The rest are catalogue entries that say "coming soon". | Proves all three categories without inventing balance numbers the designer hasn't given yet. |

If the user wants a different deployment model, only step 5 changes; the UI shell and wire format stay.

---

## 1. Architecture overview

```
   Client                                      Server (Sources/Training/Server.cpp)
   ------                                      ----------------------------------
   ClientMenu "Store" button            ──▶   on_chat("/buy <sku>")
        │                                            │
        ▼                                            ▼
   StoreWindow  (new, in-game ui::UIElement)     purchase handler
        │  category sidebar + card grid          validates: credits, mode,
        │  lazy VoxelModelPreview thumbnails                prerequisites, caps
        │  status label from server reply         deducts zc, applies effect,
        │                                        sends chat reply + fresh state
        ▲                                            │
        └── StoreState packet (Smoke sub 6) ◀────────┘
             credits, wave, core hp, owned upgrades,
             per-sku price/availability
```

**Wire:** one new Smoke subpacket (`StoreState = 6`) in `Sources/Training/Protocol.h`, written by the server on change + 5 s resend, parsed in `Sources/Client/Smoke.cpp`, stored on `Client` as a small `StoreState` struct. Purchases travel as ordinary chat commands (`/buy <sku>`), reusing the existing command path — no client→server packet needed. This keeps the change small and consistent with how `/turret` already works.

---

## 2. Server work — `Sources/Training/Server.cpp`

### 2.1 Catalogue (new, near the tuning structs ~line 720)

Define a small static table; this is the single source of truth for SKUs, shown names, prices, and prerequisites:

```cpp
struct StoreItem {
    const char* sku;        // wire token, e.g. "gun_mg", "gun_minigun"
    const char* label;      // shown in the UI
    int price;              // base Zedcred price (overridable in towerdefense.toml)
    int category;           // 0 player, 1 gun, 2 core
    int requires;           // index of prerequisite item, -1 for none
    const char* model;      // KV6 path for the thumbnail, or nullptr
};
enum StoreCategory { kPlayerUpgrades = 0, kGunUpgrades, kCoreUpgrades };
```
Initial entries (names/prices provisional, all configurable):

| SKU | Category | Effect |
|---|---|---|
| `player_hp` | Player | +25 max HP (repeatable up to a cap) |
| `player_speed` | Player | +10% move speed (repeatable, small cap) |
| `gun_lvl2`, `gun_lvl3` | Gun | Existing turret raise path (acts on the turret under the crosshair, or buys+places level 2/3) |
| `core_repair` | Core | Restore 25 core HP (mirrors `core_mend`) |
| `core_hp` | Core | +25 max core HP (needs `horde::Config` support — see 2.4) |

### 2.2 State

Add to `Player` (~line 507): `int upgrade_levels[kPlayerUpgradeCount] = {};` — one count per repeatable player upgrade, applied in `spawn()` (~line 1643) and `refill()`.

Add to `Server::Impl` (~line 1361): `StoreItemState store_sent[kItemCount]` (last wire state, for change detection), `double store_resend_next`, and a `bool store_enabled` implied by `tower_defense_mode()`.

### 2.3 Purchase handler

```
void store_command(Player& p, const std::string& text);
```
Parsed from `/buy <sku>` in `on_chat()` (~line 5997), alongside the existing TD commands. It must:

1. Reject if not `tower_defense_mode()` or player not alive/playing.
2. Look up the SKU; reject unknown.
3. Check category-specific rules (see below) and the price (from TOML if present, else table).
4. Check `p.zc >= price`; deduct, then apply the effect.
5. Reply with one system_chat line ("Bought X for N credits; M in hand." / reason for refusal).
6. Mark the store state dirty so the next tick sends fresh state to everyone.

Per-category notes:
- **Gun upgrades:** call into the existing `turret_place(p, level, reply)` (~line 2567), but skip its own price deduction when the store already charged, OR simply expose the existing prices as the store prices and let `turret_place` remain the single payer (cleaner — the UI just sends `/turret2`, and the store item is a *description* of that action). **Recommended:** the gun cards send the existing `/turret` command directly; the store's Buy button for guns is just a shortcut to the old flow. This avoids duplicating the payment logic.
- **Player upgrades:** increment the player's level, apply the HP/speed modifier at next spawn/refill.
- **Core upgrades:** for repair, call the same code path as `core_mend` on intermission; for max HP, add a `horde::Config::core_hp_max_bonus` setter or a `Horde::add_core_max_hp(int)` method.

### 2.4 Horde mechanics seam (only for a core max-HP upgrade)

`horde::Horde` already has `core_hp_` and `cfg_.core_hp` (`Horde.cpp:43,1591`). Add one method in `Horde.h`/`Horde.cpp`:

```cpp
void add_core_max_hp(int amount);   // cfg_.core_hp += amount; core_hp_ += amount;
```
Unit-test it in `Server/test/horde_test.cpp` (existing core tests around line 464).

### 2.5 State publication

`store_state_send()` alongside `turret_state_send()` (~line 2382). Payload (Smoke sub 6):

| Field | Type | Meaning |
|---|---|---|
| `credits` | u16 (clamped at 65535) | the local player's Zedcred (sent per-player, not broadcast) |
| `wave` | u8 | current wave number |
| `phase` | u8 | `horde::Phase` as an integer |
| `core_hp` / `core_max` | u16 / u16 | core health |
| `zombies_left` | u8 | `horde->zombies_up() + pending` (clamp 255) |
| `owned_count` | u8 | number of `owned` pairs following |
| `owned` pairs | (u8 sku index, u8 level) × N | purchased levels, so the UI can tick bought items |

Send this to each player individually (credits differ), on change + the existing `kTrailResend` (5 s) refresh so a client that loads mid-game recovers. Note: `turret_state_send()` broadcasts; this one cannot.

### 2.6 TOML config

Add a `[store]` section read by the same parser (~line 2061) with one key per SKU (`price_<sku>`), clamped to a sane range. Document it in `towerdefense.toml`. This makes all provisional prices tunable without a rebuild.

---

## 3. Wire + client plumbing

1. `Sources/Training/Protocol.h`: add `SmokeSub::Store = 6` near line 302, a `StoreState`-like writer declaration near the turret writers (~line 338), and a `write_store_state()` helper in `Protocol.cpp`.
2. `Sources/Client/Smoke.cpp`: handle `kSubStore` (~line 309), parse with bounds checks, call `client.ApplyStoreState(state)`.
3. `Sources/Client/Client.h/.cpp`: add the `StoreState` struct (or include a small `Training/StoreState.h` that is header-only like `TrainingModes.h`) and `ApplyStoreState()`. Keep it plain data; no server types.
4. `Sources/Client/NetClient.cpp` and `DemoNetClient.cpp`: Smoke dispatch already goes through `ApplySmokePacket` — no changes needed beyond `Smoke.cpp`.

---

## 4. Client UI — the store window

### 4.1 Port the reference grid pieces

From `REFFERENCE/xrspades/Sources/Gui/UI/InGame/ModsWindow.cpp:65-307` (the **new**, 461-line version — the main repo's copy is the old 295-line flat list):

- `SkinCard` → `StoreCard` (a `ButtonBase`): draws the pause-menu palette, a lazily created thumbnail, the name, price, and a "bought/affordable/locked" tint. `Load()` runs on first `Render()` of a visible card; `DrawModel()` holds one cached `IImage`, updated at 24 Hz only while visible.
- `SkinGridRow` → `StoreGridRow` (a `UIElement` that lays its cards out in `OnResized()`).
- `SkinGridModel` → `StoreGridModel` (a `ListViewModel` that owns `rows` and creates row elements lazily).

Required helpers already in the main repo's widget layer but **not yet included by the old ModsWindow** (`Sources/Gui/UI/Widgets/`): `ButtonBase.h`, `DrawUtils.h`, `TextUtils.h` (`ElideText`), `Core/Math.h` (`Clamp`). Verify signatures during the port; they are used identically in the reference.

### 4.2 Thumbnail rendering

Port these reference files **unchanged** into the main repo (they have no other dependencies):

- `Sources/Gui/UI/Widgets/VoxelModelPreview.h/.cpp` (410 lines) — `Rasterize(w,h,yaw)` returns straight-alpha RGBA, max 512×512, with its own depth buffer and 2× supersampling; `Project()` is the CPU projection used by `Rasterize`. In `StoreCard::DrawModel`, cap resolution like the reference:
  ```
  density = min(max(1, renderer.ScreenPixelRatio()), 2, 512/width, 512/height)
  ```
  Reuse one `IImage`, update at 24 Hz (`frame = int(elapsed * 24)`), never reallocate every frame.

- `Sources/Gui/SkinPreviewScene.h` — `SkinPreviewPart { Handle<VoxelModel> model; Matrix4 matrix; customColor; opacity }` and `SkinPreviewScene { parts, beams, diagnostic }`.

**Do not port `SkinCatalog`/`SkinPreviewScript`/`SkinPreviewScriptApi`** unless scripted skin poses are actually needed. For TD, construct the preview scene directly from the shipped KV6 assets:

- turret models: `Resources/TowerDefense/Models/Turrets/TurretHeadMG{1,2,3}.kv6` and `TurretBaseMG.kv6` (available now — no script capture needed),
- player upgrades: any existing weapon/powerup model from the base pak, or a simple procedural `SkinPreviewPart` if none fits,
- core upgrades: reuse the turret base model or the flag model from the base pak.

Load the KV6 through the same path the reference uses for its fallback (`SkinCatalog::LoadPreview` → `VoxelModel::Load`/`FileManager::Open`). Keep a missing-model fallback: draw a coloured rectangle + the item's name so a broken pak never blocks purchasing.

### 4.3 Window layout (`StoreWindow`)

Clone `ClientMenu::OnMods` integration (`Sources/Gui/UI/InGame/ClientMenu.cpp:184-193` in both repos):

- `ClientMenu` gets a "Store" button next to "Mods"; hidden unless a StoreState has been received (set `button->visible` in `Render()`).
- `Handle<StoreWindow>::New(this, ui); window->Run()` → `ui->SetActiveUI(this)`.
- `Close()` restores the previous UI and fires `closed` so `ClientMenu` can restore its Escape hint.
- `HotKey("Escape")` closes; `Render()` draws the same two overlay rects as the reference (`SetColorNP(r, {0,0,0,0.7})` + full screen, `{0,0,0,0.5}` + contents), then `UIElement::Render()`.

Inside the window (`store window` analog of `ModsWindow.cpp:340-408`):

- Heading label "Store" (heading font, shadow) + a credits/wave/core-status line under it (updated from StoreState).
- Left sidebar: three category buttons — Player / Guns / Core — with `toggled` state, styled by the pause-menu Button palette.
- Right: `ListViewBase` with `rowHeight ≈ 150`, 1–3 columns based on width, `StoreGridModel` as its model; `ScrollToRow(0)` on category change.
- Footer: a "Back [Esc]" button. Individual cards are the Buy buttons; clicking one sends `/buy <sku>` via the existing chat entry point (see 4.4), then the server's chat reply updates the status label.
- Card visual states from authoritative state: **owned** (green tick / dimmed), **too expensive** (red price), **locked** (requires another upgrade), **available** (normal).

### 4.4 Sending a purchase from the UI

The UI must not call server code directly. Use the existing chat send path from the client:

- `ClientUI`/`Client` already expose a way to send chat (see `NetClient::SendChat` or `Client::SendChat`; pick the one used by chat window). Call it with `"/buy <sku>"`.
- The server's reply arrives as a system chat message and appears in the chat log; additionally, copy the reply text into `StoreWindow::statusLabel` if the client can observe system chat (check `Client::AddSystemMessage`/`ChatWindow`). If that hook is awkward, the authoritative `StoreState` delta is enough for the UI to show "Bought" by itself — **still require the server to reject invalid purchases**, so a desynced UI can never grant anything free.

### 4.5 Localization

Use `_Tr("Client", "...")` for every new user-visible string, matching both ModsWindow versions. Add the new keys to `Resources/Translations` if the project keeps a source list (`pot-update.sh` regenerates it).

---

## 5. Tests (per repo rule: verify before calling it fixed)

1. **Unit — horde:** add `test_core_max_hp` to `Server/test/horde_test.cpp` for `Horde::add_core_max_hp()`.
2. **Unit — catalogue:** if the store item table is extracted into a header-only `Training/StoreCatalog.h` (recommended, like `TrainingModes.h`), add `Server/test/store_catalog_test.cpp`: SKU uniqueness, prerequisite graph has no cycles, all prices > 0, category indices in range. Register it in `Server/test/CMakeLists.txt` and add it to `zs_tests`.
3. **Wire:** add a round-trip test for `write_store_state()` + the parser if practical; the parser in `Smoke.cpp` is coupled to `Client`, so factor the byte decode into a small pure function in `Training/Protocol.cpp` that both the client and the test call.
4. **Integration:** extend `Server/test/training_server_test.cpp` with a TD-mode test: start in `Mode::TowerDefense`, connect, receive the StoreState packet, send `/buy <cheap sku>`, assert the next state shows the purchase and the credits decreased. (This harness currently only exercises tutorial mode; add a mode parameter.)
5. **Build + full suite:** `cmake --build build --parallel && cd build && ctest --output-on-failure`. The pre-change baseline is green (`horde`, `training_server` pass).
6. **Manual smoke test:** run `build/bin/zerospades`, launch Tower Defense from the main menu, press Escape → Store, browse all three categories, verify thumbnails rotate, buy the cheapest player upgrade, confirm credits change and `/money` agrees.

---

## 6. File-by-file change list

| File | Change |
|---|---|
| `Sources/Training/Protocol.h` / `.cpp` | Smoke sub 6 + `StoreState` serialization helpers |
| `Sources/Client/Smoke.cpp` | Parse sub 6 → `client.ApplyStoreState(...)` |
| `Sources/Client/Client.h` / `.cpp` | Hold `StoreState`, expose `ApplyStoreState()`, expose chat send for Buy |
| `Sources/Gui/UI/InGame/ClientMenu.h/.cpp` | "Store" button + `OnStore` |
| `Sources/Gui/UI/InGame/StoreWindow.h/.cpp` | New — store overlay, sidebar, grid, cards, status |
| `Sources/Gui/UI/Widgets/VoxelModelPreview.h/.cpp` | New — ported from reference |
| `Sources/Gui/SkinPreviewScene.h` | New — ported from reference (model/beam scene) |
| `Sources/Training/StoreCatalog.h` | New — header-only SKU table & categories |
| `Sources/Training/Server.cpp` | Purchase handler, player upgrade state, `store_state_send()`, TOML `[store]` keys, `/buy` command |
| `Sources/Training/Horde.h/.cpp` | `add_core_max_hp()` (only if that card ships in step 1) |
| `towerdefense.toml` | `[store]` section with prices |
| `Server/test/CMakeLists.txt`, new test files, `horde_test.cpp`, `training_server_test.cpp` | Tests from section 5 |
| `Resources/Translations/*` | New `_Tr` keys (via `pot-update.sh`) |

Not touched: `SentryEntity.cpp` (server turret rendering), `Trail.cpp`, the map, existing turret placement rules.

---

## 7. Phased delivery / checkpoints

- **Phase A — plumbing:** wire protocol + client state + server's read-only StoreState. Verify: client receives and shows credits/wave/core in a debug label. **No purchasing yet.**
- **Phase B — UI shell:** port VoxelModelPreview + grid; build StoreWindow with three tabs and working thumbnails for the shipped turret models; buttons show "coming soon". Verify visually; tests for the ported rasterizer (deterministic pixels on a fixed model/yaw).
- **Phase C — real purchases:** `/buy` handler + player HP upgrade + core repair + gun shortcut. Verify with unit + integration tests from section 5.
- **Phase D — polish:** affordability/locked tints, status feedback, reload/resend robustness, TOML prices, translations, and (only if requested) carrying/placing bought turrets.

Each phase ends with the full build + test suite green and a brief manual in-game check.
