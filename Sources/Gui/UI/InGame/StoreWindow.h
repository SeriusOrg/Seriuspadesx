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

#pragma once

#include <Client/StoreState.h>
#include <Core/Math.h>
#include <Gui/UI/Framework/UIElement.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace client {
		class Client;
		class ClientUI;

		/**
		 * The Tower Defense store: the cards of the Guns shelf, each with a
		 * rotating voxel thumbnail, its Zedcred price, and what it costs to
		 * own. Built over the pause menu's own widgets and palette, like the
		 * mods page beside it.
		 *
		 * Shown over its owner, which it disables meanwhile. It reads the
		 * store state the server says (Smoke's sixth sub packet) and asks
		 * the server for a buy with /buy <sku>; it holds no Zedcred of its own.
		 */
		class StoreWindow : public gui::ui::UIElement {
			ClientUI* ui;
			gui::ui::UIElement* owner;
			StoreState state;
			bool state_fresh = false;

			Handle<gui::ui::Button> gun_button;
			Handle<gui::ui::Button> perk_button;
			Handle<gui::ui::Button> core_button;
			Handle<gui::ui::ListViewBase> grid;
			Handle<gui::ui::Label> status_label;
			int columns = 1;
			int category = 0;
			std::string message;
			float messageAt = -1000.0F;

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