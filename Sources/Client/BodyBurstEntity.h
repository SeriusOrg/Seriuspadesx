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

#include "ILocalEntity.h"
#include "IModel.h"
#include <Core/Math.h>

#include <string>
#include <vector>

namespace spades {

	namespace client {
		class Client;
		class GameMap;
		class IRenderer;
		class Player;
		class PlayerOutfits;

		/** Comic gore effect: a body blown to pieces by an explosion in
		 *  this mode, the two arms and the two legs flying apart, each a
		 *  ragged piece of its own, no torso and no head left standing in
		 *  for them, and fat chunks of the rest between them. A single
		 *  entity handles all the pieces of one burst. */
		class BodyBurstEntity : public ILocalEntity {
			Client* client;
			IRenderer& renderer;

			struct Piece {
				Matrix4 matrix;  // current position and tumbling orientation
				Vector3 velocity;
				Vector3 angVel;  // tumble speed, radians per second
				float scale;
				float life;     // seconds left before this piece disappears
				int modelIndex; // limbModels when limb, chunkModels when not
				bool limb;      // a limb, worn in the colour of the fallen
				bool splatDone; // blood already drawn on the first impact
			};
			std::vector<Piece> pieces;

			// burst origin, used for fog and sound distance culling
			Vector3 origin;
			// the colour the fallen wore (PlayerOutfit.h)
			Vector3 color;

			// the four limbs of the victim, and the chunks of the rest;
			// missing files are skipped so the effect degrades
			// gracefully without assets
			std::vector<Handle<IModel>> limbModels;
			std::vector<Handle<IModel>> chunkModels;
			// How many pieces this burst counts against the cap on every
			// burst's pieces at once (kMaxLivePieces), until it goes.
			int counted = 0;

		public:
			BodyBurstEntity(Client& client, Player& victim, Vector3 origin,
			                Vector3 blastDir, const PlayerOutfits* models);
			~BodyBurstEntity() override;

			bool Update(float dt) override;
			void Render3D() override;

		private:
			/** Blood splat where a piece lands: flying particles plus
			 *  persistent marks on the blocks (Client::Bleed is private,
			 *  so the same particle look is reproduced here). */
			void BloodSplat(GameMap& map, Vector3 pos, Vector3 vel);
		};
	} // namespace client
} // namespace spades
