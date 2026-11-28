/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "StoreWindow.h"

#include <Core/Exception.h>
#include <Core/Bitmap.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Core/VoxelModel.h>
#include <Gui/SkinPreviewScene.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/ButtonBase.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>
#include <Gui/UI/Widgets/VoxelModelPreview.h>
#include <Training/Grenade.h>
#include <Training/StoreCatalog.h>
#include <Training/WeaponTiers.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <Client/ClientUI.h>
#include <Client/ClientUIHelper.h>
#include <Client/Client.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Client/NetClient.h>

// The veil over a shelf's own art: how much of a tab's background image
// shows over the plain panel behind it.
DEFINE_SPADES_SETTING(cg_storeBgOpacity, "0.85");

namespace spades {
	namespace client {
		using gui::ui::Button;
		using gui::ui::ButtonBase;
		using gui::ui::UIElement;
		using gui::ui::UIManager;
		using gui::ui::VoxelModelPreview;
		using gui::ui::SetColorNP;

		namespace {
			constexpr float kCardGap = 8.0F;

			// A card is a row: the air inside its edges, and the breath between
			// the lines of its words.
			constexpr float kCardPad = 9.0F;
			constexpr float kLineGap = 3.0F;

			// The placeholder every card without a model of its own wears: the
			// breadcrumb, the trail marker every player already knows.
			constexpr const char* kPlaceholderModel =
			  "Models/Effects/Trail/Breadcrumb.kv6";

			// One shelf art a tab, from the Tower Defense pak's own Gfx/BG. A
			// missing image leaves the plain dark panel standing, so the page
			// works before its art is drawn.
			constexpr const char*
			  kTabBackgrounds[training::store::kCategoryCount] = {
			    "Gfx/BG/Weapons.png",
			    "Gfx/BG/Perks.png",
			    "Gfx/BG/Core.png",
			  };

			// The shelves the page stands, left to right, and their
			// captions. The release stands the Weapons shelf alone;
			// Perks and Core sit out until their cards come back.
			constexpr int kTabOrder[] = {0};
			constexpr const char* kTabNames[] = {"Weapons"};
			constexpr int kTabCount = int(sizeof(kTabOrder) / sizeof(kTabOrder[0]));

			// The empty band above a class group on the Weapons shelf,
			// one group standing apart from the one above it.
			constexpr float kGroupGap = 16.0F;

			// One row of the Weapons shelf: which card it stands, whether
			// the card is another class's (greyed, seen and not bought)
			// and the empty band above it when a group starts there.
			struct ShelfEntry {
				int index;
				bool greyed = false;
				float gap = 0.0F;
			};

			/** The description wrapped to the width its lines sit in, at
			 * most three lines of it, the last elided when the words run
			 * past them. */
			std::vector<std::string> WrappedText(IFont& font,
			                                      const std::string& text,
			                                      float width, int maxLines) {
				std::vector<std::string> lines;
				std::string word, line;
				const auto take = [&]() {
					if (line.empty()) {
						line = word;
					} else if (font.Measure(line + " " + word).x <= width) {
						line += " " + word;
					} else {
						lines.push_back(line);
						line = word;
					}
					word.clear();
				};
				for (const char c : text) {
					if (c == ' ')
						take();
					else
						word += c;
				}
				if (!word.empty())
					take();
				if (!line.empty())
					lines.push_back(line);
				if (int(lines.size()) > maxLines) {
					lines.resize(maxLines);
					lines[maxLines - 1] =
					  gui::ui::ElideText(font, lines[maxLines - 1], width);
				}
				return lines;
			}

			/** The numbers a dress fights with, a line each, read from
			 * the tiers table the server pays its damage out of too. A
			 * tool says its own two or three. */
			std::vector<std::string> ShelfStats(
			  const training::store::Item& item) {
				std::vector<std::string> out;
				char buf[32];
				const auto add = [&](const char* label, const std::string& v) {
					out.push_back(std::string(label) + ": " + v);
				};
				if (item.effect != training::store::kEffectWeaponSkin)
					return out;
				if (item.arg <= 2 && item.tier <= 4) {
					const auto& s =
					  training::tiers::kGuns[item.arg][item.tier];
					add("Damage", std::to_string(s.damage[0]));
					std::snprintf(buf, sizeof buf, "%.1f", 1.0F / s.delay);
					add("Fire rate", std::string(buf) + "/s");
					std::snprintf(buf, sizeof buf, "x%.1f", s.recoil);
					add("Recoil", buf);
					add("Magazine", std::to_string(s.clip));
				} else if (item.arg == 3 && item.tier <= 3) {
					const auto& s = training::tiers::kSpade[item.tier];
					add("Damage", std::to_string(s.melee_damage));
					std::snprintf(buf, sizeof buf, "x%.2f", s.range);
					add("Reach", buf);
					add("Block bite",
					    std::to_string(int(50.0F * s.chip + 0.5F)));
				} else if (item.arg == 4) {
					// The grenade's own numbers: its blast peaks at a
					// hundred at the centre of it, and three to carry.
					add("Damage", "100");
					add("Ammo", std::to_string(nade::kMaxGrenades));
				}
				return out;
			}

			std::string Trimmed(std::string word) {
				const size_t a = word.find_first_not_of(" \t\r\n");
				if (a == std::string::npos)
					return "";
				return word.substr(a, word.find_last_not_of(" \t\r\n") - a + 1);
			}

			/** The pause menu's palette, so a card reads as one of its
			 * buttons. A greyed card is another class's rung: neither
			 * hovered nor pressed, for it is not this player's to buy. */
			Vector4 CardColour(bool hover, bool pressed, bool greyed,
			                   bool owned, bool poor) {
				if (greyed) return MakeVector4(0.14F, 0.14F, 0.16F, 0.55F);
				if (owned)  return MakeVector4(0.22F, 0.34F, 0.22F, 0.85F);
				if (poor)   return MakeVector4(0.30F, 0.18F, 0.18F, 0.65F);
				if (pressed && hover) return MakeVector4(0.7F, 0.7F, 0.7F, 0.9F);
				if (hover)  return MakeVector4(0.4F, 0.4F, 0.4F, 0.7F);
				return MakeVector4(0.2F, 0.2F, 0.2F, 0.5F);
			}

			/** What a card with no model of its own shows instead: a mark of its
			 * own, drawn. Every ability in the store gets an icon that says what
			 * it is, rather than the same yellow square on every card. */
			enum class IconKind {
				None, Shield, Boot, Plus, Block, Core, Plates, Ammo
			};

			/** One card: a rotating thumbnail over a name, a detail and a price. */
			class StoreCard : public ButtonBase {
				const training::store::Item* item;
				std::uint8_t level = 0;
				int price = 0;
				int price_override = -1;  // the pak's own word, when it says one
				int shown_price = 0;      // the number the card stands at
				std::uint16_t purse = 0;
				// Another class's rung, seen and not bought: the card
				// stands veiled at the foot of the shelf.
				bool greyed = false;
				// The numbers the rung fights with, a line each, drawn
				// in their own block at the right edge of the card.
				std::vector<std::string> stats;

				std::unique_ptr<VoxelModelPreview> preview;
				Handle<IImage> image;
				bool loaded = false;
				// A picture that could not be drawn is not given up on: it is
				// asked for again, a few times, and only then left to the drawn
				// stand-in. One throw used to blank a card for the session.
				int loadRetries = 0;
				// The mark a card with no model of its own wears, and its tier.
				IconKind iconKind = IconKind::None;
				int iconTier = 0;
				float animationStart = 0.0F;
				float initialYaw = 1.15F;
				int renderedFrame = -1;

				std::string caption;
				std::string detail;
				std::string priceText;

				void Load() {
					loaded = true;
					animationStart = GetManager().time;
					// Every card wears a voxel model: its own when it has one, the
					// breadcrumb placeholder when it has not, so a shelf reads as one
					// set of things rather than a mix of drawn marks and pictures.
					const char* model_path =
						item->model != nullptr ? item->model : kPlaceholderModel;
					try {
						auto stream = FileManager::OpenForReading(model_path);
						if (!stream) {
							// Said loudly: a card whose picture cannot be read is a blank
							// card, and the log is the only place the reason can be read
							// afterwards.
							SPLog("Store: no file for %s's thumbnail (%s)", item->sku,
							      model_path);
							return;
						}
						auto model = VoxelModel::LoadKV6(*stream);
						stream.reset();
						if (!model) {
							SPLog("Store: %s\'s thumbnail is not a voxel model (%s)",
								      item->sku, model_path);
							preview.reset();
							return;
						}
						preview = std::make_unique<VoxelModelPreview>(*model);
						if (preview->IsEmpty()) {
							SPLog("Store: %s's thumbnail has no surfaces (%s)", item->sku,
							      model_path);
							preview.reset();
						}
					} catch (const std::exception& e) {
						// A thumbnail that cannot be drawn must not block a card, but it
						// must be said which one, or a blank card is a mystery: a bad
						// model, a bad transform, a surface too big to mesh.
						SPLog("Store: %s's thumbnail could not be built (%s): %s", item->sku,
						      model_path, e.what());
						preview.reset();
					}
				}

				void DrawModel(IRenderer& r, const AABB2& bounds) {
					if (!preview || preview->IsEmpty() || bounds.GetWidth() <= 0 ||
					    bounds.GetHeight() <= 0)
						return;
					const float elapsed = std::max(0.0F, GetManager().time - animationStart);
					// The shelf stands still: a model turns only while the mouse rests
					// on its card, and goes back to its standing pose when it leaves.
					const int frame = hover ? int(elapsed * 24.0F) : -1;
					const float density =
					  std::min({std::max(1.0F, r.ScreenPixelRatio()), 2.0F,
					            512.0F / bounds.GetWidth(), 512.0F / bounds.GetHeight()});
					const int w = std::max(1, int(std::floor(bounds.GetWidth() * density)));
					const int h = std::max(1, int(std::floor(bounds.GetHeight() * density)));
					try {
						const bool resized =
						  !image || int(image->GetWidth()) != w || int(image->GetHeight()) != h;
						if (resized || renderedFrame != frame) {
							const float turn = float(frame) * (0.4F / 24.0F);
							const float yaw =
							  frame >= 0 ? std::fmod(initialYaw + turn, 6.283185F)
							              : initialYaw;
							auto pixels = preview->Rasterize(w, h, yaw);
							auto bitmap = Handle<Bitmap>::New(pixels.data(), w, h);
							if (resized)
								image = r.CreateImage(*bitmap);
							else
								image->Update(*bitmap, 0, 0);
							renderedFrame = frame;
						}
						if (image) {
							SetColorNP(r, MakeVector4(1, 1, 1, 1));
							r.DrawImage(image, bounds);
						}
				} catch (const std::exception& e) {
						// A raster that cannot be produced: said once, since this runs every
						// frame, and then asked for again a few times before the card settles
						// on its drawn stand-in. One throw used to blank a card for the rest
						// of the session.
						static bool complained = false;
						if (!complained) {
							complained = true;
							SPLog("Store: a thumbnail could not be drawn: %s", e.what());
						}
						image = {};
						renderedFrame = -1;
						if (++loadRetries < 3) {
							// Worth another try: forget the raster, keep the surface, and let
							// Load() run again on the next frame.
							loaded = false;
						} else {
							// The picture is not going to be drawn: the drawn stand-in takes the
							// card instead.
							preview.reset();
						}
					}
				}

				/** A sentry drawn of a few boxes: a base, a pillar, a head on a swivel
			 * and a barrel pointing out of it. Only ever a card's stand-in, when its
			 * voxel model cannot be read, so it is a silhouette rather than a rival
			 * for the real thing. */
			void DrawTurretGlyph(IRenderer& r, Vector2 at, float h) {
				const float w = h * 0.60F;
				const auto box = [&](float x, float y, float bw, float bh, Vector4 c) {
					SetColorNP(r, c);
					r.DrawFilledRect(at.x + x, at.y + y, at.x + x + bw, at.y + y + bh);
				};
				const Vector4 dark(0.15F, 0.16F, 0.18F, 1.0F);
				const Vector4 mid(0.29F, 0.31F, 0.34F, 1.0F);
				const Vector4 lit(0.45F, 0.47F, 0.51F, 1.0F);
				// The base, the pillar, the head - widest at the bottom, each sitting
				// on the one under it.
				box(-w * 0.50F, h * 0.24F, w, h * 0.20F, mid);
				box(-w * 0.30F, h * 0.06F, w * 0.60F, h * 0.19F, dark);
				box(-w * 0.34F, -h * 0.08F, w * 0.68F, h * 0.16F, lit);
				// The barrel out to one side, and its shadow under the head.
				box(w * 0.02F, -h * 0.02F, w * 0.60F, h * 0.055F, mid);
				box(-w * 0.08F, h * 0.01F, w * 0.36F, h * 0.03F, dark);
			}

			/** A canvas for an icon: a unit square, mapped onto the screen. Every
			 * mark in the store is drawn on one of these, so they are all made
			 * of the same parts in the same proportions. */
			class IconCanvas {
				IRenderer& r;
				Vector2 at;
				float size;

			public:
				IconCanvas(IRenderer& r, Vector2 at, float size)
				    : r(r), at(at), size(size) {}

				Vector2 P(float x, float y) const {
					return at + MakeVector2(x * size, y * size);
				}
				void Rect(float x, float y, float w, float h, Vector4 c) {
					SetColorNP(r, c);
					r.DrawFilledRect(at.x + x * size, at.y + y * size,
					                 at.x + (x + w) * size, at.y + (y + h) * size);
				}
				void Tri(float x0, float y0, float x1, float y1, float x2, float y2,
				         Vector4 c) {
					SetColorNP(r, c);
					r.DrawFilledTriangle(P(x0, y0), P(x1, y1), P(x2, y2));
				}
				void Quad(float x0, float y0, float x1, float y1, float x2, float y2,
				          float x3, float y3, Vector4 c) {
					Tri(x0, y0, x1, y1, x2, y2, c);
					Tri(x0, y0, x2, y2, x3, y3, c);
				}
			};


			void DrawStoreIcon(IRenderer& r, Vector2 at, float size,
			                    IconKind kind, int tier) {
				IconCanvas c(r, at, size);
				// The store's own palette: pale marks on the grey card, a mid
				// grey for the faces turned away, and the same gold the prices use,
				// and nothing else.  A shelf of these reads as one set.
				const Vector4 light(0.92F, 0.94F, 0.97F, 1.0F);
				const Vector4 mid(0.58F, 0.62F, 0.68F, 1.0F);
				const Vector4 dark(0.24F, 0.26F, 0.30F, 1.0F);
				const Vector4 gold(0.96F, 0.79F, 0.28F, 1.0F);

				switch (kind) {
				case IconKind::Shield: {
					// A shield, cut hollow, with a bar for each tier.
					c.Quad(0.16F, 0.08F, 0.84F, 0.08F, 0.84F, 0.48F, 0.16F, 0.48F, light);
					c.Tri(0.16F, 0.46F, 0.84F, 0.46F, 0.50F, 0.94F, light);
					c.Quad(0.23F, 0.15F, 0.77F, 0.15F, 0.77F, 0.47F, 0.23F, 0.47F, dark);
					c.Tri(0.23F, 0.45F, 0.77F, 0.45F, 0.50F, 0.84F, dark);
					for (int k = 0; k < tier && k < 3; ++k) {
						const float y = 0.24F + float(k) * 0.08F;
						c.Quad(0.31F, y, 0.69F, y, 0.69F, y + 0.05F, 0.31F, y + 0.05F,
						       light);
					}
				} break;
				case IconKind::Boot: {
					// A boot, and a speed line for each tier.
					c.Rect(0.38F, 0.10F, 0.22F, 0.44F, light);
					c.Quad(0.38F, 0.52F, 0.60F, 0.52F, 0.88F, 0.62F, 0.38F, 0.74F,
					       light);
					c.Rect(0.34F, 0.72F, 0.60F, 0.10F, mid);
					for (int k = 0; k < tier && k < 3; ++k)
						c.Rect(0.06F, 0.28F + float(k) * 0.13F,
						       0.24F - float(k) * 0.05F, 0.05F, gold);
				} break;
				case IconKind::Plus: {
					// A dressing: a plus, and nothing else.
					c.Rect(0.42F, 0.12F, 0.16F, 0.76F, light);
					c.Rect(0.12F, 0.42F, 0.76F, 0.16F, light);
				} break;
				case IconKind::Block: {
					// One block, drawn as the game draws one: three faces of a cube.
					c.Quad(0.50F, 0.10F, 0.92F, 0.32F, 0.50F, 0.54F, 0.08F, 0.32F,
					       light);
					c.Quad(0.08F, 0.32F, 0.50F, 0.54F, 0.50F, 0.94F, 0.08F, 0.72F,
					       mid);
					c.Quad(0.50F, 0.54F, 0.92F, 0.32F, 0.92F, 0.72F, 0.50F, 0.94F,
					       dark);
				} break;
				case IconKind::Core: {
					// The core, cut hollow, with the gold cross of a mended thing.
					c.Quad(0.50F, 0.06F, 0.94F, 0.50F, 0.50F, 0.94F, 0.06F, 0.50F,
					       light);
					c.Quad(0.50F, 0.16F, 0.84F, 0.50F, 0.50F, 0.84F, 0.16F, 0.50F,
					       dark);
					c.Rect(0.46F, 0.34F, 0.08F, 0.32F, gold);
					c.Rect(0.34F, 0.46F, 0.32F, 0.08F, gold);
				} break;
				case IconKind::Plates: {
					// A plate for each tier, stacked.
					const int n = std::max(1, std::min(3, tier));
					const float plate = n == 1 ? 0.44F : 0.20F;
					const float gap = 0.09F;
					float y = (1.0F - (float(n) * plate + float(n - 1) * gap)) * 0.5F;
					for (int i = 0; i < n; ++i) {
						c.Rect(0.14F, y, 0.72F, plate, light);
						c.Rect(0.14F, y + plate * 0.82F, 0.72F, plate * 0.18F, mid);
						y += plate + gap;
					}
				} break;
				case IconKind::Ammo: {
					// Three rounds, stood up.
					for (int i = 0; i < 3; ++i) {
						const float x = 0.30F + float(i) * 0.15F;
						c.Rect(x, 0.20F, 0.10F, 0.44F, light);
						c.Rect(x, 0.12F, 0.10F, 0.08F, mid);
						c.Rect(x, 0.64F, 0.10F, 0.05F, mid);
					}
				} break;
				case IconKind::None:
				default:
					break;
				}
			}

			public:
				StoreCard(UIManager* manager,
				          const training::store::Item* item, int index,
				          const std::map<std::string, StoreText>* texts,
				          bool greyed_ = false)
				    : ButtonBase(manager), item(item) {
					greyed = greyed_;
					stats = ShelfStats(*item);
					caption = item->label;
					detail = item->detail;
					// The store's own words, when the pak says them: the name, the
					// description and the price the card is shown at, one line of
					// Store/items.txt a card. What the file does not say falls
					// back to the catalogue's own words.
					if (texts != nullptr) {
						const auto it = texts->find(item->sku);
						if (it != texts->end()) {
							if (!it->second.name.empty())
								caption = it->second.name;
							if (!it->second.detail.empty())
								detail = it->second.detail;
							price_override = it->second.price;
						}
					}
					// Every card that has no model of its own wears a mark of its
					// own: a shield for tough skin, a boot for light feet, and so on,
					// each gaining a ridge, a line or a plate for the tier it is. A
					// card that has a model wears the model instead.
					const std::string sku = item->sku;
					if (sku == "hp1")         { iconKind = IconKind::Shield; iconTier = 1; }
					else if (sku == "hp2")    { iconKind = IconKind::Shield; iconTier = 2; }
					else if (sku == "hp3")    { iconKind = IconKind::Shield; iconTier = 3; }
					else if (sku == "speed1") { iconKind = IconKind::Boot;   iconTier = 1; }
					else if (sku == "speed2") { iconKind = IconKind::Boot;   iconTier = 2; }
					else if (sku == "health") { iconKind = IconKind::Plus; }
					else if (sku == "blocks") { iconKind = IconKind::Block; }
					else if (sku == "repair") { iconKind = IconKind::Core; }
					else if (sku == "core_hp1") { iconKind = IconKind::Plates; iconTier = 1; }
					else if (sku == "core_hp2") { iconKind = IconKind::Plates; iconTier = 2; }
					else if (sku == "core_hp3") { iconKind = IconKind::Plates; iconTier = 3; }
					else if (sku == "ammo")      { iconKind = IconKind::Ammo; }
					initialYaw = 1.15F + float(index % 7) * 0.11F;
					// A card with a level past the first tier starts its turn
					// somewhere other than the rifle's, so a row does not spin
					// as one body.
					if (item->effect == training::store::kEffectTurret)
						initialYaw = 0.75F + float(index % 5) * 0.12F;
				}

				/** The state the server says, read fresh every draw. */
				void SetState(std::uint8_t lv, int pr, std::uint16_t purse_) {
					level = lv;
					price = pr;
					purse = purse_;
					// The wire's price comes first: the host repriced the
					// cards in its own towerdefense.toml and the wire says
					// what the purse pays, so the shelf never shows a
					// number the buy does not charge. The pak's own price
					// stands for a wire that said none of it, and the
					// catalogue's is the last word.
					shown_price = price >= 0
					                ? price
					                : (price_override >= 0 ? price_override
					                                       : item->price);
					const std::string total = std::to_string(shown_price);
					const std::string coin = training::store::kCurrencyPlural;
					priceText = shown_price > 0 ? total + " " + coin : "Free";
				}

				int GetItemIndex() const {
					for (size_t i = 0; i < training::store::kItemCount; ++i)
						if (training::store::kItems[i].sku == item->sku) return int(i);
					return -1;
				}

				void Render() override {
					auto& r = GetManager().GetRenderer();
					const auto p = GetScreenPosition();
					const bool maxed =
						  level > 0 && item->max_level != 255 &&
						  level >= item->max_level;
					const bool owned = maxed;
					const bool poor =
					  !owned && !greyed && shown_price > 0 && purse < shown_price;
					const auto colour =
					  CardColour(hover, pressed && hover, greyed, owned, poor);
					SetColorNP(r, colour);
					r.DrawFilledRect(p.x + 1, p.y + 1, p.x + size.x - 1, p.y + size.y - 1);
					SetColorNP(r, MakeVector4(0, 0, 0, colour.w));
					r.DrawOutlinedRect(p.x + 1, p.y + 1, p.x + size.x - 1, p.y + size.y - 1);

					if (!loaded)
						Load();

					// The picture stands on the left in its own square frame, and the
					// words sit beside it: the name, the description under it, and the
					// price at the bottom of the words, pushed to their right edge, so
					// it reads as a thing of its own.
					const float side = std::max(1.0F, size.y - kCardPad * 2.0F);
					const AABB2 frame(p.x + kCardPad, p.y + kCardPad, side, side);
					SetColorNP(r, MakeVector4(0, 0, 0, colour.w * 0.8F));
					r.DrawOutlinedRect(frame.min.x, frame.min.y, frame.max.x, frame.max.y);
					const AABB2 area(frame.min.x + 2.0F, frame.min.y + 2.0F, side - 4.0F,
						                 side - 4.0F);
					const Vector2 centre = (area.min + area.max) * 0.5F;
					const float mark = std::min(area.GetWidth(), area.GetHeight());
					if (preview) {
						DrawModel(r, area);
					} else if (iconKind != IconKind::None) {
						// The card's own mark, drawn at the size of the picture it
						// stands in for.
						const float s = mark * 0.78F;
						DrawStoreIcon(r, centre - MakeVector2(s, s) * 0.5F, s, iconKind,
							              iconTier);
					} else if (item->effect == training::store::kEffectTurret) {
						// A sentry with no picture: its gun on a pillar.
						DrawTurretGlyph(r, centre, mark * 0.85F);
					} else {
						// Nothing at all to draw: a block, so the card still shows
						// something rather than a blank grey box.
						const float s = mark * 0.78F;
						DrawStoreIcon(r, centre - MakeVector2(s, s) * 0.5F, s,
							              IconKind::Block, 0);
					}
					// Another class's rung: the picture stays, veiled, for
					// the card says what it is rather than what it looks
					// like to buy here.
					if (greyed) {
						SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.45F));
						r.DrawFilledRect(frame.min.x, frame.min.y,
						                frame.max.x, frame.max.y);
					}

					auto* font = GetFont();
					if (font == nullptr)
						return;
					// The words sit between the picture and the numbers: the
					// name, the description wrapped under it, and the price on
					// a line of its own below. The numbers stand in a block of
					// their own at the right edge, a stat a line.
					const float statsW =
					  stats.empty()
					    ? 0.0F
					    : std::min(210.0F, std::max(110.0F, size.x * 0.28F));
					const float tx = frame.max.x + 10.0F;
					const float tw =
					  (p.x + size.x - kCardPad - statsW - 10.0F) - tx;
					if (tw < 24.0F)
						return;
					const std::string name = gui::ui::ElideText(*font, caption, tw);
					const std::vector<std::string> det =
					  WrappedText(*font, detail, tw / 0.8F, 3);
					const std::string priceLine =
					  owned ? (maxed ? "Owned" : "Bought") : priceText;

					// The words from the top of their block: the name, then
					// the description under it with a breath of air between
					// the lines.
					const float nameH = font->Measure(name).y;
					const float detH = font->Measure(name).y * 0.8F;
					const float priceH = font->Measure(priceLine).y;
					float y = p.y + kCardPad + 2.0F;
					const Vector4 nameCol =
					  greyed ? MakeVector4(0.55F, 0.55F, 0.57F, 1)
					         : MakeVector4(1, 1, 1, 1);
					const Vector4 detCol =
					  greyed ? MakeVector4(0.42F, 0.42F, 0.44F, 1)
					         : MakeVector4(0.8F, 0.8F, 0.8F, 1);
					font->DrawShadow(name, MakeVector2(tx, y), 1.0F, nameCol,
						                 MakeVector4(0, 0, 0, 0.4F));
					y += nameH + kLineGap;
					for (const std::string& l : det) {
						const float bottom = p.y + size.y - kCardPad - priceH;
						if (y + detH > bottom - kLineGap)
							break;
						font->DrawShadow(l, MakeVector2(tx, y), 0.8F, detCol,
							                 MakeVector4(0, 0, 0, 0.4F));
						y += detH + kLineGap;
					}
					// The price on a line of its own under the words, in its
					// colour: the gold a purse can reach, the red it cannot. A
					// greyed card says it in grey, for it is not this
					// player's to buy.
					const float priceY =
					  std::min(y + kLineGap, p.y + size.y - kCardPad - priceH);
					const Vector4 priceCol =
					  greyed ? MakeVector4(0.5F, 0.5F, 0.5F, 1)
					         : owned ? MakeVector4(0.55F, 0.95F, 0.55F, 1)
					                 : poor ? MakeVector4(1.0F, 0.45F, 0.45F, 1)
					                        : MakeVector4(0.75F, 0.95F,
					                                      0.35F, 1);
					font->DrawShadow(priceLine, MakeVector2(tx, priceY), 1.0F,
						                 priceCol, MakeVector4(0, 0, 0, 0.4F));
					// The numbers, a line a stat, in their block at the right
					// edge.
					if (!stats.empty()) {
						const float sx = p.x + size.x - kCardPad - statsW;
						float sy = p.y + kCardPad + 2.0F;
						for (const std::string& l : stats) {
							font->DrawShadow(
							  l, MakeVector2(sx, sy), 0.8F,
							  greyed ? MakeVector4(0.42F, 0.42F, 0.44F, 1)
							         : MakeVector4(0.85F, 0.85F, 0.75F, 1),
							  MakeVector4(0, 0, 0, 0.4F));
							sy += font->Measure(l).y * 0.8F + kLineGap;
						}
					}
				}
			};

			// A row of the shelf: one card, stood below the empty band
			// that opens its group when one starts there.
			class StoreShelfRow : public UIElement {
				float gap;

			public:
				StoreShelfRow(UIManager* manager, float gap)
				    : UIElement(manager), gap(gap) {}
				void OnResized() override {
					for (const auto& card : GetChildren()) {
						card->SetBounds(
						  AABB2(0.0F, gap, size.x, size.y - gap));
					}
				}
			};

			class StoreShelfModel : public gui::ui::ListViewModel {
				UIManager* manager;
				const training::store::Item* items;
				const std::map<std::string, StoreText>* texts;
				const std::vector<ShelfEntry> entries;
				std::vector<Handle<UIElement>> rows;
				std::function<void(int, UIElement&)> select;

			public:
				StoreShelfModel(
				  UIManager* manager, const training::store::Item* items,
				  const std::map<std::string, StoreText>* texts,
				  const std::vector<ShelfEntry>& entries,
				  std::function<void(int, UIElement&)> select)
				    : manager(manager), items(items), texts(texts),
				      entries(entries), select(std::move(select)) {
					rows.resize(entries.size());
				}

				int GetNumRows() override { return static_cast<int>(rows.size()); }

				Handle<UIElement> CreateElement(int row) override {
					if (!rows[row]) {
						const ShelfEntry& e = entries[row];
						auto card = Handle<StoreCard>::New(
						  manager, &items[e.index], row, texts, e.greyed);
						// A greyed rung is another class's: seen, not bought,
						// so the click falls through rather than asking the
						// server for a buy it would refuse.
						card->activated =
						  [this, index = e.index,
						   greyed = e.greyed](UIElement& s) {
							  if (!greyed)
								  select(index, s);
						  };
						auto element =
						  Handle<StoreShelfRow>::New(manager, e.gap);
						element->AddChild(card.GetPointerOrNull());
						rows[row] = element.Cast<UIElement>();
					}
					return rows[row];
				}
			};
		} // namespace

		StoreWindow::StoreWindow(UIElement* owner, ClientUI* ui)
		    : UIElement(owner != nullptr ? &owner->GetManager()
		                                 : &ui->GetUIManager()),
		      ui(ui), owner(owner) {
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(manager->GetRootElement().GetBounds());
			isMouseInteractive = true;

			const float w = std::min(manager->screenWidth - 32.0F, 1000.0F);
			const float h = std::min(manager->screenHeight - 48.0F, 620.0F);
			const AABB2 contents((manager->screenWidth - w) * 0.5F,
			                     (manager->screenHeight - h) * 0.5F, w, h);
			{
				// The title, centred over the whole window rather than hugged to
				// the left edge: the shelves sit under either half of it.
				auto title = Handle<gui::ui::Label>::New(manager);
				title->text = _Tr("Client", "Store");
				title->SetFont(&ui->GetFontManager().GetHeadingFont());
				title->shadowColor = MakeVector4(0, 0, 0, 0.4F);
				title->alignment = MakeVector2(0.5F, 0.0F);
				title->SetBounds(AABB2(contents.min.x, contents.min.y + 6, w, 30));
				AddChild(title.GetPointerOrNull());
			}
			{
				// The purse and the world's standing, said in one line - centred
				// under the title and riding close to it, so the two read as one
				// heading rather than two floating rows.
				auto line = Handle<gui::ui::Label>::New(manager);
				line->text = _Tr("Client", "Wave 0 - 0 Zedcreds - Core 0/0");
				line->shadowColor = MakeVector4(0, 0, 0, 0.4F);
				line->textScale = 0.9F;
				line->alignment = MakeVector2(0.5F, 0.0F);
				line->SetBounds(AABB2(contents.min.x, contents.min.y + 34, w, 20));
				status_label = line.GetPointerOrNull();
				AddChild(status_label.GetPointerOrNull());
			}

			// The tabs across the top, as many as the page stands: the
			// release one, the Weapons shelf alone. The shelf under them
			// follows the one that is pressed, and the first tab stands
			// open when the page is raised.
			const float tabY = contents.min.y + 64.0F;
			const float tabW = (w - 24.0F) / 3.0F;
			for (int i = 0; i < kTabCount; ++i) {
				const int cat = kTabOrder[i];
				auto button = Handle<gui::ui::Button>::New(manager);
				button->caption = _Tr("Client", kTabNames[i]);
				button->hotKeyText = "";
				button->SetBounds(
					AABB2(contents.min.x + 12.0F + float(i) * tabW, tabY,
					       tabW - 4.0F, 26.0F));
				button->activated = [this, cat](UIElement&) { ShowCategory(cat); };
				if (cat == 0)
					gun_button = button;
				else if (cat == 1)
					perk_button = button;
				else
					core_button = button;
				AddChild(button.GetPointerOrNull());
			}
			{
			// The shelf under the tabs: rows of cards, as many rows as
			// the page holds, one card a row.
				auto view = Handle<gui::ui::ListViewBase>::New(manager);
				grid = view.GetPointerOrNull();
				const float gridY = tabY + 38.0F;
				const float gridH = contents.max.y - 56.0F - gridY;
				grid->SetBounds(
					AABB2(contents.min.x + 12.0F, gridY, w - 24.0F, gridH));
				const int visibleRows = Clamp(int(gridH / 108.0F), 1, 4);
				grid->rowHeight = std::floor(gridH / float(visibleRows));
				AddChild(grid.GetPointerOrNull());
			}
			{
				auto button = Handle<gui::ui::Button>::New(manager);
				button->caption = _Tr("Client", "Back");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(contents.max.x - 132,
					                        contents.max.y - 44, 116, 30));
				button->activated = [this](UIElement&) { Close(); };
				AddChild(button.GetPointerOrNull());
			}
			LoadTexts();
			ShowCategory(kTabOrder[0]);
		}

		void StoreWindow::Run() {
			// ClientUI can close/rebuild this screen on resize/disconnection too.
			ui->SetActiveUI(this);
		}

		void StoreWindow::Close() {
			Handle<StoreWindow> keepAlive(this);
			ui->SetActiveUI(owner);
			if (closed)
				closed(*this);
		}

		void StoreWindow::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape")
				Close();
			else
				UIElement::HotKey(key);
		}

		void StoreWindow::LoadTexts() {
			if (texts_read)
				return;
			texts_read = true;
			// The store's own words from the Tower Defense pak: one line a
			// card, "sku | name | description | price". A line whose sku the
			// catalogue does not know is passed over; a file that cannot be
			// read at all is not an error, for the catalogue's words stand.
			std::string bytes;
			try {
				auto stream = FileManager::OpenForReading("Store/items.txt");
				if (stream)
					bytes = stream->ReadAllBytes();
			} catch (const std::exception&) {
				return;
			}
			size_t lineStart = 0;
			while (lineStart < bytes.size()) {
				const size_t lineEnd = bytes.find('\n', lineStart);
				const std::string line =
					bytes.substr(lineStart,
						lineEnd == std::string::npos
							? std::string::npos
							: lineEnd - lineStart);
				lineStart =
					lineEnd == std::string::npos ? bytes.size() : lineEnd + 1;
				const std::string trimmed = Trimmed(line);
				if (trimmed.empty() || trimmed[0] == '#')
					continue;
				std::vector<std::string> fields;
				size_t fieldStart = 0;
				while (fields.size() < 4) {
					const size_t bar = trimmed.find('|', fieldStart);
					if (bar == std::string::npos || fields.size() == 3) {
						fields.push_back(Trimmed(trimmed.substr(fieldStart)));
						break;
					}
					fields.push_back(
						Trimmed(trimmed.substr(fieldStart, bar - fieldStart)));
					fieldStart = bar + 1;
				}
				if (fields.size() < 2)
					continue;
				// Only a card the catalogue knows can carry words.
				if (training::store::find_item(fields[0].c_str()) < 0)
					continue;
				StoreText text;
				text.name = fields[1];
				if (fields.size() > 2)
					text.detail = fields[2];
				if (fields.size() > 3 && !fields[3].empty())
					text.price = std::atoi(fields[3].c_str());
				texts[fields[0]] = text;
			}
		}

		int StoreWindow::ShelfSignature() const {
			// The shape the shelf was built for: the class the player
			// stands in and the rungs they own. When the word changes the
			// list is rebuilt, so what stays on it is what is still for
			// sale to this player in this class.
			int sig = int(state.weapon) + 1;
			for (size_t i = 0; i < training::store::kItemCount; ++i) {
				if (training::store::kItems[i].effect !=
				    training::store::kEffectWeaponSkin)
					continue;
				sig = sig * 5 + int(state.level_of(int(i))) + 1;
			}
			return sig;
		}

		void StoreWindow::ShowCategory(int cat) {
			category = cat;
			if (gun_button)  gun_button->toggled = cat == 0;
			if (perk_button) perk_button->toggled = cat == 1;
			if (core_button) core_button->toggled = cat == 2;

			// The shelf's rows, the classes in the order the release reads
			// them: the player's own class first, then the tools (the
			// grenade's dress and the spade's), then the other classes'
			// rungs, greyed, for a player buys only the class they stand
			// in. A rung owned and the ladder's free floor stay off it,
			// for they only make it long.
			std::vector<ShelfEntry> entries;
			if (cat == training::store::kGuns) {
				const int my_gun =
				  state_fresh && state.weapon <= 2 ? int(state.weapon) : -1;
				const auto rungs = [&](int gun, bool greyed) {
					const size_t before = entries.size();
					for (size_t i = 0; i < training::store::kItemCount; ++i) {
						const training::store::Item& it =
						  training::store::kItems[i];
						if (it.effect != training::store::kEffectWeaponSkin)
							continue;
						if (int(it.arg) != gun)
							continue;
						// The free floor is worn by everyone from the first
						// spawn, and an owned rung is a rung no card needs to
						// say again.
						if (it.tier == 1 && it.price == 0)
							continue;
						if (state.level_of(int(i)) >= 1)
							continue;
						ShelfEntry e;
						e.index = int(i);
						e.greyed = greyed;
						entries.push_back(e);
					}
					// A group opens with a band of air above it, the first
					// row of the shelf excepted.
					if (entries.size() > before && before > 0)
						entries[before].gap = kGroupGap;
				};
				if (my_gun >= 0)
					rungs(my_gun, false);
				rungs(4, false);  // the grenade's dress
				rungs(3, false);  // the spade's
				for (int g = 0; g <= 2; ++g) {
					if (g != my_gun)
						rungs(g, true);
				}
			} else {
				// A shelf off the release keeps its own word: its cards in
				// the order the purse reads them in.
				std::vector<int> indices;
				for (size_t i = 0; i < training::store::kItemCount; ++i)
					if (training::store::kItems[i].category == cat)
						indices.push_back(int(i));
				std::stable_sort(indices.begin(), indices.end(),
				                 [](int a, int b) {
				                     if (training::store::kItems[a].price !=
				                         training::store::kItems[b].price)
				                         return training::store::kItems[a].price <
				                                training::store::kItems[b].price;
				                     return a < b;
				                 });
				for (int i : indices)
					entries.push_back(ShelfEntry{i, false, 0.0F});
			}

			auto model = Handle<StoreShelfModel>::New(
			  &GetManager(), training::store::kItems, &texts, entries,
			  [this](int index, UIElement&) {
			      const training::store::Item& item =
			        training::store::kItems[index];
			      Buy(item.sku);
			  });
			grid->SetModel(model.GetPointerOrNull());
			grid->ScrollToRow(0);
			model_sig = ShelfSignature();
			UpdateStatus(false);
		}

		void StoreWindow::Buy(const std::string& sku) {
			// The store's own word, sent as the chat the server already reads:
			// the purse, the price and the verdict are the server's, and this
			// window only asks.
			std::string text = "/buy " + sku;
			ui->GetHelper().SayGlobal(text);
			message = _Tr("Client", "Buying ") + sku + "...";
			messageAt = GetManager().time;
			UpdateStatus(false);
		}

		void StoreWindow::UpdateStatus(bool /*idle_prompt*/) {
			if (status_label == nullptr)
				return;
			// The word is only ever a moment's word: the purse, the prices and the
			// owned marks are what the server says, and once it has answered the
			// line goes back to the world's standing rather than sitting on a
			// purchase that has long since been settled.
			if (!message.empty() && GetManager().time - messageAt > 3.0F)
				message.clear();
			std::string line = message;
			if (line.empty()) {
				line = _Tr("Client", "Wave");
				line += " " + std::to_string(state.wave);
				line += " - " + std::to_string(state.zedcreds) + " ";
				line += _Tr("Client", training::store::kCurrencyPlural);
				if (state.coreMaxHp > 0) {
					line += " - " + _Tr("Client", "Core") + " ";
					line += std::to_string(state.coreHp) + "/" +
					        std::to_string(state.coreMaxHp);
				}
			}
			status_label->text = line;
		}

		void StoreWindow::Render() {
			auto& r = GetManager().GetRenderer();
			// The same overlay and panel the mods page and the pause menu use,
			// rather than a chrome of this window's own.
			const float sw = GetManager().screenWidth, sh = GetManager().screenHeight;
			SetColorNP(r, MakeVector4(0, 0, 0, 0.7F));
			r.DrawImage(nullptr, AABB2(0, 0, sw, sh));
			const float w = std::min(GetManager().screenWidth - 32.0F, 1000.0F);
			const float h = std::min(GetManager().screenHeight - 48.0F, 620.0F);
			const AABB2 contents((sw - w) * 0.5F, (sh - h) * 0.5F, w, h);
			SetColorNP(r, MakeVector4(0, 0, 0, 0.5F));
			r.DrawImage(nullptr, contents);
			// The shelf's own art, when the pak ships one: an image of the
			// tab's Gfx/BG laid over the plain panel at the player's own
			// veil. A missing image is not an error; the panel stands.
			if (!tab_bg_read[category]) {
				tab_bg_read[category] = true;
				try {
					tab_bg[category] = r.RegisterImage(kTabBackgrounds[category]);
				} catch (const std::exception&) {
					tab_bg[category] = {};
				}
			}
			if (tab_bg[category]) {
				SetColorNP(r, MakeVector4(1, 1, 1,
					Clamp((float)cg_storeBgOpacity, 0.0F, 1.0F)));
				r.DrawImage(tab_bg[category], contents);
			}
			// The cards read the store each draw: the purse, the prices and
			// the owned marks all follow what the server last said.
			if (ui->client != nullptr) {
				state = ui->client->GetStoreState();
				state_fresh = state.received;
			}
			// The shelf's shape follows the store's word: a rung bought or a
			// class changed is a list rebuilt, so what stays on it is what
			// is still worth a look.
			if (state_fresh && grid && ShelfSignature() != model_sig)
				ShowCategory(category);
			if (state_fresh && grid) {
				for (auto& row : grid->GetChildren()) {
					for (auto& card : row->GetChildren()) {
						auto* storeCard = dynamic_cast<StoreCard*>(card.GetPointerOrNull());
						if (storeCard == nullptr)
							continue;
						const int index = storeCard->GetItemIndex();
						if (index < 0)
							continue;
						const std::uint8_t level = state.level_of(index);
						// The wire's price is the purse's own truth: the host's
						// towerdefense.toml repriced the cards on the server, and
						// the store's word now says what it charges. A wire that
						// said none falls back to the pak's own word, and then to
						// the catalogue's.
						const int asked = state.price_of(index);
						storeCard->SetState(level, asked, state.zedcreds);
					}
				}
			}
			UpdateStatus(false);
			UIElement::Render();
		}

	} // namespace client
} // namespace spades