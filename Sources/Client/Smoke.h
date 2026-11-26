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

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "IAudioChunk.h"
#include "ILocalEntity.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class Client;
		class IAudioDevice;
		class NetPacketReader;

		/**
		 * Client-side state of the *Smoke* protocol extension (extension id 0x3F,
		 * packet id 0x7F), docs/extensions/smoke.md: smoke the server puts in the
		 * world - a cloud, a rising column, or a heavy vapour that pools at
		 * the floor - in a colour, opacity and size, for a
		 * time. Only drawn; the server decides what it means for the game.
		 *
		 *     0  Smoke        S->C  id(1) style(1) x y z(4 each) radius(1) r g b
		 *                           opacity(1 each) duration(2) fade(1) flags(1)
		 *     1  Clear Smoke  S->C  id(1)   255: every smoke
		 *     2  Path         S->C  id(1) r g b(1 each) count(1) then the way
		 *                           as its turns: the first x y z as x(2) y(2)
		 *                           z(1), each later one a byte of delta apiece
		 *     3  Turrets      S->C  count(1) then each gun as 9 bytes:
		 *                           id(1) level(1) x(2) y(2) z(1) locked(1)
		 *                           yaw(1) pitch(1). The whole set of the
		 *                           server's sentry guns, replacing everything
		 *                           sent before: what this list does not name
		 *                           is not standing.
		 *     4  Turret Shot  S->C  id(1), at the very tick the shot lands
		 *     5  Core          S->C  x(2) y(2) z(1): the cell the core's
		 *                           column stands on and the plane of its
		 *                           top, moving the flag the client draws
		 *                           as the core, and the tent it draws for
		 *                           the base one plane beneath (a vanilla
		 *                           packet cannot say it without a
		 *                           capture or a drop nobody made)
		 */
		struct SmokeParams {
			// 0 spreads as a wide cloud, 1 rises as a thin column, 2 gathers
			// as a heavy vapour that hugs the floor and renews rather than
			// climbs - nothing rises out of it, so a trail of it lies along
			// the ground where the eye reads it as a path. 3 is a blast:
			// the whole burst painted once at a stroke, not poured. 4 is a
			// spray: drops of the colour itself, thrown up off the origin
			// to fall again, the splash of blood read in the shade the
			// server sends.
			enum Style : std::uint8_t {
				Cloud = 0, Column = 1, Ground = 2, Blast = 3, Spray = 4};
			static constexpr int kAnonymous = 255;

			int id = kAnonymous;
			int style = Cloud;
			Vector3 origin;
			float radius = 5.0F;   // blocks
			Vector4 color;         // RGB 0-1, A the opacity
			float duration = 10.0F; // s it keeps coming
			float fade = 5.0F;     // s it takes to go after
			bool sound = false;
		};

		/** A smoke: emits puffs for its duration, then lets them fade. */
		class SmokeEntity : public ILocalEntity {
		public:
			SmokeEntity(Client& client, IAudioDevice* audio, Handle<IAudioChunk> sound,
			            const SmokeParams& params, std::shared_ptr<bool> stop);

			bool Update(float dt) override;
			void Render3D() override {} // the puffs are entities of their own

		private:
			Client& client;
			IAudioDevice* audio;
			Handle<IAudioChunk> sound;
			SmokeParams params;
			std::shared_ptr<bool> stop; // set by Clear Smoke
			float age = 0.0F;
			float puffTimer = 0.0F;
			float soundTimer = 0.0F;

			void Puff(float untilGone);
		};

		/** The server's smokes, by id, so Clear Smoke can stop them. */
		class SmokeRegistry {
		public:
			static constexpr std::uint8_t kExtensionId = 0x3F;
			static constexpr std::uint8_t kPacketId = 64 + kExtensionId;
			static constexpr std::uint8_t kSubSmoke = 0;
			static constexpr std::uint8_t kSubClear = 1;
			/** Version 3 of the extension: the same negotiation, the
			 * sentry guns carried on two more sub packets - a Turrets
			 * list a v2 client ignores on arrival, and its shots. */
			static constexpr std::uint8_t kVersion = 3;
			static constexpr std::uint8_t kSubPath = 2;
			static constexpr std::uint8_t kSubTurret = 3;
			static constexpr std::uint8_t kSubTurretShot = 4;
			/** The core's standing place, said again at once when it moves
			 * (the /core server command) and whole on the same five seconds
			 * the guns are - a client that finished loading mid game is
			 * owed the flag and the tent of its base whatever its
			 * StateData said. */
			static constexpr std::uint8_t kSubCore = 5;
			/** One player's Tower Defense store: the Zedcred purse, the wave and
			 * core standing, and which upgrades are already bought. Sent to
			 * each player on their own, since the purse differs. */
			static constexpr std::uint8_t kSubStore = 6;

			/** The stop flag for a new smoke; an id in use stops the old one. */
			std::shared_ptr<bool> Start(int id);
			/** Stops one smoke, or every one (255); their puffs fade. */
			void Clear(int id);

		private:
			std::unordered_map<int, std::shared_ptr<bool>> smokes;
		};

		/** A Smoke packet (after its id) applied to the client's world: a smoke
		 * started or cleared. A malformed one is dropped; an unknown sub packet,
		 * and an unknown style (drawn as a cloud), are how it grows. */
		void ApplySmokePacket(NetPacketReader& r, Client& client);
	} // namespace client
} // namespace spades
