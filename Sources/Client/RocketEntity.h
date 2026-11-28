/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

#include "Client.h"
#include "ILocalEntity.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class IModel;
		class IAudioDevice;
		class IAudioChunk;

		// Visual rocket projectile, spawned when a player fires the
		// RPG (the grenade tool dressed as the rocket launcher). Purely
		// cosmetic: the damage and the crater are computed by the
		// server, which simulates the same straight flight. Physics
		// mimics an RPG-7 round:
		//   - constant cruise speed, a light gravity drop
		//   - an air burst at MaxRange when nothing was hit
		// The launch-direction drift of the standalone client was
		// dropped: the server would have to roll the same random
		// numbers, and a straight flight reads the same in play.
		class RocketEntity : public ILocalEntity {
			Client &client;
			Handle<IModel> rocketModel;
			                               // WeaponMods/GrenadeRPG/Models/
			                               // Weapons/Rifle/Rocket.kv6

			Vector3 position;
			Vector3 direction; // normalized, constant
			float traveled = 0.f;  // distance traveled (world units)
			float smokeTimer = 0.f;
			float age = 0.f;

			// --- flight "woosh" loop ---------------------------
			// IAudioDevice::Play returns no handle and the device has
			// no stop API, so a loop=true sound could never be cut at
			// the explosion. Same pattern as the smoke grenade: a
			// SHORT sample re-triggered at its EXACT duration while
			// flying; when the rocket dies, Update simply stops
			// re-triggering.
			IAudioDevice *audioDevice;       // may be null when muted
			Handle<IAudioChunk> wooshChunk;  // charged by the caller:
			                                 // WeaponMods/GrenadeRPG/
			                                 // Sounds/Weapons/Rifle/
			                                 // woosh.opus
			Handle<IAudioChunk> explodeChunk; // Sounds/Weapons/Grenade/
			                                  // Explode1.opus
			float wooshTimer = 0.f;          // re-trigger countdown (s)

			Handle<GameMap> map; // for block collision

			// ---- Tunables (keep in sync with the server's
			// Sources/Training/Grenade.cpp, rocket_impact) ---------
			static constexpr float Speed = 60.f;            // world units/s
			// a light bow to the pull of the world over the flight
			static constexpr float Gravity = 0.138f;        // world units/s^2
			static constexpr float SmokeInterval = 0.012f;  // s between puffs
			static constexpr float SmokeLifeTime = 0.8f;   // s; trail = speed * this
			static constexpr float SmokeRadius = 0.5f;      // trail puff radius
			static constexpr float SmokeGrowth = 0.3f;       // puff growth per second
			static constexpr float ExplosionLifeTime = 8.1f; // 4.5x the grenade cloud
			// dynamic lights: the flying rocket glows like 2x a rifle
			// bullet flash, the detonation like 4x
			static constexpr float FlightLightRadius = 3.0f;
			static constexpr float ExplosionLightRadius = 6.0f;
			static constexpr float ExplosionLightTime = 0.5f; // s, decaying
			// Lingering dark-gray impact smoke: stays ~2.5 s at the
			// impact point, as wide as the impact zone, rising ~7
			// units total (LifeTime x RiseSpeed). z is INVERTED in
			// this engine: the rise speed is negative.
			static constexpr int DarkSmokeCount = 6;
			static constexpr float DarkSmokeLifeTime = 2.5f;    // s
			static constexpr float DarkSmokeRiseSpeed = 2.7f;   // u/s (negative = up)
			static constexpr float DarkSmokeRadius = 2.2f;      // initial radius
			// air burst at 300 blocks when nothing was hit
			static constexpr float MaxRange = 300.f;                // blocks
			// Woosh sample re-trigger period. woosh.opus lasts
			// ~0.2 s and is re-triggered every 0.2 s. It MUST keep a
			// fade-in AND a fade-out (however short), otherwise the
			// loop boundary is a click.
			static constexpr float WooshInterval = 0.2f;      // s

		public:
			RocketEntity(Client &client, IAudioDevice *audioDevice,
			              Handle<IAudioChunk> wooshChunk,
			              Vector3 muzzlePos, Vector3 dir);
			~RocketEntity();

			/** @return false when the entity should be removed. */
			bool Update(float dt) override;
			void Render3D() override;

			static void Preload(IRenderer *); // call once at client startup

		private:
			void SpawnExplosionAt(Vector3 pos); // visual burst (smoke + flash)
		};

		// Brief bright dynamic light left at the detonation point,
		// 4x a rifle bullet flash, decaying over ExplosionLightTime.
		// (Must be its own local entity: RocketEntity is removed the
		// moment it explodes, so it can no longer emit light or play
		// its detonation sound.)
		// Also plays the SINGLE detonation boom: the mono chunk
		// played through the spatial path is inaudible past a dozen
		// blocks, so it plays in 2D (PlayLocal) with a MANUAL distance
		// falloff, which keeps distant explosions audible while
		// still quieter than close ones.
		class RocketFlashEntity : public ILocalEntity {
			Client &client;
			IAudioDevice *audioDevice;        // may be null when muted
			Vector3 origin;
			float age = 0.f;

			// Blast volume. MANDATORY: the engine plays its own
			// explosions at volume 6 (GrenadeExplosion in
			// Client_Update.cpp: param.volume = 6.f for Explode1,
			// 4.f for bullet impacts). Spatial Play uses
			// AL_INVERSE_DISTANCE_CLAMPED with referenceDistance = 1,
			// i.e. gain = 1/distance: at a 30-unit impact, volume 1
			// is -30 dB and completely inaudible. Volume 6 gives the
			// boom the exact same falloff as a grenade explosion.
			static constexpr float BlastVolume = 6.f;

		public:
			RocketFlashEntity(Client &client, IAudioDevice *audioDevice,
			                  Handle<IAudioChunk> explodeChunk,
			                  Vector3 origin);
			bool Update(float dt) override;
			void Render3D() override;
		};
	} // namespace client
} // namespace spades
