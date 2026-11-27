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

#include "Weapon.h"
#include "GameProperties.h"
#include "IWorldListener.h"
#include "World.h"
#include <Training/WeaponTiers.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace client {
		Weapon::Weapon(World& w, Player& p)
		    : world(w),
		      owner(p),
		      time(0.0F),
		      shooting(false),
		      shootingPreviously(false),
		      reloading(false),
		      unejectedBrass(false),
		      nextShotTime(0.0F),
		      reloadStartTime(-101.0F),
		      reloadEndTime(-100.0F),
		      unconfirmedShells(0),
		      lastReloadActivityTime(0.0F),
		      lastDryFire(false),
		      tier(0),
		      unlimited(false) {
			SPADES_MARK_FUNCTION();
		}

		Weapon::~Weapon() { SPADES_MARK_FUNCTION(); }

		void Weapon::Restock(int ammo, int stock) {
			this->ammo = ammo;
			this->stock = stock;
		}
		void Weapon::Restock() { stock = GetMaxStock(); }

		void Weapon::Reset() {
			SPADES_MARK_FUNCTION();
			shooting = false;
			reloading = false;
			unconfirmedShells = 0;
			ammo = GetClipSize();
			stock = GetMaxStock();
			time = 0.0F;
			nextShotTime = 0.0F;
		}

		void Weapon::SetShooting(bool b) { shooting = b; }
		void Weapon::SetUnejectedBrass(bool b) { unejectedBrass = b; }

		bool Weapon::IsClipFull() { return ammo >= GetClipSize(); }
		bool Weapon::IsSelectable() { return ammo > 0 || stock > 0; }

		float Weapon::GetTimeToNextFire() {
			return nextShotTime - time;
		}

		float Weapon::GetReloadProgress() {
			if (reloadEndTime <= reloadStartTime)
				return 1.0F; // cancelled
			return (time - reloadStartTime) / (reloadEndTime - reloadStartTime);
		}

		bool Weapon::IsAwaitingReloadCompletion() {
			return reloading || GetReloadProgress() < 1.0F;
		}

		bool Weapon::IsReadyToShoot() {
			return time >= nextShotTime
				&& (ammo > 0 || !owner.IsLocalPlayer())
				&& (!reloading || IsReloadSlow());
		}

		bool Weapon::FrameNext(float dt) {
			SPADES_MARK_FUNCTION();

			bool ownerIsLocalPlayer = owner.IsLocalPlayer();

			bool fired = false;
			bool dryFire = false;

			if (shooting && (!reloading || IsReloadSlow())) {
				// A shot interrupts a slow reload, as on the servers.
				if (reloading)
					AbortReload();

				if (!shootingPreviously)
					nextShotTime = std::max(nextShotTime, time);

				// Automatic operation of weapon.
				if (time >= nextShotTime && (ammo > 0 || !ownerIsLocalPlayer)) {
					fired = true;
					unejectedBrass = true;

					// Consume an ammo.
					if (ammo > 0)
						ammo--;

					if (world.GetListener())
						world.GetListener()->PlayerFiredWeapon(owner);
					nextShotTime += GetDelay();
					ejectBrassTime = time + GetEjectBrassTime();
				} else if (time >= nextShotTime) {
					dryFire = true;
				}

				shootingPreviously = true;
			} else {
				shootingPreviously = false;
			}

			if (reloading) {
				if (ownerIsLocalPlayer) {
					UpdateLocalReload();
				} else if (time >= reloadEndTime) {
					// A reload was completed.
					//
					// For remote players, the server does not send any information regarding the
					// number of bullets/shells loaded or held as stock. This is problematic for
					// slow-loading weapons because we can't tell how many times the reloading
					// animation has to be repeated. For now, we just assume a remote player has
					// an infinite supply of ammo, but a limited number of bullets in a clip.
					reloading = false;
					ammo = GetClipSize();
					if (world.GetListener())
						world.GetListener()->PlayerReloadedWeapon(owner);
				}
			} else if (unejectedBrass && time >= ejectBrassTime) {
				unejectedBrass = false;
				if (world.GetListener())
					world.GetListener()->PlayerEjectedBrass(owner);
			}

			time += dt;

			if (world.GetListener() && dryFire && !lastDryFire)
				world.GetListener()->PlayerDryFiredWeapon(owner);

			lastDryFire = dryFire;

			return fired;
		}

		// The server is the only one to change ammo on a reload. The client plays the reload
		// meanwhile:
		//
		// - a magazine is one animation, then a wait for the server's WeaponReload;
		// - shells are animated back to back, as long as the shells not yet confirmed would
		//   not overfill the magazine or empty the reserve. Each WeaponReload confirms shells
		//   already animated, so it never cuts the shell playing now. Shells are only
		//   animated with the gun in hand; a server that keeps loading through a tool switch
		//   (piqueserver) still has its ammo applied;
		// - a WeaponReload taking nothing from the reserve is the server ending the reload
		//   without loading (lsd on a tool switch): the reload is aborted;
		// - a reload the server never answers is given up on after a while, see
		//   GiveUpWaitingForServer.

		// Seconds without an animation or a WeaponReload after which the server is deemed to
		// have dropped the reload.
		static constexpr float kReloadAnswerTimeout = 5.0F;

		void Weapon::ReloadDone(int ammo, int stock) {
			SPADES_MARK_FUNCTION_DEBUG();

			// The answer to a reload already over here (a shot, a death, a timeout) only sets the
			// ammo; it never starts a reload again.
			if (!reloading) {
				Restock(ammo, stock);
				return;
			}

			lastReloadActivityTime = time;

			// A never-empty reserve is topped back up behind every answer,
			// so the difference between two of its counts says nothing:
			// the rounds loaded are the clip the answer grew by.
			const int loaded =
			    unlimited ? std::max(0, ammo - this->ammo) : this->stock - stock;
			Restock(ammo, stock);

			if (loaded <= 0) {
				AbortReload();
				return;
			}

			if (IsReloadSlow()) {
				unconfirmedShells = std::max(0, unconfirmedShells - loaded);

				// More shells to come: the one playing now goes on.
				if (!IsClipFull() && this->stock > 0)
					return;
			}

			FinishReload();
		}

		void Weapon::StartReloadAnimation() {
			reloadStartTime = time;
			reloadEndTime = time + GetReloadTime();
			lastReloadActivityTime = time;

			if (world.GetListener())
				world.GetListener()->PlayerReloadingWeapon(owner);
		}

		bool Weapon::NeedsAnotherShell() {
			return ammo + unconfirmedShells < GetClipSize() && unconfirmedShells < stock;
		}

		void Weapon::UpdateLocalReload() {
			if (time < reloadEndTime) {
				lastReloadActivityTime = time;
				return;
			}

			if (IsReloadSlow() && owner.IsToolWeapon() && NeedsAnotherShell()) {
				unconfirmedShells++;
				StartReloadAnimation();
				return;
			}

			// Waiting for the server.
			if (time - lastReloadActivityTime > kReloadAnswerTimeout)
				GiveUpWaitingForServer();
		}

		void Weapon::FinishReload() {
			// A shell the server confirmed before its animation ended here is loaded all the same.
			reloading = false;
			unconfirmedShells = 0;
			reloadEndTime = std::min(reloadEndTime, time);

			if (world.GetListener())
				world.GetListener()->PlayerReloadedWeapon(owner);
		}

		void Weapon::GiveUpWaitingForServer() {
			// A server may drop a reload without a word (lsd before 093109a, on a tool switch).
			// A magazine is then loaded here, as servers used to agree with; shells are not, the
			// server having already sent the count of those it loaded.
			if (IsReloadSlow()) {
				AbortReload();
				return;
			}

			int newStock = std::max(0, stock - GetClipSize() + ammo);
			ammo += stock - newStock;
			stock = newStock;
			FinishReload();
		}

		void Weapon::Reload() {
			SPADES_MARK_FUNCTION();

			if (reloading)
				return;

			bool ownerIsLocalPlayer = owner.IsLocalPlayer();
			if (ownerIsLocalPlayer) {
				if (IsClipFull())
					return;
				if (stock == 0)
					return;
				if (IsReloadSlow() && shooting && ammo > 0)
					return;
			}

			reloading = true;
			shooting = false;
			unconfirmedShells = IsReloadSlow() ? 1 : 0;
			StartReloadAnimation();
		}

		bool Weapon::AbortReload() {
			SPADES_MARK_FUNCTION_DEBUG();

			// Only a reload still playing out is aborted; one whose animation is over
			// is merely waiting for the server.
			float progress = GetReloadProgress();
			bool inProgress = progress < 1.0F;

			// End the animation too: a cancelled reload no longer blocks firing or reloading
			reloading = false;
			unconfirmedShells = 0;
			reloadEndTime = std::min(reloadEndTime, time);

			if (inProgress && world.GetListener())
				world.GetListener()->PlayerAbortedReload(owner, progress);

			return inProgress;
		}

		// The 0.75 weapons: their numbers are the Tower Defense ladder's
		// table, read at the rung the client set (rung 0 for the stock
		// gun, a world the store never stood in) - the same table the
		// server pays its damage out of, so the aim, the reload and
		// the HUD can never disagree with the hits that land.
		class RifleWeapon3 : public Weapon {
			const training::tiers::GunSpec& spec() const {
				return training::tiers::kGuns[0][GetTier()];
			}
		public:
			RifleWeapon3(World& w, Player& p) : Weapon(w, p) {}
			std::string GetName() override { return "Rifle"; }
			float GetDelay() override { return spec().delay; }
			int GetClipSize() override { return spec().clip; }
			int GetMaxStock() override { return spec().stock; }
			float GetReloadTime() override { return spec().reload_time; }
			float GetEjectBrassTime() override { return 0.0F; }
			bool IsReloadSlow() override { return spec().slow; }
			WeaponType GetWeaponType() override { return RIFLE_WEAPON; }
			int GetDamage(HitType type) override {
				const int* d = spec().damage;
				switch (type) {
					case HitTypeTorso: return d[0];
					case HitTypeHead: return d[1];
					case HitTypeArms: return d[2];
					case HitTypeLegs: return d[3];
					case HitTypeBlock: return 50;
					default: SPAssert(false); return 0;
				}
			}
			Vector2 GetRecoil() override {
				const float m = spec().recoil;
				return MakeVector2(0.0001F * m, 0.05F * m);
			}
			float GetSpread() override { return 0.006F; }
			int GetPelletSize() override { return 1; }
		};

		class SMGWeapon3 : public Weapon {
			const training::tiers::GunSpec& spec() const {
				return training::tiers::kGuns[1][GetTier()];
			}
		public:
			SMGWeapon3(World& w, Player& p) : Weapon(w, p) {}
			std::string GetName() override { return "SMG"; }
			float GetDelay() override { return spec().delay; }
			int GetClipSize() override { return spec().clip; }
			int GetMaxStock() override { return spec().stock; }
			float GetReloadTime() override { return spec().reload_time; }
			float GetEjectBrassTime() override { return 0.0F; }
			bool IsReloadSlow() override { return spec().slow; }
			WeaponType GetWeaponType() override { return SMG_WEAPON; }
			int GetDamage(HitType type) override {
				const int* d = spec().damage;
				switch (type) {
					case HitTypeTorso: return d[0];
					case HitTypeHead: return d[1];
					case HitTypeArms: return d[2];
					case HitTypeLegs: return d[3];
					case HitTypeBlock: return 34;
					default: SPAssert(false); return 0;
				}
			}
			Vector2 GetRecoil() override {
				const float m = spec().recoil;
				return MakeVector2(0.00005F * m, 0.0125F * m);
			}
			float GetSpread() override { return 0.012F; }
			int GetPelletSize() override { return 1; }
		};

		class ShotgunWeapon3 : public Weapon {
			const training::tiers::GunSpec& spec() const {
				return training::tiers::kGuns[2][GetTier()];
			}
		public:
			ShotgunWeapon3(World& w, Player& p) : Weapon(w, p) {}
			std::string GetName() override { return "Shotgun"; }
			float GetDelay() override { return spec().delay; }
			int GetClipSize() override { return spec().clip; }
			int GetMaxStock() override { return spec().stock; }
			float GetReloadTime() override { return spec().reload_time; }
			float GetEjectBrassTime() override { return 0.6F; }
			bool IsReloadSlow() override { return spec().slow; }
			WeaponType GetWeaponType() override { return SHOTGUN_WEAPON; }
			int GetDamage(HitType type) override {
				const int* d = spec().damage;
				switch (type) {
					case HitTypeTorso: return d[0];
					case HitTypeHead: return d[1];
					case HitTypeArms: return d[2];
					case HitTypeLegs: return d[3];
					// Actually, you cast a hit per pellet.
					// --GM
					case HitTypeBlock: return 34;
					default: SPAssert(false); return 0;
				}
			}
			Vector2 GetRecoil() override {
				const float m = spec().recoil;
				return MakeVector2(0.0002F * m, 0.1F * m);
			}
			float GetSpread() override { return 0.024F; }
			int GetPelletSize() override { return 8; }
		};

		class RifleWeapon4 : public Weapon {
		public:
			RifleWeapon4(World& w, Player& p) : Weapon(w, p) {}
			std::string GetName() override { return "Rifle"; }
			float GetDelay() override { return 0.6F; }
			int GetClipSize() override { return 8; }
			int GetMaxStock() override { return 48; }
			float GetReloadTime() override { return 2.5F; }
			float GetEjectBrassTime() override { return 0.0F; }
			bool IsReloadSlow() override { return false; }
			WeaponType GetWeaponType() override { return RIFLE_WEAPON; }
			int GetDamage(HitType type) override {
				switch (type) {
					// These are the 0.75 damage values.
					// To be honest, we don't need this information, as the server decides the
					// damage.
					// EXCEPT for blocks, that is.
					// --GM
					case HitTypeTorso: return 60;
					case HitTypeHead: return 250;
					case HitTypeArms: return 50;
					case HitTypeLegs: return 50;
					case HitTypeBlock: return 50;
					default: SPAssert(false); return 0;
				}
			}
			Vector2 GetRecoil() override { return MakeVector2(0.0002F, 0.075F); }
			float GetSpread() override { return 0.004F; }
			int GetPelletSize() override { return 1; }
		};

		class SMGWeapon4 : public Weapon {
		public:
			SMGWeapon4(World& w, Player& p) : Weapon(w, p) {}
			std::string GetName() override { return "SMG"; }
			float GetDelay() override { return 0.1F; }
			int GetClipSize() override { return 30; }
			int GetMaxStock() override { return 150; }
			float GetReloadTime() override { return 2.5F; }
			float GetEjectBrassTime() override { return 0.0F; }
			bool IsReloadSlow() override { return false; }
			WeaponType GetWeaponType() override { return SMG_WEAPON; }
			int GetDamage(HitType type) override {
				switch (type) {
					case HitTypeTorso: return 40;
					case HitTypeHead: return 60;
					case HitTypeArms: return 20;
					case HitTypeLegs: return 20;
					case HitTypeBlock: return 26;
					default: SPAssert(false); return 0;
				}
			}
			Vector2 GetRecoil() override { return MakeVector2(0.00005F, 0.0125F); }
			float GetSpread() override { return 0.012F; }
			int GetPelletSize() override { return 1; }
		};

		class ShotgunWeapon4 : public Weapon {
		public:
			ShotgunWeapon4(World& w, Player& p) : Weapon(w, p) {}
			std::string GetName() override { return "Shotgun"; }
			float GetDelay() override { return 0.8F; }
			int GetClipSize() override { return 8; }
			int GetMaxStock() override { return 48; }
			float GetReloadTime() override { return 0.4F; }
			float GetEjectBrassTime() override { return 0.6F; }
			bool IsReloadSlow() override { return true; }
			WeaponType GetWeaponType() override { return SHOTGUN_WEAPON; }
			int GetDamage(HitType type) override {
				switch (type) {
					case HitTypeTorso: return 40;
					case HitTypeHead: return 60;
					case HitTypeArms: return 20;
					case HitTypeLegs: return 20;
					case HitTypeBlock: return 34;
					default: SPAssert(false); return 0;
				}
			}
			Vector2 GetRecoil() override { return MakeVector2(0.0002F, 0.075F); }
			float GetSpread() override { return 0.036F; }
			int GetPelletSize() override { return 8; }
		};

		Weapon* Weapon::CreateWeapon(WeaponType type, Player& p, const GameProperties& gp) {
			SPADES_MARK_FUNCTION();

			switch (gp.protocolVersion) {
				case ProtocolVersion::v075:
					switch (type) {
						case RIFLE_WEAPON: return new RifleWeapon3(p.GetWorld(), p);
						case SMG_WEAPON: return new SMGWeapon3(p.GetWorld(), p);
						case SHOTGUN_WEAPON: return new ShotgunWeapon3(p.GetWorld(), p);
						default: SPInvalidEnum("type", type);
					}
				case ProtocolVersion::v076:
					switch (type) {
						case RIFLE_WEAPON: return new RifleWeapon4(p.GetWorld(), p);
						case SMG_WEAPON: return new SMGWeapon4(p.GetWorld(), p);
						case SHOTGUN_WEAPON: return new ShotgunWeapon4(p.GetWorld(), p);
						default: SPInvalidEnum("type", type);
					}
				default: SPInvalidEnum("protocolVersion", gp.protocolVersion);
			}
		}
	} // namespace client
} // namespace spades