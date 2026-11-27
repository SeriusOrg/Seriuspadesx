/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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

#include "GameConstants.h"
#include "Player.h"

namespace spades {
	namespace client {
		class World;
		class Player;
		struct GameProperties;

		class Weapon {
			World& world;
			Player& owner;
			float time;
			bool shooting;
			bool shootingPreviously;
			bool reloading;
			bool unejectedBrass;
			float nextShotTime;
			float ejectBrassTime;
			float reloadStartTime;
			float reloadEndTime;

			// Local player only. A shotgun loads shell by shell, and the server answers each with
			// a WeaponReload about one round trip after the shell's animation, so the client
			// animates shells back to back by itself and counts those the server has yet to
			// confirm.
			int unconfirmedShells;
			// The last time the reload moved: an animation playing or a WeaponReload received.
			float lastReloadActivityTime;

			bool lastDryFire;

			int ammo;
			int stock;

			// The Tower Defense ladder's rung this gun fights at (see
			// the public SetTier below).
			int tier;

			// Tower Defense never runs out of rounds: the server tops the
			// reserve back up behind every answer, so its count never
			// falls (see the public SetUnlimited below).
			bool unlimited;

			void StartReloadAnimation();
			bool NeedsAnotherShell();
			void UpdateLocalReload();
			void FinishReload();
			void GiveUpWaitingForServer();

		public:
			Weapon(World&, Player&);
			virtual ~Weapon();
			virtual std::string GetName() = 0;
			virtual float GetDelay() = 0;
			virtual int GetClipSize() = 0;
			virtual int GetMaxStock() = 0;
			virtual float GetReloadTime() = 0;
			virtual float GetEjectBrassTime() = 0;
			virtual bool IsReloadSlow() = 0;
			virtual int GetDamage(HitType) = 0;
			virtual WeaponType GetWeaponType() = 0;

			virtual Vector2 GetRecoil() = 0;
			virtual float GetSpread() = 0;

			virtual int GetPelletSize() = 0;

			static Weapon* CreateWeapon(WeaponType index, Player& owner, const GameProperties&);

			void Restock(int ammo, int stock);
			void Restock();
			void Reset();
			void SetShooting(bool);
			void SetUnejectedBrass(bool);

			/** @return true when fired. */
			bool FrameNext(float);

			void Reload();
			/** Ends the reload; returns whether its animation was still playing out,
			 * which makes it aborted. */
			bool AbortReload();

			bool IsShooting() const { return shooting; }
			bool IsReloading() const { return reloading; }
			bool IsUnejectedBrass() const { return unejectedBrass; }
			int GetAmmo() { return ammo; }
			int GetStock() { return stock; }

			// The Tower Defense ladder's rung this gun fights at: 0 the
			// stock gun (a world the store never stood in), 1 to 3 the
			// rungs its class climbed, the stats of each read from
			// WeaponTiers.h. The client sets it from the store's word.
			void SetTier(int t) { tier = t; }
			int GetTier() const { return tier; }

			// Tower Defense's never-empty reserve: a reload answer is
			// read by the clip it grew, not by the reserve it dipped.
			void SetUnlimited(bool v) { unlimited = v; }

			// for local player
			/** Applies a WeaponReload from the server: a shell or a magazine loaded, or, when
			 * nothing was taken from the reserve, the reload ended there. */
			void ReloadDone(int ammo, int stock);

			bool IsClipFull();
			bool IsSelectable();

			float GetTimeToNextFire();
			float GetReloadProgress();

			bool IsAwaitingReloadCompletion();
			bool IsReadyToShoot();
		};
	} // namespace client
} // namespace spades