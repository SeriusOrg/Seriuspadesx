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
	class ThirdPersonMinigunSkin : IToolSkin, IThirdPersonToolSkin, IWeaponSkin, IWeaponSkin2, IWeaponSkin3 {
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
		// A VR player's physical reload has the magazine out (reload progress
		// below zero, which the game itself never gives).
		private Model@ modelNoMagazine;
		// Spinning barrels: reuse the first-person pair (WeaponNoMagazine.kv6
		// body + Barrels.kv6, both double resolution) drawn at half scale.
		private Model@ barrelModel;
		private AudioChunk@[] fireSounds(3);
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;
		private AudioChunk@ spinTickSound;

		// ---- Réglages principaux ------------------------------------------

		// Échelle : 75% de l'échelle standard (comme View.as).
		// Le +0.001 évite le z-fighting avec les mains du joueur.
		private float thirdPersonScale = 0.058F + 0.001F;

		// ---- Spinning barrels ----------------------------------------------
		// Cheap animation: each shot pumps the spin speed up, Update decays
		// it. A few float ops per frame, nothing more. The barrels are
		// always drawn from the same pair of models, spinning or not.
		private float barrelSpinMax = 25.0F;	// rad/s
		private float barrelSpinAccel = 6.0F;	// rad/s per shot
		private float barrelSpinDown = 8.0F;	// rad/s^2 decel
		private float barrelSpin = 0.0F;
		private float barrelSpinVel = 0.0F;

		// Barrel assembly position on the body, in the same model units as
		// View.as (both models are the first-person double-resolution ones).
		// The half-scale draw below aligns them with the third-person world.
		private Vector3 barrelAttachment = Vector3(0.0F, -2.0F, 9.5F);
		// Offset from the Barrels.kv6 pivot to the true barrel axis
		// (same value as View.as). Tune if barrels wobble.
		private Vector3 barrelPivotOffset = Vector3(-0.5F, 0.0F, -0.5F);

		// ---- Spin tick audio (close range only) ---------------------------
		// environmentDistance is the distance from this player's sound
		// to the local camera: only players closer than the threshold
		// emit the barrel rotation noise.
		private bool drawSpinTick = true;
		private float spinTickMaxDistance = 6.0F;	// world units
		private float spinTickVolume = 1.0F;
		private float spinTickMinSpeed = 1.0F;	// rad/s; below: no ticking
		private float spinTicksPerRevolution = 2.0F;
		private float spinTickTimer = 0.0F;

		// --------------------------------------------------------------------

		ThirdPersonMinigunSkin(Renderer@ r, AudioDevice@ dev) {
			@renderer = r;
			@audioDevice = dev;
			@modelNoMagazine = renderer.RegisterModel("Models/Weapons/SMG/WeaponNoMagazine.kv6");
			@barrelModel = renderer.RegisterModel("Models/Weapons/SMG/Barrels.kv6");
			@fireSounds[0] = dev.RegisterSound("Sounds/Weapons/SMG/V2Local1.opus");
			@fireSounds[1] = dev.RegisterSound("Sounds/Weapons/SMG/V2Local2.opus");
			@fireSounds[2] = dev.RegisterSound("Sounds/Weapons/SMG/V2Local3.opus");
			@fireFarSound = dev.RegisterSound("Sounds/Weapons/SMG/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("Sounds/Weapons/SMG/FireStereo.opus");
			@fireSmallReverbSound = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall1.opus");
			@fireLargeReverbSound = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge1.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/SMG/Reload.opus");
			@spinTickSound = dev.RegisterSound("Sounds/Weapons/SMG/SpinTick.opus");
		}

		void Update(float dt) {
			// barrel spin: pumped by shots, decays afterwards
			barrelSpin += barrelSpinVel * dt;
			barrelSpinVel = Max(0.0F, barrelSpinVel - barrelSpinDown * dt);

			// avoid float precision loss on long sessions
			float fullTurn = PiF * 2.0F;
			if (barrelSpin > fullTurn * 100.0F)
				barrelSpin -= fullTurn * 100.0F;

			// barrel rotation noise, only for nearby players
			if (drawSpinTick
				and barrelSpinVel > spinTickMinSpeed
				and environmentDistance < spinTickMaxDistance) {
				spinTickTimer -= dt;
				if (spinTickTimer <= 0.0F) {
					if (!muted) {
						AudioParam param;
						param.volume = spinTickVolume;
						audioDevice.Play(spinTickSound, soundOrigin, param);
					}
					spinTickTimer += (PiF * 2.0F)
						/ (barrelSpinVel * spinTicksPerRevolution);
				}
			} else {
				spinTickTimer = 0.0F;
			}
		}

		void WeaponFired() {
			// each shot spins the barrels up
			barrelSpinVel = Min(barrelSpinVel + barrelSpinAccel, barrelSpinMax);

			if (!muted) {
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 20.0F;
				audioDevice.Play(fireSounds[GetRandom(fireSounds.length)], origin, param);

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
		Vector3 MuzzlePosition { get { return originMatrix * (Vector3(0.4F, -0.9F, 0.0F) - Vector3(0.0F, 0.85F, 0.125F)); } }
		Vector3 CaseEjectPosition { get { return originMatrix * (Vector3(0.4F, -0.9F, 0.0F) - Vector3(0.0F, -0.25F, 0.125F)); } }

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(thirdPersonScale);
			mat = mat * CreateScaleMatrix(-1, -1, 1);

			Vector3 trans = Vector3(0.4F, -0.9F, 0.0F);
			mat = CreateTranslateMatrix(trans) * mat;

			ModelRenderParam param;
			param.matrix = originMatrix * mat;
			param.customColor = teamColor;

			// The first-person pair (WeaponNoMagazine + Barrels) is always
			// used, spinning or not, so the model never pops between
			// renders of different resolutions. Both are drawn at half
			// scale (doubled resolution, doubled pivot: lines up voxel for
			// voxel with the third-person world).
			Matrix4 half = param.matrix * CreateScaleMatrix(0.5F);
			param.matrix = half;
			renderer.AddModel(modelNoMagazine, param);

			// spinning barrels: same half-scale space, same attachment values
			// as View.as. T(attach) * T(pivot) * R * T(-pivot)
			Matrix4 barrelMat = half
				* CreateTranslateMatrix(barrelAttachment)
				* CreateTranslateMatrix(barrelPivotOffset)
				* CreateRotateMatrix(Vector3(0, 1, 0), barrelSpin)
				* CreateTranslateMatrix(-barrelPivotOffset);
			param.matrix = barrelMat;
			renderer.AddModel(barrelModel, param);
		}
	}

	// IMPORTANT : le minigun remplace le slot SMG. Le moteur appelle
	// la fabrique "CreateThirdPersonSMGSkin" (nom codé en dur dans
	// ClientPlayer.cpp). Fichier pak : Scripts/Skin/SMG/ThirdPerson.as
	IWeaponSkin@ CreateThirdPersonSMGSkin(Renderer@ r, AudioDevice@ dev) {
		return ThirdPersonMinigunSkin(r, dev);
	}
}