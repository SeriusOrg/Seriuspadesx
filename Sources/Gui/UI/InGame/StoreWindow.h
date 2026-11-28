/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <Client/StoreState.h>
#include <Core/Math.h>
#include <Gui/UI/Framework/UIElement.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>

#include <map>
#include <string>

namespace spades {

	namespace client {
		class Client;
		class ClientUI;
		class IImage;

		/** One card of the store's own words: one line of Store/items.txt
		 * in the Tower Defense pak, "sku | name | description | price".
		 * What the file does not say falls back to the catalogue's own
		 * words, and the price stands only for a wire that said none -
		 * the server's word is what the shelf charges - so a missing
		 * file leaves the store standing. */
		struct StoreText {
			std::string name;
			std::string detail;
			int price = -1;
		};

		/**
		 * The Tower Defense store: a landscape page. The release stands
		 * one tab, the Weapons shelf, alone; Perks and Core sit out until
		 * their cards come back. The shelf reads as the classes climb:
		 * the player's own class first, then the tools (the grenade's
		 * dress and the spade's), then the other classes' rungs greyed
		 * and at the foot of the shelf, for a player buys only the class
		 * they stand in; a rung owned and the ladder's free floor stay
		 * off it, and the shelf is rebuilt when the store's word of it
		 * changes. A card is a row: the voxel thumbnail stands on the
		 * left in its own frame, its name and its description wrapped
		 * beside it, the price on a line of its own under the words, and
		 * the rung's stats in a block at the right edge. A card without a
		 * model of its own wears the breadcrumb as a placeholder rather
		 * than a drawn icon.
		 *
		 * Each tab can carry its own art, an image of the Tower Defense
		 * pak's Gfx/BG (Weapons.png, Perks.png, Core.png) drawn over the
		 * plain panel at cg_storeBgOpacity; a missing image leaves the
		 * panel standing, so the page works before its art is drawn.
		 *
		 * A thumbnail stands still, and turns only while the mouse rests
		 * on its card. Shown over its owner, which it disables meanwhile.
		 * It reads the store state the server says (Smoke's sixth sub
		 * packet) and asks the server for a buy with /buy <sku>; it holds
		 * no Zedcred of its own.
		 */
		class StoreWindow : public gui::ui::UIElement {
			ClientUI* ui;
			gui::ui::UIElement* owner;
			StoreState state;
			bool state_fresh = false;

			std::map<std::string, StoreText> texts;
			bool texts_read = false;

			Handle<gui::ui::Button> gun_button;
			Handle<gui::ui::Button> perk_button;
			Handle<gui::ui::Button> core_button;
			Handle<IImage> tab_bg[3];
			bool tab_bg_read[3] = {false, false, false};
			Handle<gui::ui::ListViewBase> grid;
			Handle<gui::ui::Label> status_label;
			int category = 0;
			int model_sig = -1;
			std::string message;
			float messageAt = -1000.0F;

			void LoadTexts();
			int ShelfSignature() const;
			void ShowCategory(int cat);
			void Buy(const std::string& sku);
			void UpdateStatus(bool idle_prompt);

		public:
			gui::ui::EventHandler closed;

			StoreWindow(gui::ui::UIElement* owner, ClientUI* ui);
			void Run();
			void Close();
			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
