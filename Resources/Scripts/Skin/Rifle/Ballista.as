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

// Tower Defense dress: the mod's own view skin, adapted. The class
// is picked by the store's word (the factory in this weapon's stock
// View.as reads the tier); every file it loads is spelled with the
// dress's pak folder in front.

namespace spades {
	class ViewRifleBallistaSkin : BasicViewWeapon {
		private AudioDevice@ audioDevice;
		private Model@ gunModel1;
		private Model@ gunModel2;
		private Model@ gunModel3;
		private Model@ magazineModel;
		private Model@ boltModel;
		private Model@ scopeModel;
		private Model@ scopeModel1;
		private Model@ scopeModel2;
		private Model@ singleVoxelModel;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;

		// Constants

		// Attachment Points
		private Vector3 magazineAttachment = Vector3(0, 0, 0);
		private Vector3 scopeAttachment = Vector3(0.025F, 16.75F, -4.575F);
		private Vector3 boltAttachment = Vector3(-1.0F, 0.0F, -8.0F);

		// Scales
		private float globalScale = 0.033F;
		private float weaponScale = 0.2F;
		private float scopeScale = 0.1F;

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
		private ViewWeaponEvent chargingHandlePushed = ViewWeaponEvent();

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		Matrix4 AdjustToReload(Matrix4 mat) {
			if (reloadProgress > 0.15F and reloadProgress < 0.7) {
				reloadPitchSpring.desired = 0.6;
				reloadRollSpring.desired = 0.6;
				charmVerticalSwingSpring.desired = -0.01F;
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
				reloadPitchSpring.velocity = 8;
				reloadRollSpring.velocity = -2;
				reloadOffsetSpring.velocity = 0.25;
				charmVerticalSwingSpring.velocity += 0.2F;
			}

			if (chargingHandlePushed.WasActivated()) {
				chargingHandlePushed.Acknowledge();
				reloadPitchSpring.velocity = -4;
				reloadRollSpring.velocity = 0.5;
				reloadOffsetSpring.velocity = -0.25;
				charmVerticalSwingSpring.velocity -= 0.2F;
			}

			mat *= CreateEulerAnglesMatrix(Vector3(0, 1, 0) * reloadRollSpring.position);
			mat *= CreateEulerAnglesMatrix(Vector3(-0.25, 0, 0) * reloadPitchSpring.position);
			mat *= CreateTranslateMatrix(Vector3(0, -1, 0) * reloadOffsetSpring.position);

			return mat;
		}

		Vector3 GetMagazineOffset() {
			Vector3 detachMagPos = Vector3(0, 0, 0);
			Vector3 dispenseMagPos = Vector3(0, 0, 15);

			if (reloadProgress < 0.2) {
				return magazineAttachment;
			} else if (reloadProgress < 0.3) {
				magazineRemoved.Activate();
				float per = Min(1.0, (reloadProgress-0.2) / (0.3-0.2));
				return Mix(magazineAttachment, detachMagPos, SmoothStep(per));
			} else if (reloadProgress < 0.35) {
				return detachMagPos;
			} else if (reloadProgress < 0.4) {
				float per = Min(1.0, (reloadProgress-0.35) / (0.4-0.35));
				return Mix(detachMagPos, dispenseMagPos, SmoothStep(per));
			} else if (reloadProgress < 0.5) {
				return dispenseMagPos;
			} else if (reloadProgress < 0.55) {
				float per = Min(1.0, (reloadProgress-0.5) / (0.55-0.5));
				return Mix(dispenseMagPos, detachMagPos, SmoothStep(per));
			} else if (reloadProgress < 0.6) {
				magazineInserted.Activate();
				float per = Min(1.0, (reloadProgress-0.55) / (0.6-0.55));
				return Mix(detachMagPos, magazineAttachment, SmoothStep(per));
			} else {
				return magazineAttachment;
			}
		}

		Vector3 GetLeftHandOffset() {
			Vector3 leftHandOffset = Vector3(1.0F, 9.0F, 2.0F);
			Vector3 magazineOffset = GetMagazineOffset() + Vector3(0, 4, 8);

			if (reloadProgress < 0.2) {
				return leftHandOffset;
			} else if (reloadProgress < 0.3) {
				float per = Min(1.0, (reloadProgress-0.2)/(0.3-0.2));
				return Mix(leftHandOffset, magazineOffset, SmoothStep(per));
			} else if (reloadProgress < 0.65) {
				magazineTouched.Activate();
				return magazineOffset;
			} else if (reloadProgress < 1.0) {
				float per = Min(1.0, (reloadProgress-0.65)/(1.0-0.9));
				return Mix(magazineOffset, leftHandOffset, SmoothStep(per));
			} else {
				return leftHandOffset;
			}
		}

		Vector3 GetRightHandOffset() {
			Vector3 rightHandOffset = Vector3(0.0F, -8.0F, 2.0F);
			Vector3 boltOffset = Vector3(-3.0F, 0.0F, -2.0F);

			// sprint animation
			rightHandOffset -= Vector3(0.0F, 3.0F, 0.5F) * sprintSpring.position;
			boltOffset -= Vector3(2.0F, 2.0F, -2.0F) * sprintSpring.position;

			Vector3 boltLiftOffset = boltOffset - Vector3(0.0F, 0.0F, 2.0F);
			Vector3 boltPullOffset = boltLiftOffset - Vector3(0.0F, 5.0F, 0.0F);
			Vector3 holdOffset = rightHandOffset;

			if (IsReloading) {
				if (reloadProgress < 0.05) { //moves to bolt
					float per = Min(1.0, (reloadProgress/0.05));
					return Mix(rightHandOffset, boltOffset, SmoothStep(per));
				} else if (reloadProgress < 0.1) { //lifts bolt
					float per = Min(1.0, ((reloadProgress-0.05)/(0.1-0.05)));
					return Mix(boltOffset, boltLiftOffset, SmoothStep(per));
				} else if (reloadProgress < 0.15) { //pulls bolt
					chargingHandlePulled.Activate();
					float per = Min(1.0, ((reloadProgress-0.1)/(0.15-0.1)));
					return Mix(boltLiftOffset, boltPullOffset, SmoothStep(per));
				} else if (reloadProgress < 0.2) { //moves back to gun
					float per = Min(1.0, ((reloadProgress-0.15)/(0.2-0.15)));
					return Mix(boltPullOffset, holdOffset, SmoothStep(per));
				} else if (reloadProgress < 0.8) { //holds gun in place
					chargingHandlePulled.Reset();
					return holdOffset;
				} else if (reloadProgress < 0.82) { //moves to bolt
					float per = Min(1.0, ((reloadProgress-0.8)/(0.82-0.8)));
					return Mix(holdOffset, boltPullOffset, SmoothStep(per));
				} else if (reloadProgress < 0.85) { //push bolt
					float per = Min(1.0, ((reloadProgress-0.82)/(0.85-0.82)));
					return Mix(boltPullOffset, boltLiftOffset, SmoothStep(per));
				} else if (reloadProgress < 0.9) { //drops bolt
					chargingHandlePushed.Activate();
					float per = Min(1.0, ((reloadProgress-0.85)/(0.9-0.85)));
					return Mix(boltLiftOffset, boltOffset, SmoothStep(per));
				} else if (reloadProgress < 0.95) { //moves back to gun
					chargingHandlePushed.Reset();
					float per = Min(1.0, ((reloadProgress-0.9)/(0.95-0.9)));
					return Mix(boltOffset, rightHandOffset, SmoothStep(per));
				}
			} else {
				if (readyState < 0.1666) {//raise bolt animation
					float per = Min(1.0, (readyState/0.1666));
					return Mix(rightHandOffset, boltOffset, SmoothStep(per));
				} else if (readyState < 0.3333) {//raise bolt animation
					float per = Min(1.0, ((readyState-0.1666)/(0.3333-0.1666)));
					return Mix(boltOffset, boltLiftOffset, SmoothStep(per));
				} else if (readyState < 0.5) {//pull bolt animation
					chargingHandlePulled.Activate();
					float per = Min(1.0, ((readyState-0.3333)/(0.5-0.3333)));
					return Mix(boltLiftOffset, boltPullOffset, SmoothStep(per));
				} else if (readyState < 0.6666) {//pull bolt animation
					chargingHandlePulled.Reset();
					float per = Min(1.0, ((readyState-0.5)/(0.6666-0.5)));
					return Mix(boltPullOffset, boltLiftOffset, SmoothStep(per));
				} else if (readyState < 0.8333) {//push bolt animation
					chargingHandlePushed.Activate();
					float per = Min(1.0, ((readyState-0.6666)/(0.8333-0.6666)));
					return Mix(boltLiftOffset, boltOffset, SmoothStep(per));
				} else if (readyState < 1.0) {//push bolt animation
					chargingHandlePushed.Reset();
					float per = Min(1.0, ((readyState-0.8333)/(1.0-0.8333)));
					return Mix(boltOffset, rightHandOffset, SmoothStep(per));
				}
			}

			return rightHandOffset;
		}

		ViewRifleBallistaSkin(Renderer@ r, AudioDevice@ dev) {
			super(r);
			@audioDevice = dev;
			@gunModel1 = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@gunModel2 = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/Barrel.kv6");
			@gunModel3 = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/Stock.kv6");
			@magazineModel = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/Magazine.kv6");
			@scopeModel = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/Scope.kv6");
			@scopeModel1 = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/ScopeFront.kv6");
			@scopeModel2 = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/ScopeRear.kv6");
			@singleVoxelModel = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/SingleVoxel.kv6");
			@boltModel = renderer.RegisterModel("WeaponMods/RifleLvl2/Models/Weapons/Rifle/Bolt.kv6");

			@fireSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireLocal.opus");
			@fireFarSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireStereo.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/Rifle/ReloadLocal.opus");

			@fireSmallReverbSound = dev.RegisterSound("Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound("Sounds/Weapons/Rifle/V2AmbienceLarge.opus");

			@scopeImage = renderer.RegisterImage("Gfx/Rifle.png");

			// initial raise spring
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

		void WeaponFired() {
			BasicViewWeapon::WeaponFired();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.referenceDistance = 4.0F;
				param.volume = 1.0F;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.0F;
				audioDevice.PlayLocal(fireStereoSound, origin, param);

				param.volume = 8.0F * environmentRoom;
				audioDevice.PlayLocal((environmentSize < 0.5F)
					? fireSmallReverbSound : fireLargeReverbSound, origin, param);
			}

			recoilVerticalSpring.velocity += 1.5;
			recoilBackSpring.velocity += 1.5;
			recoilRotationSpring.velocity += (GetRandom() * 2 - 1);

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;
		}

		void ReloadingWeapon() {
			magazineTouched.Reset();
			magazineRemoved.Reset();
			magazineInserted.Reset();
			chargingHandlePulled.Reset();
			chargingHandlePushed.Reset();

			if (!IsMuted){
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 0.5F;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}

			charmHorizontalSwingSpring.velocity += 0.1F;
			charmVerticalSwingSpring.velocity += 0.1F;
		}

		// (override BasicViewWeapon::GetZPos())
		float GetZPos() { return 0.2F - AimDownSightStateSmooth * 0.052F; }

		// (override BasicViewWeapon::GetViewWeaponMatrix())
		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			float sp = 1.0F - AimDownSightStateSmooth;

			// sprinting animation
			mat = CreateEulerAnglesMatrix(Vector3(0.44F, 0, -0.5F) * sprintSpring.position) * mat;
			mat = CreateTranslateMatrix(Vector3(0.2F, 0, 0.15F) * sprintSpring.position) * mat;

			// raise gun animation
			mat = CreateRotateMatrix(Vector3(0, 0, 1), raiseSpring.position * -1.3F) * mat;
			mat = CreateRotateMatrix(Vector3(0, 1, 0), raiseSpring.position * 0.2F) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1F, -0.3F, 0.1F) * raiseSpring.position) * mat;

			// recoil animation
			Vector3 recoilRot(0, 0, 0);
			recoilRot.x = -2.5 * recoilVerticalSpring.position;
			recoilRot.y = 0.3F * recoilRotationSpring.position;
			recoilRot.z = 0.3F * recoilRotationSpring.position;;
			Vector3 recoilOffset = Vector3(0, 0, -0.1) * recoilVerticalSpring.position;
			recoilOffset -= Vector3(0, 1.2, 0) * recoilBackSpring.position;
			mat = CreateEulerAnglesMatrix(recoilRot * sp) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset);

			// add weapon offset
			Vector3 trans(0, 0, 0);

			if (cg_trueAimDownSight.BoolValue and cg_pngScope.BoolValue)
				trans += Vector3(Mix(-0.26F, -0.13F, sp), 0.5F * sp, Mix(0.3F, 0.2F, sp));
			else
				trans += Vector3(-0.13F * sp, 0.5F, GetZPos());

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue * sp;
			trans.y += cg_viewWeaponY.FloatValue * sp;
			trans.z += cg_viewWeaponZ.FloatValue * sp;
			trans.x *= abs(weapSide);

			// add weapon sway
			trans += swing * GetMotionGain();

			mat *= CreateTranslateMatrix(trans);

			// twist the gun when strafing
			Vector3 strafeRot = Vector3(-2.0*swingFromSpring.z, 0, 2.0*swingFromSpring.x);
			mat = mat * CreateEulerAnglesMatrix(strafeRot * sp);

			if ((cg_trueAimDownSight.BoolValue and not cg_pngScope.BoolValue) and AimDownSightStateSmooth > 0.0F) {
				Vector3 sightPos = (scopeAttachment - Vector3(0.025F, 0.0F, 0.025F)) * globalScale;
				mat = AdjustToAlignSight(mat, sightPos, AimDownSightStateSmooth);
			}

			// reload animation
			mat = AdjustToReload(mat);

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		// IWeaponSkin3 (override BasicViewWeapon::{get_MuzzlePosition, get_CaseEjectPosition})
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 1.25F, -0.075F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 0.1F, -0.1F); } }

		void Draw2D() {
			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
			if (AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0, 0, 0);
				RightHandPosition = Vector3(0, 0, 0);

				// Non-uniform scaling is not allowed when using the software renderer.
				// ONLY draw the crosshairs and scope if we're using the gl renderer.
				// Otherwise, just draw an image in the center (check Draw2D).
				ConfigItem r_renderer("r_renderer");
				if (r_renderer.StringValue == "gl" and not cg_pngScope.BoolValue) {
					ModelRenderParam param;
					param.depthHack = true;

					Matrix4 scopeMatrix = eyeMatrix
						* CreateScaleMatrix(0.01F)
						* CreateTranslateMatrix(Vector3(0.0F, 50.0F, 0.0F));
					param.matrix = scopeMatrix;
					renderer.AddModel(scopeModel, param);

					// draw hair lines
					param.ghost = true;
					float size = 35.0F;
					float thick = 0.12F;
					Matrix4 linesMatrix = scopeMatrix
						* CreateTranslateMatrix(0.0F, 15.0F, 0.0F);

					// vertical hair
					param.matrix = linesMatrix
						* CreateScaleMatrix(thick, thick, size);
					renderer.AddModel(singleVoxelModel, param);

					// horizontal hair
					param.matrix = linesMatrix
						* CreateScaleMatrix(size, thick, thick);
					renderer.AddModel(singleVoxelModel, param);
				}

				return;
			}

			Matrix4 mat = GetViewWeaponMatrix()
				* CreateScaleMatrix(globalScale);

			Vector3 leftHand, rightHand;
			leftHand = mat * GetLeftHandOffset();
			rightHand = mat * GetRightHandOffset();

			ModelRenderParam param;
			param.depthHack = true;

			Matrix4 weapMatrix = eyeMatrix * mat;

			// draw weapon
			param.matrix = weapMatrix
				* CreateScaleMatrix(weaponScale);
			renderer.AddModel(gunModel1, param);
			renderer.AddModel(gunModel2, param);
			renderer.AddModel(gunModel3, param);

			// draw scope
			param.matrix = weapMatrix
				* CreateScaleMatrix(scopeScale);
			renderer.AddModel(scopeModel1, param);
			renderer.AddModel(scopeModel2, param);

			// draw magazine
			param.matrix = weapMatrix
				* CreateTranslateMatrix(GetMagazineOffset())
				* CreateScaleMatrix(weaponScale);
			renderer.AddModel(magazineModel, param);

			Matrix4 boltMat = weapMatrix;
			Vector3 boltOffset = Vector3(-0.95F, 0.0F, -0.08F);
			Vector3 boltPullOffset = boltOffset - Vector3(0.0F, 5.0F, 0.0F);
			Vector3 boltRot = Vector3(0.0F, -0.5F, 0.0F);

			if (IsReloading) {
				if (reloadProgress < 0.05) { //moves to bolt
					boltMat *= CreateTranslateMatrix(Vector3(0, 0, 0));
					boltMat *= CreateEulerAnglesMatrix(Vector3(0, 0, 0));
				} else if (reloadProgress < 0.1) { //lifts bolt
					float per = Min(1.0, ((reloadProgress-0.05)/(0.1-0.05)));
					per = SmoothStep(per);
					boltMat *= CreateTranslateMatrix(Mix(Vector3(0, 0, 0), boltOffset, per));
					boltMat *= CreateEulerAnglesMatrix(Mix(Vector3(0, 0, 0), boltRot, per));
				} else if (reloadProgress < 0.15) { //pulls bolt
					float per = Min(1.0, ((reloadProgress-0.1)/(0.15-0.1)));
					boltMat *= CreateTranslateMatrix(Mix(boltOffset, boltPullOffset, SmoothStep(per)));
					boltMat *= CreateEulerAnglesMatrix(boltRot);
				} else if (reloadProgress < 0.2) { //moves back to gun
					boltMat *= CreateTranslateMatrix(boltPullOffset);
					boltMat *= CreateEulerAnglesMatrix(boltRot);
				} else if (reloadProgress < 0.8) { //holds gun in place
					boltMat *= CreateTranslateMatrix(boltPullOffset);
					boltMat *= CreateEulerAnglesMatrix(boltRot);
				} else if (reloadProgress < 0.82) { //moves to bolt
					boltMat *= CreateTranslateMatrix(boltPullOffset);
					boltMat *= CreateEulerAnglesMatrix(boltRot);
				} else if (reloadProgress < 0.85) { //push bolt
					float per = Min(1.0, ((reloadProgress-0.82)/(0.85-0.82)));
					boltMat *= CreateTranslateMatrix(Mix(boltPullOffset, boltOffset, SmoothStep(per)));
					boltMat *= CreateEulerAnglesMatrix(boltRot);
				} else if (reloadProgress < 0.9) { //drops bolt
					float per = Min(1.0, ((reloadProgress-0.85)/(0.9-0.85)));
					per = SmoothStep(per);
					boltMat *= CreateTranslateMatrix(Mix(boltOffset, Vector3(0, 0, 0), per));
					boltMat *= CreateEulerAnglesMatrix(Mix(boltRot, Vector3(0, 0, 0), per));
				} else if (reloadProgress < 0.95) { //moves back to gun
					boltMat *= CreateTranslateMatrix(Vector3(0, 0, 0));
					boltMat *= CreateEulerAnglesMatrix(Vector3(0, 0, 0));
				} else {
					boltMat *= CreateTranslateMatrix(Vector3(0, 0, 0));
					boltMat *= CreateEulerAnglesMatrix(Vector3(0, 0, 0));
				}
			} else {
				if (readyState < 0.1666) { // No animation
					boltMat *= CreateTranslateMatrix(Vector3(0, 0, 0));
					boltMat *= CreateEulerAnglesMatrix(Vector3(0, 0, 0));
				} else if (readyState < 0.3333) { // Raise bolt animation
					float per = Min(1.0, ((readyState-0.1666)/(0.3333-0.1666)));
					per = SmoothStep(per);
					boltMat *= CreateTranslateMatrix(Mix(Vector3(0, 0, 0), boltOffset, per));
					boltMat *= CreateEulerAnglesMatrix(Mix(Vector3(0, 0, 0), boltRot, per));
				} else if (readyState < 0.5) { // Pull bolt animation
					float per = Min(1.0, ((readyState-0.3333)/(0.5-0.3333)));
					boltMat *= CreateTranslateMatrix(Mix(boltOffset, boltPullOffset, SmoothStep(per)));
					boltMat *= CreateEulerAnglesMatrix(boltRot);
				} else if (readyState < 0.6666) { // Pull bolt animation in reverse
					float per = Min(1.0, ((readyState-0.5)/(0.6666-0.5)));
					boltMat *= CreateTranslateMatrix(Mix(boltPullOffset, boltOffset, SmoothStep(per)));
					boltMat *= CreateEulerAnglesMatrix(boltRot);
				} else if (readyState < 0.8333) { // Complete pull bolt animation
					float per = Min(1.0, ((readyState-0.6666)/(0.8333-0.6666)));
					per = SmoothStep(per);
					boltMat *= CreateTranslateMatrix(Mix(boltOffset, Vector3(0, 0, 0), per));
					boltMat *= CreateEulerAnglesMatrix(Mix(boltRot, Vector3(0, 0, 0), per));
				} else {
					boltMat *= CreateTranslateMatrix(Vector3(0, 0, 0));
					boltMat *= CreateEulerAnglesMatrix(Vector3(0, 0, 0));
				}
			}

			// draw bolt
			param.matrix = boltMat
				* CreateScaleMatrix(weaponScale)
				* CreateTranslateMatrix(boltAttachment);
			renderer.AddModel(boltModel, param);

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
					* CreateTranslateMatrix(1.1F, 1.5F, -1.5F)
					* CreateScaleMatrix(0.1F);
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
