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

namespace spades {
	class ThirdPersonRPGSkin : IToolSkin, IThirdPersonToolSkin, IWeaponSkin, IWeaponSkin2, IWeaponSkin3 {
		private float sprintState;
		private float raiseState;
		private Vector3 teamColor;
		private bool muted;
		private Matrix4 originMatrix;
		private float aimDownSightState;
		private float readyState;
		private bool reloading;
		private float reloadProgress;
		private int ammo, clipSize;

		private float environmentRoom;
		private float environmentSize;
		private float environmentDistance;
		private Vector3 soundOrigin;

		float SprintState { set { sprintState = value; } }
		float RaiseState { set { raiseState = value; } }
		bool IsMuted { set { muted = value; } }
		Vector3 TeamColor { set { teamColor = value; } }
		Matrix4 OriginMatrix { set { originMatrix = value; } }
		float AimDownSightState { set { aimDownSightState = value; } }
		// REQUIRED by IThirdPersonToolSkin (fix 17 nov. 2026): the
		// stock rifle skin defines it. Without this getter the script
		// FAILS TO COMPILE and the whole third-person weapon is
		// silently missing, whatever the scale is set to.
		float PitchBias { get { return 0.0F; } }

		bool IsReloading { set { reloading = value; } }
		float ReloadProgress { set { reloadProgress = value; } }
		int Ammo { set { ammo = value; } }
		int ClipSize { set { clipSize = value; } }
		float ReadyState { set { readyState = value; } }

		// IWeaponSkin2
		void SetSoundEnvironment(float room, float size, float distance) {
			environmentRoom = room;
			environmentSize = size;
			environmentDistance = distance;
		}
		Vector3 SoundOrigin { set { soundOrigin = value; } }

		private Renderer@ renderer;
		private AudioDevice@ audioDevice;
		private Model@ model;
		private Model@ rocketModel;
		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;

		// ---- Main settings --------------------------------------------------

		// 3RD-PERSON WEAPON SCALE (the knob to adjust the RPG size).
		// Scales the weapon body ONLY (there are no hand anchors in
		// third person: the arms are drawn by the engine's player
		// model). World-space offsets do NOT follow it:
		//   - `trans` below (0.4, -0.9, 0) is in world units
		//   - `rocketSeat` is in world units: after a scale change,
		//     nudge it so the loaded rocket stays at the (new) muzzle
		// The loaded rocket size is set independently by `rocketScale`.
		private float thirdPersonScale = 0.02F;

		// Seat of the loaded rocket at the muzzle (world units,
		// relative to originMatrix).
		private Vector3 rocketSeat = Vector3(0.4F, -0.4F, -0.1F);

		// world units per model unit, INDEPENDENT of thirdPersonScale
		private float rocketScale = 0.05F;

		// NOTE: the flying rocket, its smoke trail and its exhaust
		// flame are handled ENGINE-SIDE by RocketEntity (fork). The
		// skin only shows the loaded rocket at the muzzle.

		// ---- RPG state ------------------------------------------------------

		private bool rocketLoaded = true;

		// --------------------------------------------------------------------

		ThirdPersonRPGSkin(Renderer@ r, AudioDevice@ dev) {
			@renderer = r;
			@audioDevice = dev;
			@model = renderer.RegisterModel("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@rocketModel = renderer.RegisterModel("Models/Weapons/Rifle/Rocket.kv6");
			@fireSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireLocal.opus");
			@fireFarSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireStereo.opus");
			@fireSmallReverbSound = dev.RegisterSound("Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound("Sounds/Weapons/Rifle/V2AmbienceLarge.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/Rifle/Reload.opus");
		}

		void Update(float dt) {
			// (the flying rocket is handled engine-side by RocketEntity)
		}

		void WeaponFired() {
			// the visible projectile itself is spawned engine-side
			// (RocketEntity); the skin just hides the barrel rocket
			rocketLoaded = false;

			if (!muted) {
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 20.0F;
				audioDevice.Play(fireSound, origin, param);

				param.volume = 20.0F * environmentRoom;
				audioDevice.Play((environmentSize < 0.5F)
					? fireSmallReverbSound : fireLargeReverbSound, origin, param);

				param.volume = 1.0F;
				param.referenceDistance = 26.0F;
				audioDevice.Play(fireFarSound, origin, param);
				param.referenceDistance = 4.0F;
				audioDevice.Play(fireStereoSound, origin, param);
			}
		}

		void ReloadingWeapon() {
			if (!muted) {
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 0.2F;
				audioDevice.Play(reloadSound, origin, param);
			}
		}

		void ReloadedWeapon() {}

		// IWeaponSkin3
		Vector3 MuzzlePosition { get { return originMatrix * rocketSeat; } }
		Vector3 CaseEjectPosition { get { return originMatrix * rocketSeat; } }

		void AddToScene() {
			ModelRenderParam param;
			param.customColor = teamColor;

			// draw weapon body
			Matrix4 mat = CreateScaleMatrix(thirdPersonScale);
			// NO flip (fix 17 nov. 2026): WeaponNoMagazine.kv6 is
			// authored BUTT toward +Y, i.e. muzzle -Y, which is
			// already the third-person forward direction (stock
			// MuzzlePosition = originMatrix * (0.35, -1.85, -0.125):
			// forward = -Y). The stock rifle flip
			// CreateScaleMatrix(-1, -1, 1) turned this model 180 deg
			// and made the RPG point BACKWARD: it exists in the stock
			// skin only because Weapon.kv6 is authored muzzle +Y,
			// the opposite of ours.
			// Position tuning knobs (fix 17 nov. 2026, in WORLD units):
			//   x : lateral (0.4 = same as the stock rifle)
			//   y : forward/back - forward is -Y, so BACK toward the
			//       player = INCREASE y (was -0.9)
			//   z : up/down - UP is -Z, so RAISE the weapon =
			//       DECREASE z (was 0.0)
			Vector3 trans = Vector3(0.4F, -0.4F, -0.1F);
			mat = CreateTranslateMatrix(trans) * mat;

			param.matrix = originMatrix * mat;
			renderer.AddModel(model, param);

			// the rocket appears at the muzzle once the reload seats it
			if (reloading and reloadProgress >= 0.6F)
				rocketLoaded = true;

			if (rocketLoaded) {
				param.matrix = originMatrix
					* CreateTranslateMatrix(rocketSeat)
					// rocketScale is WORLD units per model unit and
					// rocketSeat is in world units too: NO compensation
					// by thirdPersonScale (the old
					// "rocketScale / thirdPersonScale" made the rocket
					// gigantic for any weapon scale != 1)
					* CreateScaleMatrix(rocketScale);
				renderer.AddModel(rocketModel, param);
			}

			// (the flying rocket, the flame and the smoke are drawn
			//  engine-side by RocketEntity)
		}
	}

	// IMPORTANT: the RPG replaces the RIFLE slot. The engine calls the
	// factory "CreateThirdPersonRifleSkin" (name hardcoded in
	// ClientPlayer.cpp). Pak file: Scripts/Skin/Rifle/ThirdPerson.as
	IWeaponSkin@ CreateThirdPersonRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ThirdPersonRPGSkin(r, dev);
	}
}