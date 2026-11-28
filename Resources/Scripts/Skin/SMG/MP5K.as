/*
 Copyright (c) 2013 yvt
 Modified by Paratrooper

 This file is part of OpenSpades.
 Modified by PTrooper
 Modified by Nuceto

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
	class ViewSMGMP5KSkin : BasicViewWeapon {
		private AudioDevice@ audioDevice;
		private Model@ Weapon;
		private Model@ chargingHandle;
		private Model@ FrontSight;
		private Model@ DotSight;
		private Model@ Suppressor;
		private Model@ ironSight;

		private Model@ MagFull;
		private Model@ MagEmpty;

		private ConfigItem mp5_sp("mp5_sp", "1");
		private ConfigItem mp5_disablephys("mp5_disablephys", "0");

		private AudioChunk@[] fireSounds(4);
		private AudioChunk@[] spfireSounds(4);
		private AudioChunk@ fireFarSound;
		private AudioChunk@ reloadSound;

		// Constants
		// Pivots
		// Weapon
		private Vector3 pivot = Vector3(3.5, 35.0, 9.0);
		// Front Sight
		private Vector3 rearSightAttachment = Vector3(-19.5F, -35.0F, -120.5f);
		private Vector3 frontSightAttachment = Vector3(-19.5F, 248.5F, -133.0F);
		private Vector3 reflexSightAttachment = Vector3(-18.5F, 40.0F, -145.0F);

		// Scale
		// Weapon and global scale multiplier.
		private float globalScale = 0.015F;

		// A bunch of springs.
		private ViewWeaponSpring recoilVerticalSpring = ViewWeaponSpring(300, 24);
		private ViewWeaponSpring recoilBackSpring = ViewWeaponSpring(300, 16);
		private ViewWeaponSpring recoilRotationSpring = ViewWeaponSpring(100, 8);
		private ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring reloadPitchSpring = ViewWeaponSpring(150, 12, 0);
		private ViewWeaponSpring reloadRollSpring = ViewWeaponSpring(150, 16, 0);
		private ViewWeaponSpring ZPitchSpring = ViewWeaponSpring(150, 16, 0);
		private ViewWeaponSpring reloadOffsetSpring = ViewWeaponSpring(150, 12, 0);
		private ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 11, 0);
		private ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);
		private Vector3 swingFromSpring = Vector3();

		// A bunch of events.
		private ViewWeaponEvent magazineTouched = ViewWeaponEvent();
		private ViewWeaponEvent magazineRemoved = ViewWeaponEvent();
		private ViewWeaponEvent magazineInserted = ViewWeaponEvent();
		private ViewWeaponEvent chargingHandlePulled = ViewWeaponEvent();
		private ViewWeaponEvent Slap = ViewWeaponEvent();

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		Matrix4 AdjustToReload(Matrix4 mat) {
			if (reloadProgress < 0.25) {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = 0;
				charmVerticalSwingSpring.desired = 0;
			} else if (reloadProgress < 0.70) {
				reloadPitchSpring.desired = 0.5;
				reloadRollSpring.desired = 0.4;
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
				reloadPitchSpring.velocity = -2;
				charmVerticalSwingSpring.velocity += 0.1F;
			}

			if (magazineInserted.WasActivated()) {
				magazineInserted.Acknowledge();
				reloadPitchSpring.velocity = 4;
				charmVerticalSwingSpring.velocity += 0.1F;
			}

			if (chargingHandlePulled.WasActivated()) {
				chargingHandlePulled.Acknowledge();
				reloadPitchSpring.velocity = 1;
				ZPitchSpring.velocity = 1.5;
				reloadOffsetSpring.velocity = 0.5;
				charmVerticalSwingSpring.velocity += 0.2F;
			}

			if(reloadProgress > 0.82){
				Slap.Activate();
			}

			if (Slap.WasActivated()) {
				Slap.Acknowledge();
				reloadPitchSpring.velocity = -2;
				ZPitchSpring.velocity = -1.5;
				charmHorizontalSwingSpring.velocity += 0.1F;
				charmVerticalSwingSpring.velocity += 0.2F;
			}

			mat *= CreateEulerAnglesMatrix( Vector3(0.0, 0.6, 0.0) * reloadRollSpring.position );//Y
			mat *= CreateEulerAnglesMatrix( Vector3(-1.0, 0.0, 0.0) * reloadPitchSpring.position );//X
			mat *= CreateEulerAnglesMatrix( Vector3(0.0, 0.0, -1.0) * ZPitchSpring.position );
			mat *= CreateTranslateMatrix( Vector3(0.0, -1, 0) * reloadOffsetSpring.position );//translate Y

			return mat;
		}

		Vector3 GetMagazineOffset() {
			Vector3 magazineAttachment = Vector3(-7.5f, 99.f, 25.f);
			if (reloadProgress < 0.30) {
				return magazineAttachment - pivot;
			} else if (reloadProgress < 0.35) {
				magazineRemoved.Activate();
				return Mix(
					magazineAttachment - pivot,
					magazineAttachment - pivot + Vector3(0.0, -120, 140),
					SmoothStep(Min(1.0, (reloadProgress-0.30) / 0.10))
				);
			} else if (reloadProgress < 0.50) {
				return magazineAttachment - pivot + Vector3(0.0, -120, 140);
			} else if (reloadProgress < 0.60) {
				return Mix(
					magazineAttachment - pivot + Vector3(0.0, -120, 140),
					magazineAttachment - pivot,
					SmoothStep(Min(1.0, (reloadProgress-0.5) / 0.1))
				);
			} else {
				magazineInserted.Activate();
				return magazineAttachment - pivot;
			}
		}

		Vector3 GetLeftHandOffset() {
			Vector3 leftHandDefaultOffset = Vector3(5.0, 50.0, 12.0);
			Vector3 ChargingHandle = Vector3(12.0, 50.0, 3.0);
			Vector3 ChargingHandleBack = Vector3(12.0, 45.0, 2.0);
			Vector3 magazine = Vector3(11.0, -22.0, 0.0);
			Vector3 magazinePos = Vector3(8.0, 45.0, 15.0);
			Vector3 slapUp = Vector3(12.0, 45.0, -2.0);
			Vector3 slapDown = Vector3(12.0, 55.0, 8.0);

			if (reloadProgress < 0.10) {
				return Mix(
					leftHandDefaultOffset-pivot,
					ChargingHandle-pivot,
					SmoothStep(Min(1.0, (reloadProgress) / 0.10))
				);
			}else if (reloadProgress < 0.2) {
				chargingHandlePulled.Activate();
				return Mix(
					ChargingHandle-pivot,
					ChargingHandleBack-pivot,
					SmoothStep(Min(1.0, (reloadProgress-0.10) / 0.10))
				);
			}else if (reloadProgress < 0.30) {
				return Mix(
					ChargingHandleBack-pivot,
					magazinePos-pivot,
					SmoothStep(Min(1.0, (reloadProgress-0.20) / 0.05))
				);
			}else if (reloadProgress < 0.35) {
				return Mix(
					magazinePos-pivot,
					(GetMagazineOffset()/2) + magazine,
					SmoothStep(Min(1.0, (reloadProgress-0.30) / 0.10))
				);
			}else if (reloadProgress < 0.70) {
				magazineTouched.Activate();
				return (GetMagazineOffset()/2) + magazine;

			} else if (reloadProgress < 0.80) {
				return Mix(
					(GetMagazineOffset()/2) + magazine,
					slapUp-pivot,
					SmoothStep(Min(1.0, (reloadProgress-0.7) / 0.10))
				);
			}else if (reloadProgress < 0.85) {
				return Mix(
					slapUp-pivot,
					slapDown-pivot,
					SmoothStep(Min(1.0, (reloadProgress-0.8) / 0.05))
				);
			}else if (reloadProgress < 0.95) {
				return Mix(
					slapDown-pivot,
					leftHandDefaultOffset-pivot,
					SmoothStep(Min(1.0, (reloadProgress-0.85) / 0.10))
				);
			}else {
				return leftHandDefaultOffset-pivot;
			}
		}

		Vector3 GetRightHandOffset() {
			return Vector3(1, 30.0, 12.0)-pivot;
		}

		ViewSMGMP5KSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@Weapon = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/WeaponFP.kv6");
			@DotSight = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/DotSight.kv6");
			@MagFull = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/MagazineFull.kv6");
			@MagEmpty = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/MagazineEmpty.kv6");
			@chargingHandle = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/ChargingHandle.kv6");
			@FrontSight = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/FrontSight.kv6");
			@Suppressor = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/Suppressor.kv6");
			@ironSight = renderer.RegisterModel("WeaponMods/SMGLvl1/Models/Weapons/SMG/ironSight.kv6");

			@fireSounds[0] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/V2Local1.opus");
			@fireSounds[1] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/V2Local2.opus");
			@fireSounds[2] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/V2Local3.opus");
			@fireSounds[3] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/V2Local4.opus");

			@spfireSounds[0] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/spV2Local1.opus");
			@spfireSounds[1] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/spV2Local2.opus");
			@spfireSounds[2] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/spV2Local3.opus");
			@spfireSounds[3] = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/spV2Local4.opus");

			@fireFarSound = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/FireFar.opus");
			@reloadSound = dev.RegisterSound
				("WeaponMods/SMGLvl1/Sounds/Weapons/SMG/ReloadLocal.opus");

			@scopeImage = renderer.RegisterImage("Gfx/SMG.png");

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
			ZPitchSpring.Update(dt);
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

			if (not mp5_disablephys.BoolValue)
				swingFromSpring = Vector3(horizontalSwingSpring.position, 0, verticalSwingSpring.position);
		}

		void WeaponFired(){
			BasicViewWeapon::WeaponFired();

			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 8.f;
				if(mp5_sp.FloatValue > 0){
					audioDevice.PlayLocal(spfireSounds[GetRandom(spfireSounds.length)], origin, param);
				}else{
					audioDevice.PlayLocal(fireSounds[GetRandom(fireSounds.length)], origin, param);
				}
			}

			recoilVerticalSpring.velocity += 0.75;
			recoilBackSpring.position += 0.01;
			//recoilBackSpring.velocity += 0.75;
			recoilRotationSpring.velocity += (GetRandom() * 2 - 1);

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;
		}

		void ReloadingWeapon() {
			magazineTouched.Reset();
			magazineRemoved.Reset();
			magazineInserted.Reset();
			chargingHandlePulled.Reset();
			Slap.Reset();

			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 1.f;
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

			if (not mp5_disablephys.BoolValue) {
				mat = CreateEulerAnglesMatrix(Vector3(0.2, -0.0, -0.8)*sprintSpring.position) * mat;
				mat = CreateTranslateMatrix(Vector3(0.0, -0.1, 0.05)*sprintSpring.position) * mat;
			} else {
				if (sprintStateSmooth > 0.f) {
					mat = CreateRotateMatrix(Vector3(0.f, 1.f, 0.f),
						sprintStateSmooth * -0.1f) * mat;
					mat = CreateRotateMatrix(Vector3(1.f, 0.f, 0.f),
						sprintStateSmooth * 0.3f) * mat;
					mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f),
						sprintStateSmooth * -0.65f) * mat;
					mat = CreateTranslateMatrix(Vector3(0.03f, -0.15f, 0.05f)
						* sprintStateSmooth)  * mat;
				}
			}

			// raise gun animation
			mat = CreateRotateMatrix(Vector3(0.0, 0.0, 1.0), raiseSpring.position * -1.3) * mat;
			mat = CreateRotateMatrix(Vector3(0.0, 1.0, 0.0), raiseSpring.position * -1.2) * mat;
			mat = CreateRotateMatrix(Vector3(1.0, 0.0, 0.0), raiseSpring.position * -1) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1, -0.3, 0.8) * raiseSpring.position) * mat;

			// recoil animation
			Vector3 recoilRot(0.0F, 0.0F, 0.0F);
			recoilRot.x = -2.5F * recoilVerticalSpring.position;
			recoilRot.y = 0.3F * recoilRotationSpring.position;
			recoilRot.z = 0.3F * recoilRotationSpring.position;
			Vector3 recoilOffset = Vector3(0.0F, 0.0F, 0.0F);
			recoilOffset.y -= Mix(2.5F, 1.25F, sp) * recoilBackSpring.position;
			recoilOffset.z -= 0.1F * recoilVerticalSpring.position * sp;
			mat = CreateEulerAnglesMatrix(recoilRot * sp) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset);

			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);

			bool reflexScope = cg_reflexScope.BoolValue;
			Vector3 sightAttachment = reflexScope ? reflexSightAttachment : frontSightAttachment;
			Vector3 offset = reflexScope ? Vector3(sightAttachment.x, 0.0F, -53.0F) : Vector3(sightAttachment.x, 0.0F, -56.75F);
			Vector3 sightPos = (sightAttachment - offset) * (globalScale * 0.13F);

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
			trans += swing * sp;

			mat = CreateTranslateMatrix(trans) * mat;

			// twist the gun when strafing
			Vector3 swingRot(0.0F, 0.0F, 0.0F);
			swingRot.z += 2.0F * horizontalSwingSpring.position;
			swingRot.x -= 2.0F * verticalSwingSpring.position;
			mat = mat * CreateEulerAnglesMatrix(swingRot * sp);

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
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * Vector3(-0.02F,  mp5_sp.BoolValue ? 0.45F : 0.3F, -0.085F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * Vector3(-0.025F, 0.085F, -0.1F); } }

		void Draw2D() {
			// draw reflex sight (2D image)
			if (cg_reflexScope.IntValue >= 3
				and not cg_pngScope.BoolValue
				and AimDownSightStateSmooth > 0.8F) {
				DrawReflexSight2D(GetViewWeaponMatrix(), 0.0075F);
				return;
			}

			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
			if (cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				RightHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				return;
			}

			Matrix4 mat = GetViewWeaponMatrix()
				* CreateScaleMatrix(globalScale);

			Vector3 leftHand, rightHand;
			leftHand = mat * GetLeftHandOffset();
			rightHand = mat * GetRightHandOffset();

			ModelRenderParam param;
			param.depthHack = true;

			Matrix4 weapMatrix = eyeMatrix * mat
				* CreateScaleMatrix(0.13F)
				* CreateTranslateMatrix(10.0F * AimDownSightStateSmooth, 0, 0);

			// draw weapon
			param.matrix = weapMatrix
				* CreateTranslateMatrix(-8.5F, -20.f, 10.f)
				* CreateEulerAnglesMatrix(Vector3(0, rad(90), 0));
			renderer.AddModel(Weapon, param);

			// draw magazine
			param.matrix = weapMatrix
				* CreateTranslateMatrix(GetMagazineOffset())
				* CreateEulerAnglesMatrix(Vector3(0, rad(90), 0));
			renderer.AddModel((reloadProgress < 0.4F) ? MagEmpty : MagFull, param);

			// draw suppressor
			if (mp5_sp.BoolValue) {
				param.matrix = weapMatrix
					* CreateTranslateMatrix(-10.f, 130.f, -44.f);
				renderer.AddModel(Suppressor, param);
			}

			// draw charginghandle
			if (reloadProgress < 0.10) {
				param.matrix = weapMatrix
					* CreateTranslateMatrix(Vector3(-7.f, 107.f, -61.f));
			} else if (reloadProgress < 0.82) {
				param.matrix = weapMatrix
					* CreateTranslateMatrix(Mix(
						Vector3(-7.f, 107.f, -61.f),
						Vector3(-7.f, 80.f, -61.f),
						SmoothStep(Min(1.0, (reloadProgress-0.10) / 0.10))
					))
					* CreateEulerAnglesMatrix(Mix(
						Vector3(0.f, 0.f, 0.f),
						Vector3(0.f, 1.f, 0.f),
						SmoothStep(Min(1.0, (reloadProgress-0.10) / 0.05))
					));
			} else {
				param.matrix = weapMatrix
					* CreateTranslateMatrix(Vector3(-7.f, 107.f, -61.f));
			}
			renderer.AddModel(chargingHandle, param);

			// draw dotsight
			if (cg_reflexScope.BoolValue) {
				param.matrix = weapMatrix
					* CreateScaleMatrix(0.5F)
					* CreateTranslateMatrix(reflexSightAttachment)
					* CreateEulerAnglesMatrix(Vector3(0, rad(90), 0));
				renderer.AddModel(DotSight, param);
			} else {
				param.matrix = weapMatrix
					* CreateScaleMatrix(0.5F)
					* CreateTranslateMatrix(rearSightAttachment);
				renderer.AddModel(ironSight, param);
			}

			// draw frontsight
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.5F)
				* CreateTranslateMatrix(frontSightAttachment);
			renderer.AddModel(FrontSight, param);

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
					* CreateTranslateMatrix(1.75F, 55.0F, -35.5F)
					* CreateScaleMatrix(1.0F);
				renderer.AddModel(charmBaseModel, param);

				param.matrix = param.matrix
					* CreateTranslateMatrix(-0.75F, -1.0F, 1.25F)
					* CreateEulerAnglesMatrix(charmRot)
					* CreateEulerAnglesMatrix(Vector3(rad(90.0F), 0, rad(-90.0F)));
				renderer.AddModel(charmModel, param);
			}

			// draw reflex sight (3D sprite)
			int reflexMode = cg_reflexScope.IntValue;
			if ((reflexMode >= 1 and reflexMode < 3) and not cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.8F) {
				bool dotReflex = reflexMode == 1;
				float reflexSize = 1.0F / (dotReflex ? 50.0F : 8.0F);
				Vector3 offset = Vector3(reflexSightAttachment.x + 10.0F, 0.0F, -53.0F);
				Vector3 sightPos = (reflexSightAttachment - offset);
				DrawReflexSight3D(dotReflex ? ballImage : reflexImage, weapMatrix * sightPos, reflexSize);
			}

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}

}
