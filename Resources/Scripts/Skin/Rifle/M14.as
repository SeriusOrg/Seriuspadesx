/*
 Copyright (c) 2013 yvt
 Modified by Paratrooper

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

 namespace spades {
	class ViewRifleM14Skin : BasicViewWeapon {
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ magazineModel;
		private Model@ chargingHandleModel;
		private Model@ rearSightModel;
		private Model@ frontSightModel;

		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;

		// Constants
		// Pivots
		// Weapon
		private Vector3 pivot = Vector3(1.5, 30.0, 6.0);
		// Magazine
		private Vector3 magazinePivot = Vector3(1.5, 5.5, -0.5);
		// Charging Handle
		private Vector3 chargingHandlePivot = Vector3(9.5, -0.5, 3.5);

		// Attachment Points
		// Magazine
		private Vector3 magazineAttachment = Vector3(1.5, 42.5, 6.0);
		// Rear Sights
		private Vector3 rearSightAttachment = Vector3(1.5, 28.6, 0.4);
		// Front Sights
		private Vector3 frontSightAttachment = Vector3(1.5, 94.0, -0.5);
		// Charging Handle
		private Vector3 chargingHandleAttachment = Vector3(1.5, 38.5, 1.5);

		// Scale
		// Weapon and global scale multiplier.
		private float globalScale = 0.015;
		// Magazine
		private float magazineScale = 0.5;
		// Rear Sights
		private float rearSightScale = 0.25;
		// Front Sights
		private float frontSightScale = 0.125;
		// Charging Handle
		// Slightly less than 0.5 to avoid z-fighting with the body.
		private float chargingHandleScale = 0.499;

		// A bunch of springs.
		private ViewWeaponSpring recoilVerticalSpring = ViewWeaponSpring(200, 24);
		private ViewWeaponSpring recoilBackSpring = ViewWeaponSpring(100, 16);
		private ViewWeaponSpring recoilRotationSpring = ViewWeaponSpring(50, 8);
		private ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring reloadPitchSpring = ViewWeaponSpring(150, 12, 0);
		private ViewWeaponSpring reloadRollSpring = ViewWeaponSpring(150, 16, 0);
		private ViewWeaponSpring reloadOffsetSpring = ViewWeaponSpring(150, 12, 0);
		private ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 10, 0);
		private ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);
		private Vector3 swingFromSpring = Vector3();

		// A bunch of events.
		private ViewWeaponEvent magazineTouched = ViewWeaponEvent();
		private ViewWeaponEvent magazineRemoved = ViewWeaponEvent();
		private ViewWeaponEvent magazineInserted = ViewWeaponEvent();
		private ViewWeaponEvent chargingHandlePulled = ViewWeaponEvent();

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		// I need trueReloadProgress because reloadProgress
		// is choppy.
		private double trueReloadProgress = 2.5;

		Matrix4 AdjustToReload(Matrix4 mat) {
			if (trueReloadProgress < 0.60) {
				reloadPitchSpring.desired = 0.6;
				reloadRollSpring.desired = 0.6;
				charmVerticalSwingSpring.desired = -0.01F;
			} else if (trueReloadProgress < 0.90) {
				reloadPitchSpring.desired = 0.0;
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

			if (chargingHandlePulled.WasActivated()) {
				chargingHandlePulled.Acknowledge();
				reloadPitchSpring.velocity = 5;
				reloadOffsetSpring.velocity = 2;
				charmVerticalSwingSpring.velocity += 0.2F;
			}

			mat *= CreateEulerAnglesMatrix( Vector3(0.0, 0.6, 00) * reloadRollSpring.position );
			mat *= CreateEulerAnglesMatrix( Vector3(-1, 0.0, 0.0) * reloadPitchSpring.position );
			mat *= CreateTranslateMatrix( Vector3(0.0, -1, 0) * reloadOffsetSpring.position );

			return mat;
		}

		Vector3 GetMagazineOffset() {
			if (trueReloadProgress < 0.20) {
				return magazineAttachment - pivot;
			} else if (trueReloadProgress < 0.25) {
				magazineRemoved.Activate();
				return Mix(
					magazineAttachment - pivot,
					magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0),
					SmoothStep(Min(1.0, (trueReloadProgress-0.20) / 0.05))
				);
			} else if (trueReloadProgress < 0.40) {
				return magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0);
			} else if (trueReloadProgress < 0.50) {
				return Mix(
					magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0),
					magazineAttachment - pivot,
					SmoothStep(Min(1.0, (trueReloadProgress-0.4) / 0.1))
				);
			} else {
				magazineInserted.Activate();
				return magazineAttachment - pivot;
			}
		}

		Vector3 GetLeftHandOffset() {
			if (trueReloadProgress < 0.10) {
				return Mix(
					Vector3(4.0, 55.0, 9.0)-pivot,
					GetMagazineOffset() + Vector3(0, 0, 15.0),
					SmoothStep(Min(1.0, (trueReloadProgress) / 0.10))
				);
			} else if (trueReloadProgress < 0.60) {
				magazineTouched.Activate();
				return GetMagazineOffset() + Vector3(0, 0, 15.0);
			} else if (trueReloadProgress < 1.0) {
				return Mix(
					GetMagazineOffset() + Vector3(0, 0, 15.0),
					Vector3(4.0, 55.0, 9.0)-pivot,
					SmoothStep(Min(1.0, (trueReloadProgress-0.60) / 0.10))
				);
			} else {
				return Vector3(4.0, 55.0, 9.0)-pivot;
			}
		}

		Vector3 GetRightHandOffset() {
			Vector3 chargingHandleOffset = chargingHandleAttachment-pivot + Vector3(-5, 11, -3);

			if (trueReloadProgress < 0.70) {
				return Vector3(-0.5, 30.0, 12.0)-pivot;
			} else if (trueReloadProgress < 0.80) {
				return Mix(
					Vector3(-0.5, 30.0, 12.0)-pivot,
					chargingHandleOffset,
					SmoothStep(Min(1.0, (trueReloadProgress-0.70) / 0.10))
				);
			} else if (trueReloadProgress < 0.82) {
				return chargingHandleOffset;
			} else if (trueReloadProgress < 0.87) {
				chargingHandlePulled.Activate();
				return Mix(
					chargingHandleOffset,
					chargingHandleOffset + Vector3(0, -6, 0),
					SmoothStep(Min(1.0, (trueReloadProgress-0.82) / 0.05))
				);
			} else if (trueReloadProgress < 1) {
				return Mix(
					chargingHandleOffset + Vector3(0, -6, 0),
					Vector3(-0.5, 30.0, 12.0)-pivot,
					SmoothStep(Min(1.0, (trueReloadProgress-0.87) / 0.13))
				);
			} else {
				return Vector3(-0.5, 30.0, 12.0)-pivot;
			}
		}

		ViewRifleM14Skin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("WeaponMods/RifleLvl1/Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@magazineModel = renderer.RegisterModel
				("WeaponMods/RifleLvl1/Models/Weapons/Rifle/Magazine.kv6");
			@rearSightModel = renderer.RegisterModel
				("WeaponMods/RifleLvl1/Models/Weapons/Rifle/RearSight.kv6");
			@frontSightModel = renderer.RegisterModel
				("WeaponMods/RifleLvl1/Models/Weapons/Rifle/FrontSight.kv6");
			@chargingHandleModel = renderer.RegisterModel
				("WeaponMods/RifleLvl1/Models/Weapons/Rifle/ChargingHandle.kv6");

			@fireFarSound = dev.RegisterSound
				("WeaponMods/RifleLvl1/Sounds/Weapons/Rifle/FireFar.opus");
			@fireStereoSound = dev.RegisterSound
				("WeaponMods/RifleLvl1/Sounds/Weapons/Rifle/FireStereo.opus");
			@reloadSound = dev.RegisterSound
				("WeaponMods/RifleLvl1/Sounds/Weapons/Rifle/ReloadLocal.opus");

			@fireSmallReverbSound = dev.RegisterSound
				("WeaponMods/RifleLvl1/Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound
				("WeaponMods/RifleLvl1/Sounds/Weapons/Rifle/V2AmbienceLarge.opus");

			@scopeImage = renderer.RegisterImage("Gfx/Rifle.png");

			raiseSpring.position = 1;
			charmRaiseSpring.position = 1;
		}

		void Update(float dt) {
			BasicViewWeapon::Update(dt);

			recoilVerticalSpring.damping = Mix(16, 24, AimDownSightState);
			recoilBackSpring.damping = Mix(12, 20, AimDownSightState);
			recoilRotationSpring.damping = Mix(8, 16, AimDownSightState);

			recoilVerticalSpring.Update(dt);
			recoilBackSpring.Update(dt);
			recoilRotationSpring.Update(dt);

			horizontalSwingSpring.velocity += swing.x * 60 * dt * 2;
			horizontalSwingSpring.Update(dt);
			verticalSwingSpring.velocity += swing.z * 60 * dt * 2;
			verticalSwingSpring.Update(dt);

			reloadPitchSpring.Update(dt);
			reloadRollSpring.Update(dt);
			reloadOffsetSpring.Update(dt);
			trueReloadProgress += dt/2.5;

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

			swingFromSpring = Vector3(horizontalSwingSpring.position, 0, verticalSwingSpring.position);
		}

		void WeaponFired(){
			BasicViewWeapon::WeaponFired();

			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.referenceDistance = 4.f;
				param.volume = 1.f;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.f;
				audioDevice.PlayLocal(fireStereoSound, origin, param);

				param.volume = 8.f * environmentRoom;
				if (environmentSize < 0.5f) {
					audioDevice.PlayLocal(fireSmallReverbSound, origin, param);
				} else {
					audioDevice.PlayLocal(fireLargeReverbSound, origin, param);
				}
			}

			recoilVerticalSpring.velocity = recoilVerticalSpring.velocity + 1.5;
			recoilBackSpring.velocity = recoilBackSpring.velocity + 1.5;
			recoilRotationSpring.velocity = recoilRotationSpring.velocity + GetRandom()*2-1;

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;
		}

		void ReloadingWeapon() {
			magazineTouched.Reset();
			magazineRemoved.Reset();
			magazineInserted.Reset();
			chargingHandlePulled.Reset();

			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 0.5f;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}

			trueReloadProgress = 0;
			charmHorizontalSwingSpring.velocity += 0.1F;
			charmVerticalSwingSpring.velocity += 0.1F;
		}

		// (override BasicViewWeapon::GetViewWeaponMatrix())
		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			float sp = 1.0F - AimDownSightStateSmooth;

			mat = CreateEulerAnglesMatrix(Vector3(0.2, -0.0, -0.8)*sprintSpring.position) * mat;
			mat = CreateTranslateMatrix(Vector3(0.0, -0.1, 0.05)*sprintSpring.position) * mat;

			// raise gun animation
			mat = CreateRotateMatrix(Vector3(0.0, 0.0, 1.0), raiseSpring.position * -1.3) * mat;
			mat = CreateRotateMatrix(Vector3(0.0, 1.0, 0.0), raiseSpring.position * -1.2) * mat;
			mat = CreateRotateMatrix(Vector3(1.0, 0.0, 0.0), raiseSpring.position * -1) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1, -0.3, 0.8) * raiseSpring.position) * mat;

			// recoil animation
			Vector3 recoilRot;
			Vector3 recoilOffset;
			recoilRot = Vector3(-2.5 * recoilVerticalSpring.position, 0.3 * recoilRotationSpring.position, 0.3 * recoilRotationSpring.position) * sp;
			recoilOffset = Vector3(0.0, 0.0, -0.1) * recoilVerticalSpring.position;
			recoilOffset = recoilOffset + Vector3(0.0, -1.2, 0) * recoilBackSpring.position;

			// No recoil when the player is aiming. Multiply by (1 - aimScopingState)
			mat = CreateEulerAnglesMatrix(recoilRot) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset);

			// add weapon offset
			Vector3 sightPos = (frontSightAttachment-pivot + Vector3(0, 0, -2)) * globalScale;

			Vector3 trans(0.0F, 0.0F, 0.0F);

			if (cg_trueAimDownSight.BoolValue and cg_pngScope.BoolValue)
				trans += Vector3(Mix(-0.26F, -0.13F, sp), 0.3F * sp, Mix(0.3F, 0.2F, sp));
			else
				trans += Vector3(-0.13F * sp, Mix(0.2F, 0.3F, sp), GetZPos(-sightPos.z));

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue * sp;
			trans.y += cg_viewWeaponY.FloatValue * sp;
			trans.z += cg_viewWeaponZ.FloatValue * sp;
			trans.x *= abs(weapSide);

			// add weapon sway
			trans += swing * Vector3(1.0F, 0.5F, 1.0F) * sp;

			mat = CreateTranslateMatrix(trans) * mat;

			// twist the gun when strafing
			// don't rotate when scoped
			mat = mat * CreateEulerAnglesMatrix(Vector3(-2.0*swingFromSpring.z, 0, 2.0*swingFromSpring.x) * sp);
			mat = mat * CreateTranslateMatrix(Vector3(0.5*swingFromSpring.x, 0, 0.5*swingFromSpring.z));

			if ((cg_trueAimDownSight.BoolValue and not cg_pngScope.BoolValue) and AimDownSightStateSmooth > 0.0F)
				mat = AdjustToAlignSight(mat, sightPos, AimDownSightStateSmooth);

			// reload animation
			mat = AdjustToReload(mat);

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		// IWeaponSkin3 (override BasicViewWeapon::{get_MuzzlePosition, get_CaseEjectPosition})
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 1.0F, -0.05F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 0.2F, -0.075F); } }

		void Draw2D() {
			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
			if (cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				RightHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				return;
			}

			Matrix4 mat = CreateScaleMatrix(globalScale);
			mat = GetViewWeaponMatrix() * mat;

			bool reloading = IsReloading;
			float reload = trueReloadProgress;
			Vector3 leftHand, rightHand;

			leftHand = mat * GetLeftHandOffset();
			rightHand = mat * GetRightHandOffset();

			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix;
			param.depthHack = true;
			renderer.AddModel(gunModel, param);

			// draw sights
			Matrix4 sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(rearSightAttachment - pivot);
			sightMat *= CreateScaleMatrix(rearSightScale);
			param.matrix = sightMat;
			renderer.AddModel(rearSightModel, param);

			sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(frontSightAttachment - pivot);
			sightMat *= CreateScaleMatrix(frontSightScale);
			param.matrix = sightMat;
			renderer.AddModel(frontSightModel, param);

			// draw charging handle
			Matrix4 chargingMat = weapMatrix;
			chargingMat *= CreateTranslateMatrix(chargingHandleAttachment - pivot);
			if (!IsReloading) {
				if (readyState < 1.0) {
					if (readyState < 0.05) {
						chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * readyState/0.05);
					} else if (readyState < 0.10) {
						chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) );
					} else if (readyState  < 0.15) {
						chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (0.10-readyState)/0.05);
					}
				}
			} else {
				if (trueReloadProgress < 0.82) {
					chargingMat = chargingMat;
				} else if (trueReloadProgress < 0.87) {
					chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (trueReloadProgress - 0.82)/0.05);
				} else if (trueReloadProgress < 0.89) {
					chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (1 - (trueReloadProgress - 0.87)/0.02));
				}
			}
			chargingMat *= CreateScaleMatrix(chargingHandleScale);
			param.matrix = chargingMat;
			renderer.AddModel(chargingHandleModel, param);

			// magazine/reload action
			Matrix4 magazineMat = weapMatrix;
			// Not reloading.
			magazineMat *= CreateTranslateMatrix(GetMagazineOffset());
			magazineMat *= CreateScaleMatrix(magazineScale);
			// 15 degree tilt forward
			// magazineMat *= CreateEulerAnglesMatrix( Vector3(-0.2618, 0, 0) );
			// magazineMat = AdjustMagazineToReload(magazineMat, trueReloadProgress);

			param.matrix = magazineMat;
			renderer.AddModel(magazineModel, param);

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
				param.matrix = weapMatrix
					* CreateTranslateMatrix(3.5F, 42.0F, -3.0F)
					* CreateScaleMatrix(0.3F);
				renderer.AddModel(charmBaseModel, param);

				param.matrix = param.matrix
					* CreateTranslateMatrix(-0.75F, -1.0F, 1.25F)
					* CreateEulerAnglesMatrix(charmRot)
					* CreateEulerAnglesMatrix(Vector3(rad(90.0F), 0, rad(-90.0F)));
				renderer.AddModel(charmModel, param);
			}

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}

}
