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
#include <Training/StoreCatalog.h>

#include <algorithm>
#include <string>
#include <vector>

#include <Client/ClientUI.h>
#include <Client/ClientUIHelper.h>
#include <Client/Client.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Client/NetClient.h>

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

			// The text block under a card's picture: the air under the price, the
			// air between two lines, and the air between the picture and the name.
			// The lines themselves are measured from the font as they are drawn, so
			// the stack is placed from the bottom of the card upwards and a long
			// name or a long price can never hang off the card or push into the
			// picture - which is what the old fixed offsets did.
			constexpr float kCardPad = 9.0F;
			constexpr float kLineGap = 3.0F;
			constexpr float kThumbGap = 6.0F;
			constexpr float kThumbTop = 8.0F;   // air above the picture

			/** The pause menu's palette, so a card reads as one of its buttons. */
			Vector4 CardColour(bool hover, bool pressed, bool owned, bool poor) {
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
				std::uint16_t purse = 0;

				std::unique_ptr<VoxelModelPreview> preview;
				Handle<IImage> image;
				Handle<IImage> sprite;
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
					// The sprite is registered whether there is a model or not: a card with
					// no model used to return here and never ask for its sprite at all, so
					// the ammo card showed nothing.
					if (item->icon != nullptr) {
						try {
							sprite = GetManager().GetRenderer().RegisterImage(item->icon);
						} catch (const std::exception&) {
							sprite = {};
						}
					}
					if (item->model == nullptr)
						return;
					try {
						auto stream = FileManager::OpenForReading(item->model);
						if (!stream) {
							// Said loudly: a card whose picture cannot be read is a blank
							// card, and the log is the only place the reason can be read
							// afterwards.
							SPLog("Store: no file for %s's thumbnail (%s)", item->sku,
							      item->model);
							return;
						}
						auto model = VoxelModel::LoadKV6(*stream);
						stream.reset();
						if (!model) {
							SPLog("Store: %s's thumbnail is not a voxel model (%s)", item->sku,
							      item->model);
							preview.reset();
							return;
						}
						preview = std::make_unique<VoxelModelPreview>(*model);
						if (preview->IsEmpty()) {
							SPLog("Store: %s's thumbnail has no surfaces (%s)", item->sku,
							      item->model);
							preview.reset();
						}
					} catch (const std::exception& e) {
						// A thumbnail that cannot be drawn must not block a card, but it
						// must be said which one, or a blank card is a mystery: a bad
						// model, a bad transform, a surface too big to mesh.
						SPLog("Store: %s's thumbnail could not be built (%s): %s", item->sku,
						      item->model, e.what());
						preview.reset();
					}
				}

				void DrawModel(IRenderer& r, const AABB2& bounds) {
					if (!preview || preview->IsEmpty() || bounds.GetWidth() <= 0 ||
					    bounds.GetHeight() <= 0)
						return;
					const float elapsed = std::max(0.0F, GetManager().time - animationStart);
					const int frame = int(elapsed * 24.0F);
					const float density =
					  std::min({std::max(1.0F, r.ScreenPixelRatio()), 2.0F,
					            512.0F / bounds.GetWidth(), 512.0F / bounds.GetHeight()});
					const int w = std::max(1, int(std::floor(bounds.GetWidth() * density)));
					const int h = std::max(1, int(std::floor(bounds.GetHeight() * density)));
					try {
						const bool resized =
						  !image || int(image->GetWidth()) != w || int(image->GetHeight()) != h;
						if (resized || renderedFrame != frame) {
							const float yaw =
							  std::fmod(initialYaw + float(frame) * (0.4F / 24), 6.283185F);
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
				StoreCard(UIManager* manager, const training::store::Item* item, int index)
				    : ButtonBase(manager), item(item) {
					caption = item->label;
					detail = item->detail;
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
					const std::string total = std::to_string(price);
					const std::string coin = training::store::kCurrencyPlural;
					priceText = price > 0 ? total + " " + coin : "Free";
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
					const bool poor = !owned && price > 0 && purse < price;
					const auto colour = CardColour(hover, pressed && hover, owned, poor);
					SetColorNP(r, colour);
					r.DrawFilledRect(p.x + 1, p.y + 1, p.x + size.x - 1, p.y + size.y - 1);
					SetColorNP(r, MakeVector4(0, 0, 0, colour.w));
					r.DrawOutlinedRect(p.x + 1, p.y + 1, p.x + size.x - 1, p.y + size.y - 1);

					if (!loaded)
						Load();

					// The three lines are measured before anything is drawn, and the
					// stack is placed from the bottom of the card upwards: the picture
					// above it is then given exactly the room that is left, so a long
					// name or a long price can neither hang off the card nor push into
					// the picture.  The old fixed offsets did both.
					auto* font = GetFont();
					std::string name, det, priceLine;
					float nameH = 0.0F, detH = 0.0F, priceH = 0.0F;
					if (font != nullptr) {
						name = gui::ui::ElideText(*font, caption, size.x - 16.0F);
						det = gui::ui::ElideText(*font, detail, size.x - 16.0F, 0.8F);
						priceLine = owned ? (maxed ? "Owned" : "Bought") : priceText;
						nameH = font->Measure(name).y;
						detH = font->Measure(det).y * 0.8F;
						priceH = font->Measure(priceLine).y;
					}
					const float priceY = p.y + size.y - kCardPad - priceH;
					const float detY = priceY - kLineGap - detH;
					const float nameY = detY - kLineGap - nameH;
					const float thumbBottom = (font != nullptr ? nameY : priceY) - kThumbGap;
					const float thumbHeight = thumbBottom - (p.y + kThumbTop);

					if (thumbHeight > 1.0F) {
						const AABB2 area(p.x + 10.0F, p.y + kThumbTop, size.x - 20.0F,
						                 thumbHeight);
						const Vector2 centre = (area.min + area.max) * 0.5F;
						const float mark =
						  std::min({area.GetWidth(), area.GetHeight()});
						if (preview) {
							DrawModel(r, area);
						} else if (iconKind != IconKind::None) {
							// The card's own mark, drawn at the size of the picture it
							// stands in for.
							const float s = mark * 0.78F;
							DrawStoreIcon(r, centre - MakeVector2(s, s) * 0.5F, s,
							              iconKind, iconTier);
						} else if (sprite) {
							// A Hotbar sprite, kept to the card's own shape.
							const float sx =
							  std::min(area.GetWidth() / 40.0F, area.GetHeight() / 40.0F);
							const float half = 20.0F * sx;
							const AABB2 at(centre.x - half, centre.y - half, half * 2.0F,
							               half * 2.0F);
							SetColorNP(r, MakeVector4(1, 1, 1, 1));
							r.DrawImage(sprite, at);
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
					}

					if (font != nullptr) {
						const auto nm = font->Measure(name);
						font->DrawShadow(name,
						                 p + MakeVector2((size.x - nm.x) * 0.5F, nameY - p.y),
						                 1.0F, MakeVector4(1, 1, 1, 1),
						                 MakeVector4(0, 0, 0, 0.4F));
						const auto dm = font->Measure(det) * 0.8F;
						font->DrawShadow(det,
						                 p + MakeVector2((size.x - dm.x) * 0.5F, detY - p.y),
						                 0.8F, MakeVector4(0.8F, 0.8F, 0.8F, 1),
						                 MakeVector4(0, 0, 0, 0.4F));
						const auto pm = font->Measure(priceLine);
						font->DrawShadow(priceLine,
						                 p + MakeVector2((size.x - pm.x) * 0.5F, priceY - p.y),
						                 1.0F,
						                 MakeVector4(owned ? 0.55F : (poor ? 1.0F : 0.75F),
						                              owned ? 0.95F : (poor ? 0.45F : 0.95F),
						                              owned ? 0.55F : (poor ? 0.45F : 0.35F), 1),
						                 MakeVector4(0, 0, 0, 0.4F));
					}
				}
			};

			class StoreGridRow : public UIElement {
				int columns;

			public:
				StoreGridRow(UIManager* manager, int columns)
				    : UIElement(manager), columns(columns) {}
				void OnResized() override {
					const float w = size.x / float(columns);
					int i = 0;
					for (const auto& card : GetChildren())
						card->SetBounds(
						    AABB2(float(i++) * w, 0, w - kCardGap, size.y - kCardGap));
				}
			};

			class StoreGridModel : public gui::ui::ListViewModel {
				UIManager* manager;
				const training::store::Item* items;
				int item_count;
				int columns;
				std::vector<int> indices;
				std::vector<Handle<UIElement>> rows;
				std::function<void(int, UIElement&)> select;

			public:
				StoreGridModel(UIManager* manager, const training::store::Item* items,
				               int item_count, int columns,
				               const std::vector<int>& indices,
				               std::function<void(int, UIElement&)> select)
				    : manager(manager), items(items), item_count(item_count),
				      columns(columns), indices(indices), select(std::move(select)) {
					rows.resize((indices.size() + columns - 1) / columns);
				}

				int GetNumRows() override { return static_cast<int>(rows.size()); }

				Handle<UIElement> CreateElement(int row) override {
					if (!rows[row]) {
						auto element = Handle<StoreGridRow>::New(manager, columns);
						const int end = std::min(static_cast<int>(indices.size()),
						                         (row + 1) * columns);
						for (int i = row * columns; i < end; ++i) {
							auto card = Handle<StoreCard>::New(manager, &items[indices[i]], i);
							card->activated = [this, index = indices[i]](UIElement& s) {
								select(index, s);
							};
							element->AddChild(card.GetPointerOrNull());
						}
						rows[row] = element.Cast<UIElement>();
					}
					return rows[row];
				}
			};
		} // namespace

		StoreWindow::StoreWindow(UIElement* owner, ClientUI* ui)
		    : UIElement(&owner->GetManager()), ui(ui), owner(owner) {
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(manager->GetRootElement().GetBounds());
			isMouseInteractive = true;

			const float w = std::min(manager->screenWidth - 32.0F, 1000.0F);
			const float h = std::min(manager->screenHeight - 48.0F, 640.0F);
			const AABB2 contents((manager->screenWidth - w) * 0.5F,
			                     (manager->screenHeight - h) * 0.5F, w, h);
			// The heading band: the title, the purse line under it, and then the
			// shelves.  The line used to sit at +40 while the shelves started at
			// +48, so the cards painted over the bottom of the words.
			constexpr float kHeading = 60.0F;
			const AABB2 sidebar(contents.min.x + 12, contents.min.y + kHeading,
			                    Clamp(w * 0.18F, 96.0F, 164.0F), h - kHeading - 60.0F);
			const float gridX = sidebar.max.x + 12;

			{
				// The title, centred over the whole window rather than hugged to the
				// left edge: the shelves sit under either half of it.
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

			const float step =
			  std::min(32.0F, (sidebar.GetHeight() - 16) /
			              float(training::store::kCategoryCount));
			for (int cat = 0; cat < training::store::kCategoryCount; ++cat) {
				auto button = Handle<gui::ui::Button>::New(manager);
				button->caption =
				  _Tr("Client", training::store::category_name(static_cast<uint8_t>(cat)));
				button->hotKeyText = "";
				button->SetBounds(AABB2(sidebar.min.x,
				                        sidebar.min.y + float(cat) * step + 8.0F,
				                        sidebar.GetWidth(), step - 2));
				button->activated = [this, cat](UIElement&) { ShowCategory(cat); };
				if (cat == 0) gun_button = button;
				else if (cat == 1) perk_button = button;
				else core_button = button;
				AddChild(button.GetPointerOrNull());
			}
			{
				auto view = Handle<gui::ui::ListViewBase>::New(manager);
				grid = view.GetPointerOrNull();
				grid->SetBounds(
				  AABB2(gridX, sidebar.min.y, contents.max.x - gridX - 12,
				          sidebar.GetHeight()));
				const int visibleRows = Clamp(int(sidebar.GetHeight() / 150.0F), 1, 3);
				grid->rowHeight = std::floor(sidebar.GetHeight() / float(visibleRows));
				columns = Clamp(static_cast<int>(grid->GetItemWidth() / 190.0F), 1, 3);
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
			ShowCategory(0);
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

		void StoreWindow::ShowCategory(int cat) {
			category = cat;
			if (gun_button)  gun_button->toggled = cat == 0;
			if (perk_button) perk_button->toggled = cat == 1;
			if (core_button) core_button->toggled = cat == 2;

			std::vector<int> indices;
			for (size_t i = 0; i < training::store::kItemCount; ++i)
				if (training::store::kItems[i].category == cat) indices.push_back(int(i));
			// A card asked by the server's word stands under the word that
			// asks it: the tiers of a gun in the order they are raised in,
			// because that is the order a player reads them in.
			std::stable_sort(indices.begin(), indices.end(),
			                 [](int a, int b) {
			                     if (training::store::kItems[a].price !=
			                         training::store::kItems[b].price)
			                         return training::store::kItems[a].price <
			                                training::store::kItems[b].price;
			                     return a < b;
			                 });

			auto model = Handle<StoreGridModel>::New(
			  &GetManager(), training::store::kItems,
			  static_cast<int>(training::store::kItemCount), columns, indices,
			  [this](int index, UIElement&) {
			      const training::store::Item& item = training::store::kItems[index];
			      Buy(item.sku);
			  });
			grid->SetModel(model.GetPointerOrNull());
			grid->ScrollToRow(0);
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
			const float h = std::min(GetManager().screenHeight - 48.0F, 640.0F);
			const AABB2 contents((sw - w) * 0.5F, (sh - h) * 0.5F, w, h);
			SetColorNP(r, MakeVector4(0, 0, 0, 0.5F));
			r.DrawImage(nullptr, contents);
			// The cards read the store each draw: the purse, the prices and
			// the owned marks all follow what the server last said.
			if (ui->client != nullptr) {
				state = ui->client->GetStoreState();
				state_fresh = state.received;
			}
			if (state_fresh && grid) {
				for (auto& row : grid->GetChildren()) {
					for (auto& card : row->GetChildren()) {
						auto* storeCard = dynamic_cast<StoreCard*>(card.GetPointerOrNull());
						if (storeCard == nullptr)
							continue;
						const int index = storeCard->GetItemIndex();
						if (index < 0)
							continue;
						const training::store::Item& item = training::store::kItems[index];
						const std::uint8_t level = state.level_of(index);
						std::uint16_t price = item.price;
						// The catalogue's price. The host's towerdefense.toml can
						// reprice a card on its own side, and the shipped table
						// matches this one, but the wire carries no prices, so a
						// host that edits them is seen only in what it charges.
						storeCard->SetState(level, price, state.zedcreds);
					}
				}
			}
			UpdateStatus(false);
			UIElement::Render();
		}

	} // namespace client
} // namespace spades