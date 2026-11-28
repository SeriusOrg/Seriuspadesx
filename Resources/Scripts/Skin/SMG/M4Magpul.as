/*
 Copyright (c) 2013 OpenSpades Developers
 Modified by Paratrooper 2015

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 ( at your option ) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http:// www.gnu.org/licenses/>.

 */

// Tower Defense dress: the mod's own view skin, adapted. The class
// is picked by the store's word (the factory in this weapon's stock
// View.as reads the tier); every file it loads is spelled with the
// dress's pak folder in front.

namespace spades {
	class ViewSMGM4Skin : BasicViewWeapon {
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ chargingHandleModel;
		private Model@ magazineEmptyModel;
		private Model@ magazineLoadedModel;
		private Model@ sightsFrontModel;
		private Model@ sightsPinModel;
		private Model@ sightsReflexModel;

		private AudioChunk@[] fireSounds( 4 );
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ reloadSound;

		// some important numbers
		private float globalScale = 0.008f;											// scale of the weapon matrix
		private Vector3 weaponPivot = Vector3( 4.5f, 40.f, 17.f );					// pivot of the weapon if viewed from slab6
		private Vector3 sightOffset = Vector3( 4.5f, 98.f, -5.5f ) - weaponPivot;	// the position of the front sight with respect to the pivot of the gun
		private float shootRand1 = GetRandom();										// random number every time the gun is fired, used for the random rotation in the Y-axis due to recoil
		private float shootRand2 = GetRandom();										// random number every time the gun is fired, used for the random Y-axis translation due to recoil

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		ViewSMGM4Skin( Renderer@ r, AudioDevice@ dev ) {
			super( r );
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				( "WeaponMods/SMGLvl3/Models/Weapons/SMG/WeaponNoMagazine.kv6" );
			@chargingHandleModel = renderer.RegisterModel
				( "WeaponMods/SMGLvl3/Models/Weapons/SMG/ChargingHandle.kv6" );
			@magazineEmptyModel = renderer.RegisterModel
				( "WeaponMods/SMGLvl3/Models/Weapons/SMG/MagazineEmpty.kv6" );
			@magazineLoadedModel = renderer.RegisterModel
				( "WeaponMods/SMGLvl3/Models/Weapons/SMG/Magazine.kv6" );
			@sightsFrontModel = renderer.RegisterModel
				( "WeaponMods/SMGLvl3/Models/Weapons/SMG/SightsFront.kv6" );
			@sightsPinModel = renderer.RegisterModel
				( "WeaponMods/SMGLvl3/Models/Weapons/SMG/SightsPin.kv6" );
			@sightsReflexModel = renderer.RegisterModel
				( "WeaponMods/SMGLvl3/Models/Weapons/SMG/SightsReflex.kv6" );

			@fireSounds[0] = dev.RegisterSound
				( "WeaponMods/SMGLvl3/Sounds/Weapons/SMG/V2Local1.opus" );
			@fireSounds[1] = dev.RegisterSound
				( "WeaponMods/SMGLvl3/Sounds/Weapons/SMG/V2Local2.opus" );
			@fireSounds[2] = dev.RegisterSound
				( "WeaponMods/SMGLvl3/Sounds/Weapons/SMG/V2Local3.opus" );
			@fireSounds[3] = dev.RegisterSound
				( "WeaponMods/SMGLvl3/Sounds/Weapons/SMG/V2Local4.opus" );
			@fireFarSound = dev.RegisterSound
				( "WeaponMods/SMGLvl3/Sounds/Weapons/SMG/FireFar.opus" );
			@fireStereoSound = dev.RegisterSound
				( "WeaponMods/SMGLvl3/Sounds/Weapons/SMG/FireStereo.opus" );
			@reloadSound = dev.RegisterSound
				( "WeaponMods/SMGLvl3/Sounds/Weapons/SMG/ReloadLocal.opus" );

			@reflexImage = renderer.RegisterImage
				( "WeaponMods/SMGLvl3/Gfx/ReflexRound.png" );

			@scopeImage = renderer.RegisterImage("Gfx/SMG.png");

			charmRaiseSpring.position = 1;
		}

		void Update( float dt ) {
			// dTime = dt;
			BasicViewWeapon::Update( dt );

			bool isSprinting = sprintState >= 1 or sprintState > lastSprintState;
			bool isRaised = raiseState >= 1 or raiseState > lastRaiseState;

			charmSprintSpring.desired = isSprinting ? 1 : 0;
			charmRaiseSpring.desired = isRaised ? 0 : 1;

			lastSprintState = sprintState;
			lastRaiseState = raiseState;
		}

		void WeaponFired() {
			BasicViewWeapon::WeaponFired();

			shootRand1 = Mix( shootRand1, GetRandom(), 0.6f );
			shootRand2 = GetRandom();

			if ( !IsMuted ) {
				Vector3 origin = Vector3( 0.4f, -0.3f, 0.5f );
				AudioParam param;
				param.volume = 8.f;
				audioDevice.PlayLocal( fireSounds[GetRandom( fireSounds.length )], origin, param );

				param.volume = 4.f;
				audioDevice.PlayLocal( fireFarSound, origin, param );
				param.volume = 1.f;
				audioDevice.PlayLocal( fireStereoSound, origin, param );
			}

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;
		}

		void ReloadingWeapon() {
			if ( !IsMuted ) {
				Vector3 origin = Vector3( 0.4f, -0.3f, 0.5f );
				AudioParam param;
				param.volume = 0.2f;
				audioDevice.PlayLocal( reloadSound, origin, param );
			}

			charmHorizontalSwingSpring.velocity += 0.1F;
			charmVerticalSwingSpring.velocity += 0.1F;
		}

		Matrix4 GetShootRecoilMatrix() {
			Matrix4 mat;
			float per = GetLocalFireVibration(); // jumps to 1 immediately after firing, down to 0 after cooling down
			float peak = 0.8; // peak of the recoil anim
			per /= ( per > peak ) ? ( 1 - peak ) : ( peak );

			per = sin( per * PiF * 0.5f ); // ditto, but with some sin easing
			mat *= CreateEulerAnglesMatrix( Vector3( 0.f, rad( ( shootRand1 - 0.5f ) * 2.f * 3.f ) , 0.f ) * per );
			mat *= CreateTranslateMatrix( Vector3( 0.f, -1 - shootRand2/2, 0.f ) * 0.01 * per );
			Vector3 tilt = Vector3( rad( -1.f ), 0.f, 0.f );

			mat *= CreateEulerAnglesMatrix( tilt * per );
			return mat;
		}

		Matrix4 GetMovementSwingMatrix() {
			Matrix4 mat;
			mat *= CreateEulerAnglesMatrix( Vector3( rad( 10.f ), 0.f, 0.f ) * swing.z/( 0.05f ));
			mat *= CreateTranslateMatrix( Vector3( swing.x, swing.y/2, 0 ));
			//mat *= CreateEulerAnglesMatrix( Vector3( 0.f, rad( 10.f ) * swing.x/( 0.05f ), 0.f ));
			return mat;
		}

		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			float sp = 1.0F - AimDownSightStateSmooth;

			mat *= GetShootRecoilMatrix();
			mat *= GetMovementSwingMatrix();

			if (sprintState > 0.0F) {
				float per = sprintState;
				per *= per;
				mat *= CreateEulerAnglesMatrix( Vector3( 0.f, 0.f, rad( -30.f )) * per );
				mat *= CreateEulerAnglesMatrix( Vector3( rad( 10.f ), 0.f, 0.f ) * per );
			}

			if (raiseState < 1.0F) {
				float putdown = 1.f - raiseState;
				putdown = putdown * putdown;
				mat *= CreateEulerAnglesMatrix( Vector3( 0.f, 0.f, rad( -30.f )) * putdown );
				mat *= CreateEulerAnglesMatrix( Vector3( rad( 80.f ), 0.f, 0.f ) * putdown );
			}

			// add weapon offset
			Vector3 trans(0, 0, 0);

			if (cg_trueAimDownSight.BoolValue and cg_pngScope.BoolValue)
				trans += Vector3(Mix(-0.2F, -0.1F, sp), 0.2F * sp, Mix(0.3F, 0.2F, sp));
			else
				trans += Vector3(-0.1F * sp, Mix(0.1F, 0.2F, sp), GetZPos(-sightOffset.z * globalScale));

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue * sp;
			trans.y += cg_viewWeaponY.FloatValue * sp;
			trans.z += cg_viewWeaponZ.FloatValue * sp;
			trans.x *= abs(weapSide);

			mat *= CreateTranslateMatrix(trans);

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		// IWeaponSkin3 (override BasicViewWeapon::{get_MuzzlePosition, get_CaseEjectPosition})
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 0.65F, -0.085F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 0.15F, -0.075F); } }

		void Draw2D() {
			// draw reflex sight (2D image)
			if (cg_reflexScope.IntValue >= 3
				and not cg_pngScope.BoolValue
				and AimDownSightStateSmooth > 0.8F) {
				DrawReflexSight2D(GetViewWeaponMatrix(), 0.004F);
				return;
			}

			BasicViewWeapon::Draw2D();
		}

		Vector3 V3QuickMix( Vector3 v0, Vector3 v1, float current, float start, float end ) {
			return Mix( v0, v1, SmoothStep( ( current-start ) / ( end-start ) ) );
		}
		void AddToScene() {
			if (cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				RightHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				return;
			}

			bool reloading = IsReloading;
			float reload = ReloadProgress;
			reload *= 2.5F;

			Vector3 weaponRot = Vector3(0.0F, 0.0F, 0.0F);

			// protip: jostle the y-axis and z-axis realistically a bit every frame you get to make it look realistic
			Vector3[] rotTimeline = {
				weaponRot, // start at default pos
				Vector3( rad( -8.f ) , rad( 10.f ), 0.f ), // lhand goes to mag
				Vector3( rad( -24.f ) , rad( 20.f ), 0.f ), // lhand pulls mag out of the well and goes down
				Vector3( rad( -16.f ), rad( 20.f ), 0.f ), // lhand stays down for a bit
				Vector3( rad( -8.f ), rad( 18.f ), 0.f ), // lhand rises up with new mag to the well
				Vector3( rad( -16.f ), rad( 18.f ), 0.f ), // lhand pulls down slightly
				Vector3( rad( -12.f ), rad( 18.f ), 0.f ), // lhand sharply taps mag in place
				Vector3( rad( -16.f ), rad( 14.f ), 0.f ), // lhand keeps hand on mag
				Vector3( rad( -20.f ), rad( 10.f ), 0.f ), // lhand moves to chaging handle
				Vector3( rad( -24.f ), rad( 6.f ), 0.f ), // lhand keeps hand at charging handle for a bit
				Vector3( rad( -28.f ), rad( 8.f ), 0.f ), // lhand pulls charging handle back
				Vector3( rad( -28.f ), rad( 6.f ), 0.f ), // lhand keeps charging handle back
				Vector3( rad( -12.f ), rad( 4.f ), 0.f ), // lhand guides charging hande back in place
				weaponRot // return to default pos
			};

			if (reloading) {
				if ( reload < 0.1f )
					weaponRot = V3QuickMix( rotTimeline[0], rotTimeline[1], reload, 0.f, 0.1f );
				else if ( reload < 0.4 )
					weaponRot = V3QuickMix( rotTimeline[1], rotTimeline[2], reload, 0.1f, 0.4f );
				else if ( reload < 0.7 )
					weaponRot = V3QuickMix( rotTimeline[2], rotTimeline[3], reload, 0.4f, 0.7f );
				else if ( reload < 1.0 )
					weaponRot = V3QuickMix( rotTimeline[3], rotTimeline[4], reload, 0.7f, 1.0f );
				else if ( reload < 1.2 )
					weaponRot = V3QuickMix( rotTimeline[4], rotTimeline[5], reload, 1.0f, 1.2f );
				else if ( reload < 1.25 )
					weaponRot = V3QuickMix( rotTimeline[5], rotTimeline[6], reload, 1.2f, 1.25f );
				else if ( reload < 1.4 )
					weaponRot = V3QuickMix( rotTimeline[6], rotTimeline[7], reload, 1.25f, 1.4f );
				else if ( reload < 1.7 )
					weaponRot = V3QuickMix( rotTimeline[7], rotTimeline[8], reload, 1.4f, 1.7f );
				else if ( reload < 1.8 )
					weaponRot = V3QuickMix( rotTimeline[8], rotTimeline[9], reload, 1.7f, 1.8f );
				else if ( reload < 2.0 )
					weaponRot = V3QuickMix( rotTimeline[9], rotTimeline[10], reload, 1.8f, 2.0f );
				else if ( reload < 2.1 )
					weaponRot = V3QuickMix( rotTimeline[10], rotTimeline[11], reload, 2.0f, 2.1f );
				else if ( reload < 2.3 )
					weaponRot = V3QuickMix( rotTimeline[11], rotTimeline[12], reload, 2.1f, 2.3f );
				else if ( reload < 2.5 )
					weaponRot = V3QuickMix( rotTimeline[12], rotTimeline[13], reload, 2.3f, 2.5f );
				else // if ( reload >= 2.5 )
					weaponRot = rotTimeline[0];
			}

			Matrix4 mat = GetViewWeaponMatrix()
				* CreateEulerAnglesMatrix(weaponRot)
				* CreateScaleMatrix(globalScale);

			if ((cg_trueAimDownSight.BoolValue and not cg_pngScope.BoolValue) and AimDownSightStateSmooth > 0.0F)
				mat = AdjustToAlignSight(mat, sightOffset, AimDownSightStateSmooth);

			Vector3 leftHand, rightHand;
			leftHand = mat * ( Vector3( 7.f, 94.f, 24.f ) - weaponPivot );
			rightHand = mat * ( Vector3( 4., 34.f, 26.f ) - weaponPivot );

			Vector3[] lhTimeline = {
				leftHand, // start at default pos
				mat * ( Vector3( 7.5f, 86.f, 57.f ) - weaponPivot ), // lhand goes to mag
				mat * ( Vector3( 7.5f, 60.f, 140.f ) - weaponPivot ), // lhand pulls mag out of the well and goes down
				mat * ( Vector3( 7.5f, 60.f, 140.f ) - weaponPivot ), // lhand stays down for a bit
				mat * ( Vector3( 7.5f, 86.f, 57.f ) - weaponPivot ), // lhand rises up with new mag to the well
				mat * ( Vector3( 5.5f, 82.f, 79.f ) - weaponPivot ), // lhand pulls down slightly
				mat * ( Vector3( 5.5f, 82.f, 61.f ) - weaponPivot ), // lhand sharply taps mag in place
				mat * ( Vector3( 5.5f, 82.f, 61.f ) - weaponPivot ), // lhand keeps hand on mag
				mat * ( Vector3( 18.f, 74.f, 12.f ) - weaponPivot ), // lhand moves to chaging handle
				mat * ( Vector3( 18.f, 74.f, 12.f ) - weaponPivot ), // lhand keeps hand at charging handle for a bit
				mat * ( Vector3( 18.f, 60.f, 12.f ) - weaponPivot ), // lhand pulls charging handle back
				mat * ( Vector3( 18.f, 60.f, 12.f ) - weaponPivot ), // lhand keeps charging handle back
				mat * ( Vector3( 18.f, 74.f, 12.f ) - weaponPivot ), // lhand guides charging hande back in place
				leftHand // return to default pos
			};

			if ( reloading ) {
				if ( reload < 0.1f ) {
					charmVerticalSwingSpring.desired = -0.01F;
					leftHand = V3QuickMix( lhTimeline[0], lhTimeline[1], reload, 0.f, 0.1f );}
				else if ( reload < 0.4 ) {
					charmVerticalSwingSpring.velocity += 0.02F;
					leftHand = V3QuickMix( lhTimeline[1], lhTimeline[2], reload, 0.1f, 0.4f );}
				else if ( reload < 0.7 ) {
					leftHand = V3QuickMix( lhTimeline[2], lhTimeline[3], reload, 0.4f, 0.7f );}
				else if ( reload < 1.0 ) {
					leftHand = V3QuickMix( lhTimeline[3], lhTimeline[4], reload, 0.7f, 1.0f );}
				else if ( reload < 1.2 ) {
					charmVerticalSwingSpring.velocity += 0.02F;
					leftHand = V3QuickMix( lhTimeline[4], lhTimeline[5], reload, 1.0f, 1.2f );}
				else if ( reload < 1.25 ) {
					leftHand = V3QuickMix( lhTimeline[5], lhTimeline[6], reload, 1.2f, 1.25f );}
				else if ( reload < 1.4 ) {
					leftHand = V3QuickMix( lhTimeline[6], lhTimeline[7], reload, 1.25f, 1.4f );}
				else if ( reload < 1.7 ) {
					leftHand = V3QuickMix( lhTimeline[7], lhTimeline[8], reload, 1.4f, 1.7f );}
				else if ( reload < 1.8 ) {
					leftHand = V3QuickMix( lhTimeline[8], lhTimeline[9], reload, 1.7f, 1.8f );}
				else if ( reload < 2.0 ) {
					charmVerticalSwingSpring.velocity += 0.02F;
					leftHand = V3QuickMix( lhTimeline[9], lhTimeline[10], reload, 1.8f, 2.0f );}
				else if ( reload < 2.1 ) {
					charmVerticalSwingSpring.velocity += 0.02F;
					leftHand = V3QuickMix( lhTimeline[10], lhTimeline[11], reload, 2.0f, 2.1f );}
				else if ( reload < 2.3 ) {
					leftHand = V3QuickMix( lhTimeline[11], lhTimeline[12], reload, 2.1f, 2.3f );}
				else if ( reload < 2.5 ) {
					charmVerticalSwingSpring.desired = 0;
					leftHand = V3QuickMix( lhTimeline[12], lhTimeline[13], reload, 2.3f, 2.5f );}
				else { // if ( reload >= 2.5 )
					charmVerticalSwingSpring.desired = 0;
					leftHand = lhTimeline[0];}
			}

			// Model Rendering
			// draw weapon body
			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix;
			param.depthHack = true;
			renderer.AddModel( gunModel, param );

			// draw sights
			// front + barrel
			Matrix4 alignMat = weapMatrix;
			alignMat *= CreateTranslateMatrix( Vector3( 4.5f, 105.f, -4.0f ) - weaponPivot );
			alignMat *= CreateScaleMatrix( 0.25f );
			param.matrix = alignMat;
			renderer.AddModel( sightsFrontModel, param );

			// pin
			alignMat *= CreateScaleMatrix( 0.25f );
			param.matrix = alignMat;
			renderer.AddModel( sightsPinModel, param );

			// reflex
			alignMat = weapMatrix;
			alignMat *= CreateTranslateMatrix( Vector3( 4.5f, 60.f, -5.5f ) - weaponPivot );
			alignMat *= CreateScaleMatrix( 0.125f );
			param.matrix = alignMat;
			renderer.AddModel( sightsReflexModel, param );

			// draw charging handle
			alignMat = weapMatrix;
			float charging = 0.f;
			float ready = ReadyState;

			// charging sequence
			if ( !reloading ) {
				if ( ready >= 0.8f ) {
					charging = 0.f;
				}
				else if ( ready < 0.3f ) {
					charging = ready * 1.f/0.3f;
					charging = ( ready/0.3f ) * ( ready/0.3f - 2 ) * -1.f; // quad out
				}
				else {// if ( ready < 0.8f )
					charging = 1.f - ( ready - 0.3f ) * 1.f/0.5f; // linear
				}
			}
			else {
				if ( reload < 1.8f ) {
					charging = 0.f;
				}
				else if ( reload < 2.0f ) {
					charging = ( reload - 1.8f ) / ( 2.0f - 1.8f );
				}
				else if ( reload < 2.1f ) {
					charging = 1.f;
				}
				else if ( reload < 2.3f ) {
					charging = 1.f - ( ( reload - 2.1f ) / ( 2.3f - 2.1f ));
				}
				else
				{
					charging = 0.f;
				}
			}

			alignMat *= CreateTranslateMatrix( Vector3( 6.5f, 62.5f, 5.5f ) - weaponPivot );
			alignMat *= CreateTranslateMatrix( 0.f, -11.9f * charging, -0.0f );
			alignMat *= CreateScaleMatrix( 0.25f );
			param.matrix = alignMat;
			renderer.AddModel( chargingHandleModel, param ); // charging handle

			// magazine
			alignMat = weapMatrix;
			alignMat *= CreateTranslateMatrix( Vector3( 4.5f, 59.f, 14.5f ) - weaponPivot );

			// magazine sequence
			float magy = 0.f;
			float magz = 0.f;
			if ( reloading ) {
				if ( reload < 0.1f ) {
					magz = 0.f;
					magz = magy;
				}
				else if ( reload < 0.4f ) {
					magy = ( reload - 0.1f ) / ( 0.4f - 0.1f );
					magz = magy;
				}
				else if ( reload < 0.7f ) {
					magy = 1.f;
					magz = magy;
				}
				else if ( reload < 1.f ) {
					magy = 1.f - ( ( reload - 0.7f ) / ( 1.0f - 0.7f ));
					magz = 1.f - ( ( reload - 0.7f ) / ( 1.0f - 0.7f ) * 47.f/50.f );
				}
				else if ( reload < 1.2f ) {
					magy = 0.f;
					magz = 1.f - 47.f/50.f;
				}
				else if ( reload < 1.25 ) {
					magy = 0;
					magz = 1.f - ( 47.f/50.f + ( reload - 1.2f ) / ( 1.25f - 1.2f ) * 3.f/50.f );
				}
				else {
					magy = 0.f;
					magz = 0.f;
				}
			}

			alignMat *= CreateTranslateMatrix( Vector3( 0.f, -44.f * magy, 50.f * magz ));
			alignMat *= CreateScaleMatrix( 0.48f ); // just a bit below 0.5 because it overlaps with the gun model otherwise
			param.matrix = alignMat;
			if ( reloading ) {
				if ( reload < 0.7f )
					renderer.AddModel( magazineEmptyModel, param );
				else if ( reload < 1.f )
					renderer.AddModel( magazineLoadedModel, param );
				else
					renderer.AddModel( magazineEmptyModel, param );
			}
			else
				renderer.AddModel( magazineEmptyModel, param );

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
					* CreateTranslateMatrix(5.5F, 30.0F, -11.0F)
					* CreateScaleMatrix(0.33F);
				renderer.AddModel(charmBaseModel, param);

				param.matrix = param.matrix
					* CreateTranslateMatrix(-0.75F, -1.0F, 1.25F)
					* CreateEulerAnglesMatrix(charmRot)
					* CreateEulerAnglesMatrix(Vector3(rad(90.0F), 0, rad(-90.0F)));
				renderer.AddModel(charmModel, param);
			}

			// draw reflex sight (3D sprite)
			int reflexMode = cg_reflexScope.IntValue;
			if (reflexMode < 3 and not cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.8F) {
				bool dotReflex = reflexMode == 1;
				float reflexSize = dotReflex ? 0.04F : 0.4F;
				DrawReflexSight3D(dotReflex ? ballImage : reflexImage, weapMatrix * sightOffset, reflexSize);
			}

			// hands
			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}

}
/*
array<float> reloadTimeline = {0.f,
			0.1f, // lhand goes to mag
			0.4f, // lhand pulls mag out of the well and goes down
			0.7f, // lhand stays down for a bit
			1.0f, // lhand rises up with new mag to the well
			1.2f, // lhand pulls down slightly
			1.25f, // lhand sharply taps mag in place
			1.4f, // lhand keeps hand on mag
			1.7f, // lhand moves to chaging handle
			1.8f, // lhand keeps hand at charging handle for a bit
			2.0f, // lhand pulls charging handle back
			2.1f, // lhand keeps charging handle back
			2.3f, // lhand guides charging hande back in place
			2.5f}; // return to normal
*/

/* if ( reloading ) {
	for ( uint i = 1; i < reloadTimeline.length; i++ ) {
		if ( reload < reloadTimeline[i] ) {
			float per = ( reload - reloadTimeline[i - 1] ) / ( reloadTimeline[i] - reloadTimeline[i - 1] );
			weaponRot = Mix( rotTimeline[i - 1], rotTimeline[i], SmoothStep( per ));
			break;
		}
	}
} */
