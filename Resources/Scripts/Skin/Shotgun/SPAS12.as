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
 namespace spades 
 {
	class ViewShotgunSPASSkin: 
	IToolSkin, IViewToolSkin, IWeaponSkin,
	BasicViewWeapon 
	{
		
		private AudioDevice@ audioDevice;
		private Model@ gunModel;

		private Model@ pumpModel;

		private Model@ sightModel2;
		
		private ConfigItem n_rgbSpeed("n_rgbSpeed", "0.1");
		private float Time;
		
		private AudioChunk@ fireSound;
		private AudioChunk@ fireemptySound;
		private AudioChunk@ reloadSound;
		private AudioChunk@ cockSound;
		
		
		int reloadtype = 1;		//0 for empty; 1 for tactical

		// Shell-by-shell reload state, worked out from what ZeroSpades reports each
		// frame (reloading, reloadProgress, ammo, clipSize). The player we control
		// reloads one shell per reload; anyone else, a demo's player included,
		// reloads the whole tube in a single reload.
		private bool wasReloading = false;
		private float lastReloadProgress = 0.0F;
		private bool shellIsFirst = false;	// this shell starts with the gun at rest
		private bool shellIsLast = false;	// this shell fills the gun
		private bool shellStarted = false;	// a shell has been loaded since the skin was created
		private bool loadPoseHeld = false;	// the gun stays turned to the loading port after this shell
		private float returnStart = 0.0F;	// reload progress when a reload ended with the gun still turned
		
		// pivot of the weapon when viewed in slab6
		private Vector3 pivot = Vector3(3.50, -10.0, 20.0);
		// scale of the weapon
		private float globalScale = 0.01;
		// scale of the magazine, relative to the global scale
		private float magazineScale = 0.5;
		// attachment points, in the units of the hand offsets
		private Vector3 muzzleAttachment = Vector3(0.25, 31.5, -5.62);
		private Vector3 caseEjectAttachment = Vector3(1.5, -6.0, -5.5);
		private Vector3 charmAttachment = Vector3(1.74, -10.75, -4.5);
		// centre of the red dot sight's lens, on the line of sight when aiming
		private Vector3 sightLensAttachment = Vector3(0.13, -1.36, -10.24);
		// delta time between each frame
		private float deltatime = 0.0;
		// checks if mag has been thrown
		// reset to false whenever reloading
		private bool hasThrownMag = false;
		
		// Creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
		Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
			Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
			mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
			mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
			
			return mat;
		}
		// select easing functions
		float quadraticIn(float per) {
			return (per*per);
		}
		
		float quadraticOut(float per) {
			return -(per * (per-2));
		}
		
		float quadraticInOut(float per) {
			per < per/2; 
			return (per*per);
			
			per > per/2; 
			return -(per * (per-2));
			
		}	
		
		float cubicIn(float per) {
			return (per*per*per);
		}
		
		float cubicOut(float per) {
			per = per-1;
			return (per*per*per + 1);
		}
		
		ViewShotgunSPASSkin(Renderer@ r, AudioDevice@ dev)
		{
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("WeaponMods/ShotgunLvl2/Models/Weapons/Shotgun/WeaponNoPump.kv6");

			@pumpModel = renderer.RegisterModel
				("WeaponMods/ShotgunLvl2/Models/Weapons/Shotgun/Pump.kv6");

			@sightModel2 = renderer.RegisterModel
				("WeaponMods/ShotgunLvl2/Models/Weapons/Shotgun/Sight2.kv6");

				
			@fireSound = dev.RegisterSound
				("Sounds/Weapons/Shotgun/Fire.opus");
			@fireemptySound = dev.RegisterSound
				("WeaponMods/ShotgunLvl2/Sounds/Weapons/Shotgun/FireEmpty.opus");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/Shotgun/Reload.opus");
			@cockSound = dev.RegisterSound
				("Sounds/Weapons/Shotgun/Cock.opus");

			charmRaiseSpring.position = 1;
		}

		void Update(float dt) {
			BasicViewWeapon::Update(dt);
			Time += dt;

			charmSprintSpring.desired = sprintState;
			charmRaiseSpring.desired = 1.0F - raiseState;
		}
		
		void WeaponFired()
		{
			BasicViewWeapon::WeaponFired();
			
			// ammo still counts this shot here: it is refreshed when the frame is drawn
			bool lastShot = ammo <= 1;
			if(!IsMuted && !lastShot)
			{
				Vector3 origin = Vector3(0.f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 0.3f;
				audioDevice.PlayLocal(fireSound, origin, param);
			}
			if(!IsMuted && lastShot)
			{
				Vector3 origin = Vector3(0.f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 0.3f;
				audioDevice.PlayLocal(fireemptySound, origin, param);
			}

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;
		}
		
		void ReloadingWeapon()
		{
			if(!IsMuted)
			{
				Vector3 origin = Vector3(0.f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 0.2f;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}

			charmHorizontalSwingSpring.velocity += 0.1F;
			charmVerticalSwingSpring.velocity += 0.1F;
		}
		
		void ReloadedWeapon() 
		{
			if(!IsMuted && reloadtype == 0)
			{
				Vector3 origin = Vector3(0.f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 0.2f;
				audioDevice.PlayLocal(cockSound, origin, param);
			}
		}
		
		float GetZPos() 
		{
			return 0.2f - AimDownSightStateSmooth * 0.0535f;
		}
		
		// rotates gun matrix to ensure the sight is in
		// the center of screen (0, ?, 0).
		Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) 
		{
			Vector3 p = mat * sightPos;
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f), atan(p.x / p.y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), atan(p.z / p.y) * fade) * mat;
			//mat = CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), atan(p.z / p.x) * fade) * mat;//Optional gangsta style ADS WARNING, NOT COMPLETE
			return mat;
		}
		
		void Draw2D() {
			// draw reflex sight (2D image)
			if (cg_reflexScope.IntValue >= 3
				and not cg_pngScope.BoolValue
				and AimDownSightStateSmooth > 0.8F) {
				DrawReflexSight2D(GetViewWeaponMatrix()
					* CreateTranslateMatrix((sightLensAttachment + Vector3(0.0F, 5.0F, 0.0F)) * 0.02F), 0.015F);
				return;
			}

			BasicViewWeapon::Draw2D();
		}
		Vector3 HSV2RGB(float h, float s, float v) {
            if (s == 0.0F)
                return Vector3(v, v, v);

            int i = int(h * 6.0F);
            float f = h * 6.0F - i;
            float p = v * (1.0F - s);
            float q = v * (1.0F - s * f);
            float t = v * (1.0F - s * (1.0F - f));

           i = i % 6;
            switch (i) {
                case 0: return Vector3(v, t, p);
                case 1: return Vector3(q, v, p);
                case 2: return Vector3(p, v, t);
                case 3: return Vector3(p, q, v);
                case 4: return Vector3(t, p, v);
                case 5: return Vector3(v, p, q);
            }

            return Vector3(0, 0, 0);
        }

        Vector3 RGB(float speed) {
            speed = 0.002F * speed;
            auto h = (int(Time * 1000) % int(1.0F / speed)) * speed;
            return HSV2RGB(h, 1.0F, 1.0F);
        }
// redefined from BasicViewWeapon.as
		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			// sprinting animation					
			if(sprintState > 0.0) {
				sprintState = quadraticIn(sprintState);
				mat = CreateEulerAnglesMatrix(Vector3(0.2, -0.0, -0.2)*sprintState) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, -0.1, 0.05)*sprintState) * mat;
			}
			
			// raise gun animation
			if(raiseState < 1.0) {
				float putdown = 1.0 - raiseState;
				putdown = cubicIn(putdown);
				mat = CreateRotateMatrix(Vector3(0.0, 0.0, 1.0),
					putdown * -1.3) * mat;
				mat = CreateRotateMatrix(Vector3(0.0, 1.0, 0.0),
					putdown * 0.2) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, -0.3, 0.8)
					* putdown) * mat;
			}
			
			// recoil animation
			Vector3 recoilRot;
			Vector3 recoilOffset;
			if(readyState < 0.1) {
				float per = (readyState/0.1);
				per = cubicOut(per);
				recoilRot = Vector3(-0.1, 0.0, 0.0) * per;
				recoilOffset = Vector3(0.0, -0.08, -0.02) * per;
			} else if(readyState < 0.2) {
				recoilRot = Vector3(-0.1, 0.0, 0.0);
				recoilOffset = Vector3(0.0, -0.08, -0.02);
			} else if(readyState < 0.4) {
				float per = ( (readyState-0.2)/(0.4-0.2) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(-0.1, 0.0, 0.0), Vector3(0.05, 0.0, 0.0), per);
				recoilOffset = Mix(Vector3(0.0, -0.08, -0.02), Vector3(0.0, 0.0, 0.0), per);
			} else if(readyState < 0.8) {
				float per = ( (readyState-0.4)/(0.8-0.4) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(0.05, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per);
				recoilOffset = Vector3(0.0, 0.0, 0.0);
			}
			// No recoil when the player is aiming. Multiply by (1 - aimScopingState)
			float unSightState = 1.0-AimDownSightStateSmooth;
			mat = CreateEulerAnglesMatrix(recoilRot*unSightState) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset*unSightState);
			
			// default offset from when the player is not aiming (i.e. default position)
			Vector3 trans = Mix( Vector3(-0.13, 0.5,0.2), Vector3(0.0, 0.3, -(0.-pivot.z)*globalScale), AimDownSightStateSmooth);

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue * unSightState;
			trans.y += cg_viewWeaponY.FloatValue * unSightState;
			trans.z += cg_viewWeaponZ.FloatValue * unSightState;
			trans.x *= abs(weapSide);

			mat = CreateTranslateMatrix(trans) * mat;

			// offset from when the player is walking
			// again, don't move the gun when the weapon is aimed
			mat = CreateTranslateMatrix(swing * GetMotionGain() * unSightState) * mat;

			// twist the gun when strafing
			// don't rotate when scoped
			mat = mat * CreateEulerAnglesMatrix(Vector3(0.0, 2.0*swing.x, 0.0)*unSightState);

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		// IWeaponSkin3 (override BasicViewWeapon::{get_MuzzlePosition, get_CaseEjectPosition})
		// attachments are in the units of the hand offsets: scale 0.02, shifted 5 forward
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * ((muzzleAttachment + Vector3(0.0F, 5.0F, 0.0F)) * 0.02F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * ((caseEjectAttachment + Vector3(0.0F, 5.0F, 0.0F)) * 0.02F); } }
		
//ADD TO SCENE
		void AddToScene()
		{
			// the png scope replaces the gun while aiming
			if (cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				RightHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				return;
			}

			Matrix4 mat = CreateScaleMatrix(0.020f)  * CreateTranslateMatrix(0.0, 5.0, 0.0);
			mat = GetViewWeaponMatrix() * mat;
			
			bool reloading = IsReloading;
			float reload = ReloadProgress*0.5f;
			Vector3 leftHand, rightHand;
			
			Vector3 leftHand2 = mat * Vector3(1.f, -9.f, 0.f); //-10
			Vector3 leftHand3 = mat * Vector3(-5.f, 2.f, 0.f); //1
			Vector3 leftHand4 = mat * Vector3(0.f, 4.f, 4.f); //1
			Vector3 leftHando = mat * Vector3(0.f, 9.f, 3.f); //1
			//CTranslateM	(x,y,z)		(x+left/-right, y+further/-closer, z+down/-up)
			
			if(AimDownSightStateSmooth > 0.8f)
			{
				mat = AdjustToAlignSight(mat, Vector3(0.f, 2.0f, -10.4f),
				(AimDownSightStateSmooth - 0.8f) / 0.2f);
			}
		
			// a shell starts when a reload starts, or when the progress restarts for the next one
			if (reloading and (not wasReloading or ReloadProgress < lastReloadProgress))
			{
				shellStarted = true;
				shellIsFirst = not loadPoseHeld;
				shellIsLast = ammo + 1 >= clipSize;
				if (shellIsFirst)
					reloadtype = (ammo == 0) ? 0 : 1; // an empty gun is pumped after its last shell
				loadPoseHeld = not shellIsLast;
			}
			// a reload that ends with the gun still turned turns it back from here
			if (not reloading and wasReloading and loadPoseHeld)
				returnStart = ReloadProgress;
			wasReloading = reloading;
			lastReloadProgress = ReloadProgress;

//SHELL INSERTION TYPE
//if this is the first and last shell
			//in this cycle everything must be HALVED
			if (!reloading && loadPoseHeld) // the reload ended with the gun still turned: turn it back
			{
				float per = Clamp((ReloadProgress - returnStart) / 0.34f, 0.0f, 1.0f); // as long as the last shell's return
				per = quadraticInOut(per);
				mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.0, -9.0) , Vector3(0.0, 0.0, 0.0), per ));
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(0.0, 0.0, 0.0), per ));
				if (per >= 1.0f)
					loadPoseHeld = false;
			}
			else if (shellStarted && shellIsFirst && shellIsLast && reload < 0.53f)
			{
				if(reload < 0.2f) //grab new bullet
				{
					float per = reload / 0.2f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 1.0, 0.0), per ));
					
				}
				else if(reload < 0.35f) //insert bullet
				{
					float per = (reload - 0.2f) / 0.15f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 1.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0.0, 1.0, 0.0) , Vector3(0.0, 1.0, 0.0), per ));
				}
				else if(reload < 0.53f) //move hand to original
				{
					float per = (reload - 0.35f) / 0.17f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(0.0, 1.0, 0.0), Vector3(0.0, 0.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0.0, 1.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
				}
			}
//reload animation of 1st shell
			//the weapon rotation must be slower for 1st time
			else if (shellStarted && shellIsFirst && reload < 0.52) //if no shells have been inserted yet
			{
				if(reload < 0.2f) //grab new bullet
				{
					float per = reload / 0.2f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(-10.0, 5.0, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(-1.5, 0.2, -1.8), per ));
					
				}
				else if(reload < 0.35f) //insert bullet
				{
					float per = (reload - 0.2f) / 0.15f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.0, -9.0) , Vector3(-10.0, 5.3, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(-1.3, 0.2, -1.8), per ));
				}
				else if(reload < 0.53f) //move hand to original
				{
					float per = (reload - 0.35f) / 0.17f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.3, -9.0) , Vector3(-10.0, 5.0, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.3, 0.2, -1.8) , Vector3(-1.5, 0.2, -1.8), per ));
				}
			}
//reload animation of other shells
			//if shells have been inserted but its not full yet
			else if (shellStarted && !shellIsLast && reload < 0.52)
			{
				if(reload < 0.2f) //grab new bullet
				{
					float per = reload / 0.2f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.0, -9.0) , Vector3(-10.0, 5.3, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(-1.3, 0.2, -1.8), per ));
					
				}
				else if(reload < 0.35f) //insert bullet
				{
					float per = (reload - 0.2f) / 0.15f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.3, -9.0) , Vector3(-10.0, 5.0, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.3, 0.2, -1.8) , Vector3(-1.5, 0.2, -1.8), per ));
				}
				else if(reload < 0.53f) //move hand to original
				{
					float per = (reload - 0.35f) / 0.17f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.0, -9.0) , Vector3(-10.0, 5.0, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(-1.5, 0.2, -1.8), per ));
				}
			}
//reload animation of last shell
	//if reloading weapon with bullets in
			else if(shellStarted && shellIsLast && reload < 0.53 && reloadtype == 1)
			{
				if(reload < 0.2f) //grab new bullet
				{
				float per = reload / 0.2f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.0, -9.0) , Vector3(-10.0, 5.3, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(-1.3, 0.2, -1.8), per ));
				}
				else if(reload < 0.35f) //insert bullet
				{
					float per = (reload - 0.2f) / 0.15f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.3, -9.0) , Vector3(-10.0, 5.3, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.3, 0.2, -1.8) , Vector3(-1.5, 0.2, -1.8), per ));
				}
				else if(reload <= 0.52f) //move hand to original
				{
					float per = (reload - 0.35f) / 0.17f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.3, -9.0) , Vector3(0.0, 0.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(0.0, 0.0, 0.0), per ));
				}
			}
			//if reloading dry weapon
			else if(shellStarted && shellIsLast && reload < 0.53 && reloadtype == 0)
			{
				if(reload < 0.2f) //grab new bullet
				{
				float per = reload / 0.2f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.0, -9.0) , Vector3(-10.0, 5.3, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(-1.3, 0.2, -1.8), per ));
				}
				else if(reload < 0.35f) //insert bullet
				{
					float per = (reload - 0.2f) / 0.15f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.3, -9.0) , Vector3(-10.0, 5.3, -9.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.3, 0.2, -1.8) , Vector3(-1.5, 0.2, -1.8), per ));
				}
				else if(reload <= 0.52f) //move hand to original
				{
					float per = (reload - 0.35f) / 0.17f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(-10.0, 5.3, -9.0) , Vector3(0.0, 0.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1.5, 0.2, -1.8) , Vector3(0.0, 0.0, 0.0), per ));
				}
			}
		//reload animation of dry weapon (pump)
			if (reloadtype == 0)
			{
				if (reload < 0.52f)
				{
					//wait
				}
				else if (reload < 0.7f)
				{
					//moving
					float per = (reload - 0.52f)/0.18f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
				}
				else if (reload < 0.75f)
				{
					float per = (reload - 0.7f)/0.05f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
				}
				else if (reload < 1.0f)
				{
					float per = (reload - 0.75f)/0.25f;
					per = quadraticInOut(per);
					mat = mat * CreateTranslateMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
					mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(0.0, 0.0, 0.0), per ));
				}
			}

			if (!reloading && reload > 1.f && ammo > 0)
			{
				if (ReadyState < 0.1f)
				{
					//wait
				}
				else if (ReadyState < 0.5f)
				{
					float per = (ReadyState - 0.1f)/0.4f;
					per=quadraticInOut(per);
					mat *= CreateEulerAnglesMatrix(Mix(Vector3(0.0f,0.0f,0.0f),Vector3(0.1f,0.08f,0.05f),per));
					
				}
				else if (ReadyState < 0.7f)
				{
					float per = (ReadyState - 0.5f)/0.25f;
					per=quadraticInOut(per);
					mat *= CreateEulerAnglesMatrix(Mix(Vector3(0.1f,0.08f,0.05f),Vector3(0.15f,0.1f,0.07f),per));
					
				}
				else if (ReadyState < 0.75f)
				{
					float per = (ReadyState - 0.7f)/0.05f;
					per=quadraticInOut(per);
		
					mat *= CreateEulerAnglesMatrix(Mix(Vector3(0.15f,0.1f,0.07f),Vector3(0.15f,0.1f,0.07f),per));

					
				}
				else if (ReadyState < 1.f)
				{
					float per = (ReadyState - 0.75f)/0.25f;
					per=quadraticInOut(per);
					mat *= CreateEulerAnglesMatrix(Mix(Vector3(0.15f,0.1f,0.07f),Vector3(0.0f,0.0f,0.0f),per));
				}
			}


			//(x,y,z) (x+left/-right, y+further/-closer, z+down/-up)
			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.customColor = RGB(n_rgbSpeed.FloatValue);
			param.matrix = weapMatrix * CreateScaleMatrix(0.5f);
			param.depthHack = true;
			renderer.AddModel(gunModel, param);
			
			/*if (ReadyState < 0.5)
			{
				param.matrix = weapMatrix * CreateScaleMatrix(0.5f);
				param.depthHack = true;
				renderer.AddModel(trueModel, param);
			}*/
			
			// draw sights
			Matrix4 sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(0.0f, 8.0f, -10.89f);
			sightMat *= CreateScaleMatrix(0.26f);
			param.matrix = sightMat;
			renderer.AddModel(sightModel2, param); // rear

			// draw reflex sight (3D sprite)
			int reflexMode = cg_reflexScope.IntValue;
			if (reflexMode < 3 and not cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.8F) {
				// the sight's own reticle is a dot: modes 0 and 1 draw it, mode 2 draws the reflex reticle
				bool dotReflex = reflexMode != 2;
				float reflexSize = dotReflex ? 0.02F : 0.2F;
				DrawReflexSight3D(dotReflex ? ballImage : reflexImage, weapMatrix * sightLensAttachment, reflexSize);
			}
			
			
			// reload action
			leftHand = mat * Vector3(0.f, 9.f, 2.f);
			rightHand = mat * Vector3(0.f, -20.f, 2.f);			
			
			//leftHand animation - inserting shells
			//good for now
			if(reloading) 
			{
				if(reload < 0.2f) //grab new bullet
				{
					float per = reload / 0.2f;
					per=quadraticInOut(per);
					leftHand = Mix(leftHand, leftHand2,per);
						
				}
				else if(reload < 0.35f) //insert bullet
				{
					float per = (reload - 0.2f) / 0.15f;
					per=quadraticInOut(per);
					leftHand = Mix(leftHand2, leftHand3,per);
				}
				else if(reload < 0.5f) //move hand to original
				{
					float per = (reload - 0.35f) / 0.15f;
					per=quadraticInOut(per);
					leftHand = Mix(leftHand3, leftHand,per);
				}
			}
			
			//BASIC PUMP ANIMATION
			param.matrix = weapMatrix * CreateScaleMatrix(0.5f);
			param.depthHack = true;	
			if (reloadtype == 0 && reload < 1.0f)
			{
				if (reload < 0.55f)
				{
					//wait
				}
				else if (reload < 0.60f)
				{
					float per = (reload - 0.55f) / 0.05f;
					per=quadraticInOut(per);
					param.matrix *= CreateTranslateMatrix(Mix (Vector3(0.0f, 0.0f, 0.0f), Vector3(0.f, 0.f, 0.f), per));
					leftHand = leftHand;
				}
				else if (reload < 0.75f)
				{
					float per = (reload -0.60f) / 0.15f;
					per=quadraticInOut(per);
					param.matrix *= CreateTranslateMatrix(Mix(Vector3(0.f, 0.f, 0.f),Vector3 (0.f, -7.f, 0.f), per));
					leftHand = Mix(leftHand,leftHand4,per);
				}
				else if (reload < 0.9f)
				{
					float per = (reload - 0.75f) / 0.15f;
					per=quadraticInOut(per);
					param.matrix *= CreateTranslateMatrix(Mix (Vector3(0.f, -7.f, 0.f), Vector3 (0.f, 0.f, 0.f),per));
					leftHand = Mix(leftHand4,leftHando,per);
				}
				else if (!reloading) // pumped: the next reload decides again
				{
					reloadtype = 1;
				}
			}			
				//.10 - .50 move
				//.50 - .70 pump
				//.50 - .75 move down, stop
				//.75 - 1.0 pump & move orig
			
			else if (!reloading && reload > 1.5f && ammo > 0)
			{
				if (ReadyState < 0.55f)
				{
					//wait
				}
				else if (ReadyState < 0.60f)
				{
					float per = (ReadyState - 0.55f) / 0.05f;
					per=quadraticInOut(per);
					param.matrix *= CreateTranslateMatrix(Mix (Vector3(0.f, 0.f, 0.f), Vector3 (0.f, 0.f, 0.f),per));
					leftHand = leftHand;
				}
				else if (ReadyState < 0.75f)
				{
				    float per = (ReadyState -0.60f) / 0.15f;
					per=quadraticInOut(per);
					param.matrix *= CreateTranslateMatrix(Mix (Vector3(0.f, 0.f, 0.f), Vector3 (0.f, -7.f, 0.f),per));
					leftHand = Mix(leftHand,leftHand4,per);
				}
				else if (ReadyState < 0.9f)
				{
					float per = (ReadyState - 0.75f) / 0.15f;
					per=quadraticInOut(per);
					param.matrix *= CreateTranslateMatrix(Mix (Vector3(0.f, -7.f, 0.f), Vector3 (0.f, 0.f, 0.f),per));
					leftHand = Mix(leftHand4,leftHando,per);
				}
			}	
			renderer.AddModel(pumpModel, param);
			//(x,y,z) (x+left/-right, y+further/-closer, z+down/-up)

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

				ModelRenderParam charmParam;
				charmParam.depthHack = true;

				// rainbow color
				charmParam.customColor = HSV(time * 0.05F, 0.5F, 1.0F);

				// draw base; hand units are 0.02, so scale up to the stock charm size (0.1 at 0.033)
				charmParam.matrix = weapMatrix
					* CreateTranslateMatrix(charmAttachment)
					* CreateScaleMatrix(0.165F);
				renderer.AddModel(charmBaseModel, charmParam);

				charmParam.matrix = charmParam.matrix
					* CreateTranslateMatrix(-0.75F, -1.0F, 1.25F)
					* CreateEulerAnglesMatrix(charmRot)
					* CreateEulerAnglesMatrix(Vector3(rad(90.0F), 0, rad(-90.0F)));
				renderer.AddModel(charmModel, charmParam);
			}

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}
	
}
