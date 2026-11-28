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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

// [SV99_Forest_Camo 1.1 by Serius 2026-11-19]

// Tower Defense dress: the mod's own view skin, adapted. The class
// is picked by the store's word (the factory in this weapon's stock
// View.as reads the tier); every file it loads is spelled with the
// dress's pak folder in front.

namespace spades {
	class ViewRifleSV99Skin : BasicViewWeapon {
		private AudioDevice@ audioDevice;

		private Model@ gunModel;
		private Model@ magazineModel;
		private Model@ bulletModel;
		private Model@ rearSightModel;
		private Model@ frontSightModel;
		private Model@ reflexSightModel;
		private Model@ leverModel;
		private Model@ lensModel;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;

		// Attachment Points
		private Vector3 magazineAttachment = Vector3(0.0F, 0.0F, 0.0F);
		private Vector3 rearSightAttachment = Vector3(0.0125F, -8.0F, -4.5F);
		private Vector3 frontSightAttachment = Vector3(0.025F, 16.75F, -4.5F);
		private Vector3 reflexSightAttachment = Vector3(0.04F, -7.825F, -5.12F);

		// Scales
		private float globalScale = 0.033F;
		private float reflexSightScale = 0.0825F;

		// A bunch of springs.
		private ViewWeaponSpring reloadPitchSpring = ViewWeaponSpring(150, 12);
		private ViewWeaponSpring reloadRollSpring = ViewWeaponSpring(150, 16);
		private ViewWeaponSpring reloadOffsetSpring = ViewWeaponSpring(150, 12);
		private ViewWeaponSpring recoilVerticalSpring = ViewWeaponSpring(200, 24);
		private ViewWeaponSpring recoilBackSpring = ViewWeaponSpring(100, 16);
		private ViewWeaponSpring recoilRotationSpring = ViewWeaponSpring(50, 8);
		private ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 10);
		private ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);

		// A bunch of events.
		private ViewWeaponEvent magazineTouched = ViewWeaponEvent();
		private ViewWeaponEvent magazineRemoved = ViewWeaponEvent();
		private ViewWeaponEvent magazineInserted = ViewWeaponEvent();
		private ViewWeaponEvent leverPulled = ViewWeaponEvent();

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;
		private float leverCycleTimer = 1.0F;

		// Muzzle flash light + smoke trail state (world space). The
		// light and each trail FREEZE their world position/direction
		// at the shot, so they stay glued to the fire point instead
		// of riding the view. The trail puffs use the stock
		// WaterExpl.png sprite; the arrays are parallel: one index =
		// one live trail
		private Image@ smokeImage;
		private Vector3 flashPos;
		private float flashTimer = 0.0F;
		private array<Vector3> trailOrigins;
		private array<Vector3> trailDirs;
		private array<float> trailDistances;
		private array<float> trailNexts;
		private array<Vector3> puffPositions;
		private array<float> puffAges;
		private array<float> puffFade;

		Matrix4 AdjustToReload(Matrix4 mat) {
			if (reloadProgress < 0.6) {
				reloadPitchSpring.desired = 0.6;
				reloadRollSpring.desired = 0.6;
				charmVerticalSwingSpring.desired = -0.01F;
			} else if (reloadProgress < 0.9) {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = -0.6;
				charmVerticalSwingSpring.desired = 0;
			} else {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = 0;
				charmVerticalSwingSpring.desired = 0;
			}

			if (magazineTouched.WasActivated()) {
				magazineTouched.Acknowledge();
				reloadPitchSpring.velocity = 4;
			}

			if (magazineRemoved.WasActivated()) {
				magazineRemoved.Acknowledge();
				reloadPitchSpring.velocity = -4;
				charmVerticalSwingSpring.velocity += 0.1F;
			}

			if (magazineInserted.WasActivated()) {
				magazineInserted.Acknowledge();
				reloadPitchSpring.velocity = 8;
				charmVerticalSwingSpring.velocity += 0.1F;
			}

			if (leverPulled.WasActivated()) {
				leverPulled.Acknowledge();
				reloadPitchSpring.velocity = 5;
				reloadOffsetSpring.velocity = 1;
				charmVerticalSwingSpring.velocity += 0.2F;
			}

			float sp = 1.0F - AimDownSightStateSmooth;

			mat *= CreateRotateMatrix(Vector3(0, 1, 0), 0.6F * reloadRollSpring.position * sp);
			mat *= CreateRotateMatrix(Vector3(-1, 0, 0), 0.25F * reloadPitchSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0.0F, -1.0F, 0.0F) * reloadOffsetSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0.0F, 0.0F, -globalScale) * reloadPitchSpring.position * sp);

			// user reload pose: extra rotation on top of the stock
			// tilt, smoothed in during the first 15% and out during
			// the last 10% of the reload
			float poseOut = (reloadProgress - 0.9F) / 0.1F;
			if (poseOut < 0.0F)
				poseOut = 0.0F;
			float poseWeight = SmoothStep(Min(1.0, reloadProgress / 0.15))
				* (1.0F - SmoothStep(Min(1.0, poseOut)));
			mat *= CreateEulerAnglesMatrix(Vector3(
				0, rad(25), rad(-5)) * poseWeight * sp);

			return mat;
		}

		Vector3 GetMagazineOffset() {
			Vector3 magazineDetachedOffset = Vector3(0, 0, 30.0F);
			Vector3 magazinePulledOffset = magazineAttachment
				+ Vector3(0, 3.0F, 3.0F);

			// reload timeline: attached until 0.15, pulled down and
			// forward by the left hand until 0.25, moving off-screen
			// until 0.35, away until 0.6, back in by 0.7, then
			// inserted
			if (reloadProgress < 0.15) {
				return magazineAttachment;
			} else if (reloadProgress < 0.25) {
				magazineRemoved.Activate();
				float per = Min(1.0, (reloadProgress-0.15) / (0.25-0.15));
				return Mix(magazineAttachment, magazinePulledOffset, SmoothStep(per));
			} else if (reloadProgress < 0.35) {
				float per = Min(1.0, (reloadProgress-0.25) / (0.35-0.25));
				return Mix(magazinePulledOffset, magazineDetachedOffset, SmoothStep(per));
			} else if (reloadProgress < 0.6F) {
				return magazineDetachedOffset;
			} else if (reloadProgress < 0.7F) {
				float per = Min(1.0, (reloadProgress-0.6F) / 0.1F);
				return Mix(magazineDetachedOffset, magazineAttachment, SmoothStep(per));
			} else {
				magazineInserted.Activate();
				return magazineAttachment;
			}
		}

		float GetMagazineRotationWeight() {
			// same schedule as GetMagazineOffset: the rotation
			// ramps in while the magazine is pulled out, holds while it
			// is off-screen and ramps back out during the return
			if (reloadProgress < 0.15) {
				return 0.0F;
			} else if (reloadProgress < 0.25) {
				float per = Min(1.0, (reloadProgress-0.15) / (0.25-0.15));
				return SmoothStep(per);
			} else if (reloadProgress < 0.6F) {
				return 1.0F;
			} else if (reloadProgress < 0.7F) {
				float per = Min(1.0, (reloadProgress-0.6F) / 0.1F);
				return 1.0F - SmoothStep(per);
			} else {
				return 0.0F;
			}
		}

		float GetLeverPullWeight() {
			// mirrors the left hand lever motion: the lever is pulled
			// back with the hand (0.82-0.87) and returns with it
			// (0.87-1.0); zero otherwise
			if (reloadProgress < 0.82) {
				return 0.0F;
			} else if (reloadProgress < 0.87) {
				float per = Min(1.0, (reloadProgress-0.82) / (0.87-0.82));
				return SmoothStep(per);
			} else if (reloadProgress < 1.0) {
				float per = Min(1.0, (reloadProgress-0.87) / (1.0-0.87));
				return 1.0F - SmoothStep(per);
			} else {
				return 0.0F;
			}
		}

		float GetLeverShotWeight() {
			// per-shot gas cycle: fast backward stroke in the first
			// 0.1s, then return over 0.3s (~0.4s total); zero once
			// the cycle is done
			float t = leverCycleTimer;
			if (t < 0.1F) {
				return SmoothStep(t / 0.1F);
			} else if (t < 0.4F) {
				return 1.0F - SmoothStep((t - 0.1F) / 0.3F);
			} else {
				return 0.0F;
			}
		}

		Vector3 GetLeftHandOffset() {
			Vector3 leftHandOffset = Vector3(1.0F, 6.0F, 1.0F) + Vector3(0.0F, 15.0F, 0.0F);
			Vector3 magazineOffset = GetMagazineOffset() + Vector3(0.0F, 0.0F, 4.0F);
			Vector3 leverOffset = Vector3(4.0F, -4.0F, -8.79F);
			Vector3 leverPullingOffset = leverOffset + Vector3(1.0F, -8.0F, 0.0F);

			if (reloadProgress < 0.1) {
				float per = Min(1.0, reloadProgress / 0.1);
				return Mix(leftHandOffset, magazineOffset, SmoothStep(per));
			} else if (reloadProgress < 0.7) {
				magazineTouched.Activate();
				return magazineOffset;
			} else if (reloadProgress < 0.8) {
				float per = Min(1.0, (reloadProgress-0.7) / (0.8-0.7));
				return Mix(magazineOffset, leverOffset, SmoothStep(per));
			} else if (reloadProgress < 0.82) {
				return leverOffset;
			} else if (reloadProgress < 0.87) {
				leverPulled.Activate();
				float per = Min(1.0, (reloadProgress-0.82) / (0.87-0.82));
				return Mix(leverOffset, leverPullingOffset, SmoothStep(per));
			} else if (reloadProgress < 1.0) {
				float per = Min(1.0, (reloadProgress-0.87) / (1.0-0.87));
				return Mix(leverPullingOffset, leftHandOffset, SmoothStep(per));
			} else {
				return leftHandOffset;
			}
		}

		Vector3 GetRightHandOffset() {
			Vector3 rightHandOffset = Vector3(0.0F, -8.0F, 2.0F) + Vector3(0.0F, -10.0F, 5.0F);

			// sprint animation
			rightHandOffset -= Vector3(0.0F, 3.0F, 0.5F) * sprintSpring.position;

			// the reload lever is worked by the left hand (see
			// GetLeftHandOffset); the right hand stays on the grip
			return rightHandOffset;
		}

		ViewRifleSV99Skin(Renderer@ r, AudioDevice@ dev) {
			super(r);
			@audioDevice = dev;

			// load models
			@gunModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@magazineModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/Magazine.kv6");
			@bulletModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/Bullet.kv6");
			@rearSightModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/SightRear.kv6");
			@frontSightModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/SightFront.kv6");
			@reflexSightModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/SightReflex.kv6");
			@leverModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/Lever.kv6");
			@lensModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/Lens.kv6");
			// replace the stock hanging charm with SC.kv6
			@charmModel = renderer.RegisterModel("WeaponMods/RifleLvl4/Models/Weapons/Rifle/SC.kv6");

			// load sounds
			@fireSound = dev.RegisterSound("WeaponMods/RifleLvl4/Sounds/Weapons/Rifle/FireLocal.opus");
			@fireFarSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("WeaponMods/RifleLvl4/Sounds/Weapons/Rifle/FireStereo.opus");
			@fireSmallReverbSound = dev.RegisterSound("WeaponMods/RifleLvl4/Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound("WeaponMods/RifleLvl4/Sounds/Weapons/Rifle/V2AmbienceLarge.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/Rifle/ReloadLocal.opus");

			// load images
			@scopeImage = renderer.RegisterImage("WeaponMods/RifleLvl4/Gfx/noscope.png");
			@smokeImage = renderer.RegisterImage("Textures/WaterExpl.png");

			// initial raise spring
			raiseSpring.position = 1;
			charmRaiseSpring.position = 1;
		}

		void Update(float dt) {
			BasicViewWeapon::Update(dt);

			reloadPitchSpring.Update(dt);
			reloadRollSpring.Update(dt);
			reloadOffsetSpring.Update(dt);
			leverCycleTimer += dt;

			// muzzle flash light: run down its short life
			if (flashTimer > 0.0F) {
				flashTimer -= dt;
				if (flashTimer < 0.0F)
					flashTimer = 0.0F;
			}

			// live smoke trails: move along their frozen shot
			// direction (captured at the shot) and drop puffs on
			// the way. Iterated BACKWARDS so removeAt keeps the
			// indices of the remaining trails valid
			for (int i = int(trailDistances.length()) - 1; i >= 0; i--) {
				Vector3 dir = trailDirs[i];
				Vector3 origin = trailOrigins[i];
				float dist = trailDistances[i] + 500 * dt;
				// spawn a puff every 0.5 units travelled, smaller
				// as the trail nears its draw limit
				float next = trailNexts[i];
				while (next <= dist and next <= 20) {
					puffPositions.insertLast(origin + dir * next);
					puffAges.insertLast(0.0F);
					puffFade.insertLast(1.0F - next / 20);
					next += 0.5F;
				}
				trailNexts[i] = next;
				trailDistances[i] = dist;
				if (dist >= 20) {
					trailOrigins.removeAt(i);
					trailDirs.removeAt(i);
					trailDistances.removeAt(i);
					trailNexts.removeAt(i);
				}
			}

			// age the smoke puffs, remove the dead ones
			for (int i = int(puffAges.length()) - 1; i >= 0; i--) {
				puffAges[i] += dt;
				if (puffAges[i] >= 0.2F) {
					puffAges.removeAt(i);
					puffPositions.removeAt(i);
					puffFade.removeAt(i);
				}
			}

			// smoothly increase recoil damping while aiming down sights
			recoilVerticalSpring.damping = Mix(16, 24, AimDownSightStateSmooth);
			recoilBackSpring.damping = Mix(12, 20, AimDownSightStateSmooth);
			recoilRotationSpring.damping = Mix(8, 16, AimDownSightStateSmooth);
			recoilVerticalSpring.Update(dt);
			recoilBackSpring.Update(dt);
			recoilRotationSpring.Update(dt);

			horizontalSwingSpring.velocity += swing.x * 60 * dt * 2;
			verticalSwingSpring.velocity += swing.z * 60 * dt * 2;
			horizontalSwingSpring.Update(dt);
			verticalSwingSpring.Update(dt);

			sprintSpring.Update(dt);
			raiseSpring.Update(dt);

			bool isSprinting = sprintState >= 1 or sprintState > lastSprintState;
			bool isRaised = raiseState >= 1 or raiseState > lastRaiseState;

			sprintSpring.desired = isSprinting ? 1 : 0;
			raiseSpring.desired = isRaised ? 0 : 1;
			charmSprintSpring.desired = isSprinting ? 1 : 0;
			charmRaiseSpring.desired = isRaised ? 0 : 1;

			lastSprintState = sprintState;
			lastRaiseState = raiseState;
		}

		void WeaponFired() {
			BasicViewWeapon::WeaponFired();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 8.0F;
				audioDevice.PlayLocal(fireSound, origin, param);

				param.volume = 8.0F * environmentRoom;
				audioDevice.PlayLocal((environmentSize < 0.5F)
					? fireSmallReverbSound : fireLargeReverbSound,
					origin, param);

				param.referenceDistance = 4.0F;
				param.volume = 1.0F;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.0F;
				audioDevice.PlayLocal(fireStereoSound, origin, param);
			}

			recoilVerticalSpring.velocity += 1.5;
			recoilBackSpring.position += 0.1;
			recoilBackSpring.velocity += 1.0;
			recoilRotationSpring.velocity += (GetRandom() * 2 - 1);

			// gas action: cycle the lever on each shot
			leverCycleTimer = 0.0F;

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;

			// muzzle flash light: freeze the world muzzle position
			// AT THE SHOT (same point the client muzzle fire uses)
			// and start the light timer. Frozen like the client
			// flash, so the light stays glued to the fire point
			// instead of riding the view
			flashPos = eyeMatrix * MuzzlePosition;
			flashTimer = 0.12F;

			// smoke tracer trail: one trail per shot, starting
			// exactly AT the muzzle point and flying along the view
			// forward captured at the shot, i.e. toward the aimed
			// point (straight even if the player turns right after
			// firing). The eye matrix origin is read with the
			// eye*(0,0,0) trick: Matrix4.GetOrigin is not exposed
			// by the engine script API
			Vector3 eyeOrigin = eyeMatrix * Vector3(0.0F, 0.0F, 0.0F);
			Matrix4 rot = CreateTranslateMatrix(-eyeOrigin) * eyeMatrix;
			Vector3 dir = rot * Vector3(0.0F, 1.0F, 0.0F);
			trailOrigins.insertLast(eyeMatrix * MuzzlePosition);
			trailDirs.insertLast(dir);
			trailDistances.insertLast(0.0F);
			trailNexts.insertLast(0.0F);
		}

		void ReloadingWeapon() {
			magazineTouched.Reset();
			magazineRemoved.Reset();
			magazineInserted.Reset();
			leverPulled.Reset();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 0.5F;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}

			charmHorizontalSwingSpring.velocity += 0.1F;
			charmVerticalSwingSpring.velocity += 0.1F;
		}

		// (override BasicViewWeapon::GetViewWeaponMatrix())
		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			float sp = 1.0F - AimDownSightStateSmooth;

			// sprint animation
			mat = CreateEulerAnglesMatrix(Vector3(0.3F, -0.1F, -0.55F) * sprintSpring.position * sp) * mat;
			mat = CreateTranslateMatrix(Vector3(0.23F, -0.05F, 0.15F) * sprintSpring.position * sp) * mat;

			// raise gun animation
			mat = CreateRotateMatrix(Vector3(0, 0, -1), 1.3F * raiseSpring.position * sp) * mat;
			mat = CreateRotateMatrix(Vector3(0, 1, 0), 0.2F * raiseSpring.position * sp) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1F, -0.3F, 0.1F) * raiseSpring.position * sp) * mat;

			// recoil animation
			Vector3 recoilRot(0.0F, 0.0F, 0.0F);
			recoilRot.x = -1.0F * recoilVerticalSpring.position;
			recoilRot.y = 0.3F * recoilRotationSpring.position;
			recoilRot.z = 0.3F * recoilRotationSpring.position;
			Vector3 recoilOffset = Vector3(0.0F, 0.0F, 0.0F);
			recoilOffset.y -= Mix(0.35F, 0.5F, sp) * recoilBackSpring.position;
			recoilOffset.z -= 0.1F * recoilVerticalSpring.position * sp;
			mat = CreateEulerAnglesMatrix(recoilRot * sp) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset);

			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);

			// The PNG scope is this dress's one and only sight: the
			// reflex setting never swaps it here, the iron-sight
			// alignment below never runs.
			bool reflexScope = false;
			// the aim alignment point still follows the front sight;
			// aimOff shifts the camera alignment only, without
			// moving the SightFront.kv6 model
			Vector3 frontSightOff = Vector3(0.4F, 18.0F, -7.0F);
			Vector3 aimOff = Vector3(0.0F, 0.0F, 5.0F);
			Vector3 sightAttachment = (reflexScope ? reflexSightAttachment
				: frontSightAttachment + frontSightOff) + aimOff;
			Vector3 offset = reflexScope ? Vector3(sightAttachment.x, 0.0F, -0.05F)
				: Vector3(sightAttachment.x, 0.0F, 0.011F);
			Vector3 sightPos = (sightAttachment - offset) * globalScale;
			float sightZ = reflexScope ? (sightPos.z + 0.0125F) : sightPos.z;

			if (cg_trueAimDownSight.BoolValue)
				trans += Vector3(Mix(-0.26F, -0.13F, sp), 0.5F * sp, Mix(0.3F, 0.2F, sp));
			else
				trans += Vector3(-0.13F * sp, 0.5F, GetZPos(-sightZ));

			// ADS stance: shift the whole weapon toward the eye with
			// the aim (world units, view space; hip stance untouched).
			// Hand anchors and the muzzle point follow the weapon
			// automatically
			Vector3 adsOff = Vector3(0.0F, -0.8F, 0.0F);
			trans += adsOff * AimDownSightStateSmooth;

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue * sp;
			trans.y += cg_viewWeaponY.FloatValue * sp;
			trans.z += cg_viewWeaponZ.FloatValue * sp;
			trans.x *= abs(weapSide);

			// add weapon sway
			trans += swing * sp;

			mat = CreateTranslateMatrix(trans) * mat;

			// twist the gun when strafing
			Vector3 swingRot(0.0F, 0.0F, 0.0F);
			swingRot.z += 2.0F * horizontalSwingSpring.position;
			swingRot.x -= 2.0F * verticalSwingSpring.position;
			mat = mat * CreateEulerAnglesMatrix(swingRot * sp);

			// reload animation
			mat = AdjustToReload(mat);

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		// IWeaponSkin3 (override BasicViewWeapon::{get_MuzzlePosition, get_CaseEjectPosition})
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 0.55F, -0.175F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * Vector3(-0.025F, -0.1F, -0.1F); } }

		void Draw2D() {
			float sw = renderer.ScreenWidth;
			float sh = renderer.ScreenHeight;

			// custom draw of the classic png scope: auto-fit the
			// source image to the screen (the stock draw scales
			// any source image by screen width / 800, which
			// overflows the screen with a large source image).
			// It draws at full aim whatever cg_pngScope says: the
			// PNG is this dress's one and only sight, there is no
			// 3D sight stance to fall back to.
			if (AimDownSightStateSmooth > 0.99F) {
				IntVector3 col;
				col.x = cg_scopeColorR.IntValue;
				col.y = cg_scopeColorG.IntValue;
				col.z = cg_scopeColorB.IntValue;
				Vector4 color = ConvertColorRGBA(col);
				color.w = Clamp(cg_scopeAlpha.IntValue, 0, 255)
					/ 255.0F;

				Vector2 imgSize = Vector2(scopeImage.Width,
					scopeImage.Height);
				imgSize *= Min(sw / imgSize.x, sh / imgSize.y);

				if (cg_scopeDynamic.BoolValue)
					imgSize *= Max(Mix(1.25F, 1.0F, readyState), 1.0F);

				Vector2 imgPos = Vector2(sw, sh) * 0.5F
					- (imgSize * 0.5F);

				renderer.ColorNP = Vector4(1.0F, 1.0F, 1.0F, color.w);
				renderer.DrawImage(scopeImage,
					AABB2(imgPos.x, imgPos.y, imgSize.x, imgSize.y));
			} else {
				BasicViewWeapon::Draw2D();
			}
		}

		void AddToScene() {
			if (AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				RightHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				return;
			}

			Matrix4 mat = GetViewWeaponMatrix()
				* CreateScaleMatrix(globalScale);

			// gun transform (WeaponNoMagazine = base model everything else
			// is attached to). The hip rotation/offset FADE OUT while
			// aiming (tuneFade): at full ADS the gun sits in its
			// authored centered pose, so the ADS stance can be tuned
			// cleanly; back at the hip the calibrated stance is fully
			// restored. Everything attached follows via gunDelta
			float tuneFade = 1.0F - AimDownSightStateSmooth;
			Matrix4 gunTune = CreateScaleMatrix(0.5F)
				* CreateTranslateMatrix(-0.5F, 0.0F, 0.0F)
				* CreateEulerAnglesMatrix(Vector3(0, rad(3), 0) * tuneFade)
				* CreateTranslateMatrix(Vector3(-2.0F, -8.0F, 3.0F) * tuneFade)
				* CreateScaleMatrix(0.45F);

			// gun transform re-expressed in weapon space, so everything
			// attached to the base model (magazine, bullets, sights, hand
			// positions) follows it rigidly. The trailing translate + 2x
			// scale is the inverse of the stock gun centering above, so
			// this is the identity matrix at default offset values.
			Matrix4 gunDelta = gunTune
				* CreateTranslateMatrix(0.5F, 0.0F, 0.0F)
				* CreateScaleMatrix(2.0F);

			Vector3 leftHand, rightHand;
			leftHand = mat * gunDelta * GetLeftHandOffset();
			rightHand = mat * gunDelta * GetRightHandOffset();

			ModelRenderParam param;
			param.depthHack = true;

			Matrix4 weapMatrix = eyeMatrix * mat;

			// draw weapon
			param.matrix = weapMatrix * gunTune;
			renderer.AddModel(gunModel, param);

			// draw magazine
			Vector3 magOffset = Vector3(0, 1, 1);
			// rotation of the magazine while it is being pulled out
			// (weight is zero outside the pull/away phase)
			Matrix4 magRot = CreateEulerAnglesMatrix(Vector3(
				rad(-25), 0, 0) * GetMagazineRotationWeight());
			param.matrix = weapMatrix * gunDelta
				* CreateTranslateMatrix(GetMagazineOffset())
				* CreateTranslateMatrix(magOffset)
				* magRot
				* CreateScaleMatrix(0.395F);
			renderer.AddModel(magazineModel, param);

			// draw bullets in the magazine (staggered column, fed
			// from the top)
			int bulletCount = Ammo;
			// once the magazine has left the screen during a reload,
			// show a full column so it comes back already loaded.
			// note: reloadProgress is 1.0 (or more) when idle, so the
			// reloading state must be checked too, otherwise the
			// column would never deplete while firing
			if (reloading && reloadProgress > 0.35F)
				bulletCount = ClipSize;
			// highest slot index = feed position (top): draw the
			// topmost bullets so the column empties from the bottom
			// (each shot feeds the top bullet, the rest move up)
			int firstBullet = 11 - bulletCount;
			if (firstBullet < 0)
				firstBullet = 0;
			Vector3 bulletBase = Vector3(-1, -1.199F, 14);
			Vector3 bulletStep = Vector3(0, 0, -1);
			Vector3 bulletStagger = Vector3(2, 0, -0.1F);
			Matrix4 bulletFlip = CreateScaleMatrix(Vector3(1.0F, -1.0F, 1.0F));
			for (int i = firstBullet; i < firstBullet + bulletCount; i++) {
				Vector3 bulletPos = bulletBase + bulletStep * i;
				if ((i % 2) == 1)
					bulletPos += bulletStagger;
				param.matrix = weapMatrix * gunDelta
					* CreateTranslateMatrix(GetMagazineOffset())
					* CreateTranslateMatrix(magOffset)
					* magRot
					* CreateTranslateMatrix(bulletPos)
					* CreateScaleMatrix(0.3F)
					* bulletFlip;
				renderer.AddModel(bulletModel, param);
			}

			// draw sights: the iron pair only - the PNG scope is the
			// dress's one and only aim, no reflex ever rides the rail
			param.matrix = weapMatrix * gunDelta
				* CreateTranslateMatrix(rearSightAttachment + Vector3(0, 2, 2))
				* CreateScaleMatrix(0.53F);
			renderer.AddModel(rearSightModel, param); // rear

			param.matrix = weapMatrix * gunDelta
				* CreateTranslateMatrix(frontSightAttachment
					+ Vector3(0.4F, 18, -7))
				* CreateScaleMatrix(0.15F);
			renderer.AddModel(frontSightModel, param); // front pin (emissive)

			// draw lever (gas action): anchored at the hand lever
			// position; cycles back on each shot (~0.4s stroke, see
			// GetLeverShotWeight) and rides the hand pull at the end
			// of the reload
			Vector3 leverBase = Vector3(4.0F, -4.0F, -8.79F);
			Vector3 leverMotion = Vector3(1.0F, -8.0F, 0.0F)
				* (GetLeverPullWeight() + 2.0F * GetLeverShotWeight());
			param.matrix = weapMatrix * gunDelta
				* CreateTranslateMatrix(leverBase + leverMotion)
				* CreateScaleMatrix(0.2F);
			renderer.AddModel(leverModel, param);

			// draw charms
			if (cg_weaponCharms.BoolValue) {
				Vector3 charmRot(0.0F, 0.0F, 0.0F);

				// add sway spring
				charmRot.x -= 50.0F * charmVerticalSwingSpring.position;	// pitch
				charmRot.y += 30.0F * charmHorizontalSwingSpring.position;	// yaw
				charmRot.z += 20.0F * charmHorizontalSwingSpring.position;	// roll

				// add sprint/raise spring
				charmRot += Vector3(-0.5F, -0.2F, -0.2F) * charmSprintSpring.position;
				charmRot += Vector3(0.7F, 0.5F, 0.1F) * charmRaiseSpring.position;

				// tilt backward/forward based on camera pitch
				Vector3 front = eyeMatrix.GetAxis(1);
				charmRot.x -= asin(front.z) * 0.85F;

				// avoid clipping into the weapon
				charmRot.y = abs(charmRot.y);

				// rainbow color
				param.customColor = HSV(time * 0.05F, 0.5F, 1.0F);

				// draw base
				param.matrix = weapMatrix * gunDelta
					* CreateTranslateMatrix(Vector3(1.95F, 0.175F, -1.8F)
						+ Vector3(1.5F, -6.5F, -3.0F))
					* CreateScaleMatrix(0.1F);
				renderer.AddModel(charmBaseModel, param);

				param.matrix = param.matrix
					* CreateTranslateMatrix(-0.75F, -1.0F, 1.25F)
					* CreateEulerAnglesMatrix(charmRot)
					* CreateEulerAnglesMatrix(Vector3(rad(90.0F), 0, rad(-90.0F)));
				renderer.AddModel(charmModel, param);
			}

			// draw scope lens: a single glass. Voxels authored
			// black take the per-draw custom colour, so author
			// the glass black and the rim any colour
			Vector3 lensFront = eyeMatrix.GetAxis(1);
			Vector3 lensBase = Vector3(0.35F, -11.79F, -9.5F);
			// base glass colour in HSV like the charm (hue is
			// 0-1, not degrees); the tint flux brightens the
			// glass when aiming up / darkens it when looking down.
			// Rainbow hue: the hue jumps by one step every 5
			// degrees of view yaw, so a full turn runs the glass
			// through the whole colour wheel
			float yawDeg = atan2(lensFront.x, lensFront.y)
				* 57.2957795F;
			float lensHue = 0.667F + floor(yawDeg / 5.0F) * 0.0139F;
			lensHue -= floor(lensHue);
			Vector3 lensColor = HSV(lensHue, 0.7F,
				0.5F - lensFront.z * 0.15F)
				* 5.0F;
			Matrix4 lensRot = CreateEulerAnglesMatrix(Vector3(
				0, 0, rad(180)));
			Matrix4 lensMat = weapMatrix * gunDelta
				* CreateTranslateMatrix(lensBase)
				* lensRot
				* CreateScaleMatrix(0.27F);

			// glass base
			param.customColor = lensColor;
			param.matrix = lensMat;
			renderer.AddModel(lensModel, param);

			// --- muzzle flash light (world) -------------------------
			// enlarged version of the client's built-in muzzle fire
			// light, with the stock warm colour (3.0, 1.6, 0.5),
			// frozen at the shot and fading to black over its life
			// instead of popping. It lights the WORLD only, never
			// the kv6 weapon models. The radius stays under the
			// ~20-unit sandbox clip box. With cg_muzzleFire on, the
			// built-in light and smoke puffs add up with this light
			if (flashTimer > 0.0F) {
				float lightFade = flashTimer / 0.12F;
				DynamicLightParam lightParam;
				lightParam.origin = flashPos;
				lightParam.radius = 8.0F;
				lightParam.color = Vector3(3.0F, 1.6F, 0.5F)
					* lightFade;
				renderer.AddLight(lightParam);
			}

			// --- smoke trail puffs (world space) -------------------
			// fake fade by radius: each puff grows during the first
			// 70% of its life, then collapses to zero radius,
			// shrinking further with the distance fade captured at
			// spawn
			if (not (smokeImage is null)) {
				for (uint i = 0; i < puffAges.length(); i++) {
					float t = puffAges[i]
						/ 0.2F;
					float radius;
					if (t < 0.7F)
						radius = 0.5F * (t / 0.7F);
					else
						radius = 0.5F
							* (1.0F - (t - 0.7F) / 0.3F);
					radius *= puffFade[i];
					renderer.AddSprite(smokeImage, puffPositions[i],
						radius, puffAges[i] * 0.6F);
				}
			}

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}

}
