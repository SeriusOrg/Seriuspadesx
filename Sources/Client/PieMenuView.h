/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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

#include <array>
#include <string>
#include <vector>

#include "PieMenu.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		class IFont;
		class IRenderer;

		/**
		 * Draws a pie menu and tracks the slice the player is flicking towards. It
		 * knows nothing of where its rings came from: `Client` hands it the pies a
		 * context offers when it opens, and asks it which slice was chosen when it
		 * closes.
		 */
		class PieMenuView {
		public:
			enum Slice { None = -1 };

			static constexpr int kSliceCount = kPieSliceCount;
			static constexpr float kSliceSpan = kTwoPi / static_cast<float>(kSliceCount);

		private:
			// A ring, with what it draws made ready once when the menu opens.
			struct Page {
				Pie pie;
				std::string theme;
				std::array<std::string, kSliceCount> labels;
				// Each label broken into the lines that fit inside its wedge.
				std::array<std::vector<std::string>, kSliceCount> lines;
				// Each tooltip broken into the lines it takes in the hole in
				// the middle of the wheel.
				std::array<std::vector<std::string>, kSliceCount> tooltips;
			};

			IRenderer& renderer;
			IFont* font;
			IFont* bigFont;

			bool open = false;
			int targetPlayerId = -1;
			Vector2 cursor = {0.0F, 0.0F};
			int selection = None;
			int page = 0;

			float openPhase = 0.0F;
			float pagePhase = 1.0F;
			float hintTime = 0.0F;
			// Cleared for good the first time a ring is cycled, so the discovery
			// hint retires itself instead of nagging a player who already knows.
			bool hintNeeded = true;

			std::array<float, kSliceCount> highlight{};

			// The rings on offer, fixed when the menu opens: a ring must not change
			// under a held thumb, whatever arrives while it is held.
			std::vector<Page> pages;

			// Precomputed per-slice ray params (sin/cos of θ_c ± α).
			// Populated once in the constructor; used by scanline fill.
			struct SliceRay { float s1, c1, s2, c2; };
			std::array<SliceRay, kSliceCount> sliceRays;
			std::array<float, kSliceCount> sliceCenterAngles;

			const Page& CurrentPage() const;
			const Pie& CurrentPie() const;
			void UpdateSelection();
			void DrawPageIndicator(Vector2 center, float rOuter, float alpha);

		public:
			PieMenuView(Client*, IFont* font, IFont* bigFont);
			~PieMenuView();

			/** Opens on the pies a context offers, which must not be empty.
			 * The first ring is the page it opens on, every time.
			 * `targetPlayerId` is the player aimed at, or `-1`. */
			void Open(std::vector<Pie> pies, int targetPlayerId = -1);
			int Close();

			bool IsOpen() const { return open; }
			int GetTargetPlayerId() const { return targetPlayerId; }
			int GetSelection() const { return selection; }
			int GetPage() const { return page; }
			int GetPageCount() const { return static_cast<int>(pages.size()); }

			/** The highlighted slice, or null when none is. */
			const PieSlice* GetSelectedSlice() const;

			// Move to the next (dir > 0) or previous (dir < 0) ring, wrapping around.
			// The cursor is kept, so the slice under it stays selected across the flip.
			void CyclePage(int dir);

			void HandleMouseDelta(float dx, float dy);
			void Update(float dt);
			void Draw();
		};
	} // namespace client
} // namespace spades
