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
	class ViewRPGSkin : BasicViewWeapon, IGrenadeSkin {
		private AudioDevice@ audioDevice;

		private Model@ gunModel;
		private Model@ rocketModel;
		private Model@ sightFrontModel;
		private Model@ sightRearModel;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;

		// ---- Tunables ------------------------------------------------------

		// OVERALL WEAPON SCALE (the single knob to adjust the RPG size).
		// 0.045 = original tuned value, 0.03 = scale pass
		// (10 nov. 2026), 0.026 = refinement pass (11 nov. 2026,
		// player feedback: the weapon read too big in first person),
		// 0.0221 = 15 percent down (28 nov. 2026, player feedback:
		// the RPG and its rocket read too big).
		// Everything that expresses the weapon and its anchors in
		// MODEL UNITS scales together with it:
		//   - the weapon body and the loaded rocket (AddToScene)
		//   - the hand anchors (GetLeftHandOffset / GetRightHandOffset:
		//     hands are transformed by the scaled matrix, so they stay
		//     glued to the same point on the tube)
		//   - rocketSeat, insertDepth, aboveMuzzle, weaponBackOffset,
		//     weaponRaiseOffset, weaponOffsetTweak and the reload-pose
		//     translate (all model units)
		// NOT scaled (world units, in GetViewWeaponMatrix): the view
		// offsets `trans`, cg_viewWeaponX/Y/Z, the sway AND adsOffset.
		// NOTE: adsOffset is in world units, so after a scale change
		// the full-ADS stance may need a light re-tune (the sights
		// moved with the weapon, the ADS shift did not).
		private float globalScale = 0.0221F;

		// Seat of the loaded rocket at the muzzle (model units,
		// same axes as the weapon model: y forward, -Z is up).
		private Vector3 rocketSeat = Vector3(-0.25F, 42.9F, -5.5F);

		// Loaded/carried rocket scale
		private float rocketScale = 0.75F;

		// IRON SIGHTS (16 nov. 2026): SightFront.kv6 at the
		// muzzle, SightRear.kv6 at the eye end. FIXED, always
		// deployed (no folding). Same weapon space as the other
		// attachment constants (model units: y forward, -Z is up,
		// +X is left in view). Like rocketSeat, these are positions on
		// the UNRAISED body: the draw call applies the common
		// -weaponBackOffset / -weaponRaiseOffset shift, so the sights
		// follow the raised/pulled weapon automatically.
		// Reference points for tuning: the muzzle mouth is at
		// rocketSeat.y ~= 43, the rocket axis sits at z ~= -5.5, the
		// body centerline is x = 0. Start from there: raise the
		// sight = MORE NEGATIVE z, move it toward the muzzle =
		// LARGER y. Tune x only if the tube is off-center in view.
		// CALIBRATED IN GAME (17 nov. 2026): do not retune
		// blindly, these lock the sights on the tube visually.
		private Vector3 sightFrontSeat = Vector3(0.0F, 50.0F, -11.0F);
		private float sightFrontScale = 0.095F;
		private Vector3 sightRearSeat = Vector3(5.5F, 27.0F, -6.0F);
		private float sightRearScale = 0.075F;

		// Sight FLIP, per axis (16 nov. 2026): rotation in
		// DEGREES applied in the sight's own frame, AFTER modelFix:
		//   FlipPitch (X): tip nose up/down (use 180 if the sight
		//                  points backward)
		//   FlipYaw   (Y): upside down in place (current setup)
		//   FlipRoll  (Z): spin in the model plane
		// The seats do not move; combine freely (e.g. Pitch 180 +
		// Roll 180 also flips the model upside down, through the
		// other end). 0 = as authored.
		// CALIBRATED IN GAME (17 nov. 2026): Pitch 180 +
		// Yaw 180 on both sights.
		private float sightFrontFlipPitch = 180.0F;
		private float sightFrontFlipYaw = 180.0F;
		private float sightFrontFlipRoll = 0.0F;
		private float sightRearFlipPitch = 180.0F;
		private float sightRearFlipYaw = 180.0F;
		private float sightRearFlipRoll = 0.0F;

		// ---- PNG RETICLE (ADS, 11 nov. 2026) -------------------------------
		// 2D reticle drawn CENTERED on the screen by Draw2D, fading in
		// with the aim (full strength at ADS, gone at the hip).
		// The PNG lives in the pak, Gfx/ folder.
		private bool drawReticle = true;
		// path inside the pak (Gfx/ folder)
		private string reticlePath = "WeaponMods/GrenadeRPG/Gfx/RangeFinder.png";
		// on-screen size in pixels at full ADS. The image is drawn
		// WHOLE (no crop), its CENTER exactly on the screen center.
		private float reticleSize = 320.0F;
		// width/height aspect of the PNG (reticle PNG: 1920x1080
		// = 1.7778); set it to the image's real proportions to avoid
		// distortion
		private float reticleAspect = 1.7778F;
		// ADS level where the reticle STARTS fading in
		// (0 = appears as soon as the aim starts, 0.85 = only when
		// nearly fully aimed)
		private float reticleFadeStart = 0.5F;

		// ADS POSITION (17 nov. 2026): offset applied to the
		// whole weapon AND its hand anchors, IN VIEW SPACE (world
		// units, NOT scaled by globalScale), fading in with the aim
		// (AimDownSightStateSmooth: 0 = hip, 1 = full ADS). Tune the
		// full-ADS stance here; the hip stance is untouched.
		//   .x : lateral (+ = left in view, so RIGHT = negative)
		//   .y : forward/back (+ = toward the muzzle)
		//   .z : vertical (-Z is UP in view: POSITIVE z lowers
		//        the weapon in the frame, NEGATIVE z raises it)
		// CALIBRATED IN GAME (17 nov. 2026, dozens of
		// game-restart iterations): full-ADS stance locked.
		// NOTE (11 nov. 2026): calibrated for globalScale 0.03;
		// re-check after the 0.026 scale pass.
		private Vector3 adsOffset = Vector3(0.041F, -0.40F, 0.0611F);

		// NOTE: the flying rocket, its smoke trail and its exhaust
		// flame are handled ENGINE-SIDE by RocketEntity. The skin only
		// shows the loaded rocket at the muzzle and the reload
		// animation.

		// Extra offset applied to the whole weapon AND its hand anchors
		// (world units). 7 model voxels to the right. NOTE: in view space,
		// +X is LEFT, so the right shift is negative.
		private Vector3 weaponOffsetTweak =
			Vector3(-7.0F * globalScale, 0.0F, 0.0F);

		// Raise of the weapon body ONLY (model voxels): the hand anchors
		// stay where they are. -Z is up in view space (see "below view"
		// comment in GetLeftHandOffset)
		private float weaponRaiseOffset = 2.0F;

		// Pull the weapon body ONLY backward, toward the player
		// (model voxels along -Y). The hand anchors stay where they are,
		// like weaponRaiseOffset (10 nov. 2026: 27 units after
		// the scale pass).
		private float weaponBackOffset = 27.0F;

		// Reload timing (fractions of reloadProgress):
		//
		// rocketSeatProgress: the moment the rocket clips into the tube.
		// The left hand's downward insertion STOPS exactly at this
		// instant, and the seated rocket appears at the muzzle at the
		// same instant. One constant, three synchronized events.
		//
		// flipBackStart: the moment the weapon starts tipping back down
		// to firing position, BEFORE the server-side reload finishes.
		// This kills the dead time between the insertion and the return:
		// without it, the hand holds the tube mouth frozen during the
		// whole 0.65 -> 1.0 window (the last third of the server reload
		// duration). The left hand rides the tube down during the flip.
		private float rocketSeatProgress = 0.65F;
		private float flipBackStart = 0.85F;

		// HIGH HOVER POSITION of the left hand before the insertion,
		// last stop after the re-entry into the frame (weapon space,
		// vertical reload pose: +Y = up, +X = left in view, +Z = away
		// from the player):
		//   .y : height above the tube mouth (mouth ~= 42.9, the
		//        rocket hangs BELOW this point, tail down: raise y
		//        if the tail clips the muzzle)
		//   .x : lateral shift (+ = left)
		//   .z : depth (toward/away from the player)
		private Vector3 aboveMuzzle = Vector3(-0.25F, 79.0F, -8.0F);

		// How much BELOW the left-hand anchor the carried rocket hangs
		// (model units, along -Y). Raising the hand ON the rocket
		// prevents the hand model from clipping into the tube mouth
		// at the bottom of the insertion: the hand stops
		// holdBelowHand higher, while the rocket still travels the
		// full insertDepth and seats exactly at rocketSeatProgress
		// (invisible swap). Tune: raise it if the hand still clips,
		// lower it if the hand visibly floats above the rocket.
		private float holdBelowHand = 10.0F;

		// EASTER-EGG RELOAD "JUGGLE TOSS" (rare random variant, rolled
		// at each reload start in ReloadingWeapon()): same pose, same
		// timing windows as the classic reload - but instead of sliding
		// the rocket down into the tube, the left hand flicks it
		// straight up: the rocket does ONE full end-over-end spin,
		// like a juggler's club, and falls back perfectly aligned
		// into the tube mouth. Purely cosmetic, deliberately
		// unrealistic.
		//   juggleChance  : probability per reload (0.12 = ~1 in 8)
		//   juggleArc     : apex height of the flight arc, on top of
		//                   the hover -> seat line (model units)
		//   spinCenterY   : rocket model center above its origin = the
		//                   spin pivot (model units, tune to the
		//                   rocket's half length)
		//   spinDirection : 1 or -1 to flip the spin direction
		private float juggleChance = 0.1F;
		private float juggleArc = 87.0F;
		private float spinCenterY = 14.0F;
		private float spinDirection = 1.0F;

		// Variant-1 left-hand path (weapon space, vertical pose:
		// +X = left in view, +Y = up):
		//   throwPos: where the hand comes back to, on the LEFT of
		//             the screen, and flicks the rocket into its
		//             bell arc (also the rocket's launch point)
		//   idlePos : where the hand then rests, idle, by the MIDDLE
		//             of the screen (10 nov.: brought back from
		//             the left side - was Vector3(40, 40, 0))
		private Vector3 throwPos = Vector3(35.0F, 25.0F, -2.0F);
		private Vector3 idlePos = Vector3(15.0F, 10.0F, -8.0F);

		// Moment the rocket LEAVES the hand = the very END of the
		// flick gesture (the flick used to land a few hundredths
		// of a second before the rocket departed). The hand winds up,
		// snaps back to throwPos, and the rocket departs exactly at
		// tossProgress. Flight window: tossProgress -> rocketSeat.
		private float tossProgress = 0.50F;

		// 0 = classic reload, 1 = juggle toss (rolled lazily in
		// Update() on the first reload frame, see variantRolled)
		private int reloadVariant = 0;
		private bool variantRolled = false;

		// --------------------------------------------------------------------

		// A bunch of springs.
		private ViewWeaponSpring recoilVerticalSpring = ViewWeaponSpring(200, 24);
		private ViewWeaponSpring recoilBackSpring = ViewWeaponSpring(100, 16);
		private ViewWeaponSpring recoilRotationSpring = ViewWeaponSpring(50, 8);
		private ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 10);
		private ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);
		private ViewWeaponSpring reloadPitchSpring = ViewWeaponSpring(150, 12);

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		// ---- RPG state ------------------------------------------------------

		// loaded rocket visible at the muzzle
		private bool rocketLoaded = true;

		// The engine has no aim and no reload word for a grenade tool,
		// so the cook drives everything here. Pressing the trigger
		// cooks, which is this dress's aim: the weapon rises and the
		// RangeFinder reticle fades in. Releasing fires (the engine's
		// own word sends the rocket on the release), and the skin
		// plays the launch and runs its reload timeline.
		private float cookTime = 0.0F;
		private float prevCook = 0.0F;
		private float shotAt = 0.0F;

		// IGrenadeSkin: the cook, in seconds
		float CookTime { set { cookTime = value; } }

		// vertical reload pose: 0 = normal stance, 1 = RPG tipped
		// muzzle-up (butt low) to insert the rocket from above
		private float reloadPose = 0;

		// PNG reticle image (loaded once; RegisterImage returns null
		// SILENTLY when the path does not resolve - no log line, no
		// script error, the reticle just never draws)
		private Image@ reticleImage;

		ViewRPGSkin(Renderer@ r, AudioDevice@ dev) {
			super(r);
			@audioDevice = dev;

			// load models
			@gunModel = renderer.RegisterModel("WeaponMods/GrenadeRPG/Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@rocketModel = renderer.RegisterModel("WeaponMods/GrenadeRPG/Models/Weapons/Rifle/Rocket.kv6");
			@sightFrontModel = renderer.RegisterModel("WeaponMods/GrenadeRPG/Models/Weapons/Rifle/SightFront.kv6");
			@sightRearModel = renderer.RegisterModel("WeaponMods/GrenadeRPG/Models/Weapons/Rifle/SightRear.kv6");

			// ADS reticle sprite (pak Gfx/ folder)
			@reticleImage = renderer.RegisterImage(reticlePath);

			// load sounds (RPG samples to come; rifle assets for now)
			@fireSound = dev.RegisterSound("WeaponMods/GrenadeRPG/Sounds/Weapons/Rifle/FireLocal.opus");
			@fireFarSound = dev.RegisterSound("WeaponMods/GrenadeRPG/Sounds/Weapons/Rifle/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("WeaponMods/GrenadeRPG/Sounds/Weapons/Rifle/FireStereo.opus");
			@fireSmallReverbSound = dev.RegisterSound("WeaponMods/GrenadeRPG/Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound("WeaponMods/GrenadeRPG/Sounds/Weapons/Rifle/V2AmbienceLarge.opus");
			@reloadSound = dev.RegisterSound("WeaponMods/GrenadeRPG/Sounds/Weapons/Rifle/Reload.opus");

			// initial raise spring
			raiseSpring.position = 1;
			charmRaiseSpring.position = 1;
		}

		void Update(float dt) {
			// The rocket's trigger: the cook stands for the aim. While
			// the trigger is held the weapon rises to the aim (the ADS
			// stance and the reticle below read the same aim the guns
			// read); the fall of the cook is the release, the shot.
			bool cooking = cookTime > 0.05F;
			float adsTarget = cooking ? 1.0F : 0.0F;
			aimDownSightState += (adsTarget - aimDownSightState)
				* Min(1.0F, dt * 12.0F);
			if (aimDownSightState < 0.001F)
				aimDownSightState = 0.0F;
			aimDownSightStateSmooth = SmoothStep(aimDownSightState);

			if (prevCook > 0.05F and cookTime <= 0.05F) {
				WeaponFired();
				ReloadingWeapon();
				reloading = true;
				reloadProgress = 0.0F;
				shotAt = time;
			}
			prevCook = cookTime;

			// A tube reloads itself: a local timeline, the engine has
			// no reload word for this tool. A fifth of a second of
			// stillness after the shot, then two seconds of loading.
			if (reloading) {
				reloadProgress = Clamp((time - shotAt - 0.2F) / 2.0F,
					0.0F, 1.0F);
				if (reloadProgress >= 1.0F) {
					reloading = false;
					reloadProgress = 0.0F;
				}
			}

			BasicViewWeapon::Update(dt);

			reloadPitchSpring.Update(dt);

			// Roll the reload variant lazily, on the FIRST frame of the
			// reload (NOT in ReloadingWeapon(): the engine calls that
			// method BEFORE IsReloading turns true, so a reset here
			// used to wipe the roll on the very next frame - 20/20
			// classic reloads even with juggleChance = 1.0). The
			// variantRolled guard makes this insensitive to the exact
			// call order and re-rolls on every new reload.
			if (IsReloading) {
				if (!variantRolled) {
					reloadVariant = (GetRandom() < juggleChance) ? 1 : 0;
					variantRolled = true;
				}
			} else {
				variantRolled = false;
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

			// (flying rocket handled engine-side by RocketEntity)

			// blend the vertical reload pose in/out. The pose tips up as
			// soon as the reload starts; it tips back down from
			// flipBackStart (0.85), BEFORE the server-side reload ends,
			// so the last third of the reload is filled with the flip
			// motion instead of a frozen vertical hold (dead time).
			float poseTarget = 0.0F;
			if (IsReloading and reloadProgress < flipBackStart)
				poseTarget = 1.0F;
			reloadPose += (poseTarget - reloadPose) * Min(1.0F, dt * 8.0F);

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

			// the visible projectile itself is spawned engine-side
			// (RocketEntity); the skin just hides the barrel rocket
			rocketLoaded = false;

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

			// heavy recoil
			recoilVerticalSpring.velocity += 2.5;
			recoilBackSpring.position += 0.3;
			recoilBackSpring.velocity += 2.0;
			recoilRotationSpring.velocity += (GetRandom() * 2 - 1);
		}

		void ReloadingWeapon() {
			// NOTE: the variant is NOT rolled here (see Update):
			// the engine calls this BEFORE IsReloading turns true.
			reloadPitchSpring.velocity += 0.4F;

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 0.5F;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}
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
			// 17 nov. 2026: NO backward pull of the weapon when
			// firing - only the VERTICAL recoil component is kept
			// (recoilVerticalSpring below). The back spring is still
			// fed by WeaponFired() so a future pull can be restored by
			// re-enabling this line:
			// recoilOffset.y -= Mix(0.35F, 0.5F, sp) * recoilBackSpring.position;
			recoilOffset.z -= 0.1F * recoilVerticalSpring.position * sp;
			mat = CreateEulerAnglesMatrix(recoilRot * sp) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset);

			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);
			trans += Vector3(-0.13F * sp, 0.5F, GetZPos());

			// extra offset tweak
			trans += weaponOffsetTweak;

			// ADS stance: fade in with the aim, full strength at ADS
			// (world units, view space; the hip stance is untouched)
			trans += adsOffset * AimDownSightStateSmooth;

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

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		// IWeaponSkin3
		Vector3 MuzzlePosition {
			get {
				return GetViewWeaponMatrix()
					* ((rocketSeat + Vector3(0.0F, -weaponBackOffset, -weaponRaiseOffset)) * globalScale);
			}
		}
		Vector3 CaseEjectPosition {
			get {
				return GetViewWeaponMatrix()
					* ((rocketSeat + Vector3(0.0F, -weaponBackOffset, -weaponRaiseOffset)) * globalScale);
			}
		}

		// Reload (custom, 8 nov. 2026, revised 10 nov.):
		// the RPG is tipped muzzle-up, butt low (reload pose, applied in
		// AddToScene). The left hand EXITS THE FRAME TO THE LEFT
		// (view-space +X is left), comes back holding a fresh rocket,
		// hovers ABOVE the tube mouth (weapon-space +Y is up in this
		// pose), then slides the rocket down into the mouth. The rocket
		// appears seated at rocketSeatProgress. The hand then HOLDS the
		// seated position until the weapon starts tipping back down
		// (flipBackStart), and rides the tube back to the grip during
		// the flip. NOTE: on the RPG the grip is BELOW the muzzle (it
		// is not a magazine-fed weapon), so this return is a DOWNWARD
		// move - by design.
		//
		// VARIANT 1 "JUGGLE TOSS" (rare easter egg, rolled lazily in
		// Update()): same pose and same windows, but the left hand
		// comes back to the LEFT of the screen, flicks the rocket
		// into a lateral BELL ARC, then rests idle on the left. The
		// rocket sweeps across the view, one full end-over-end flip
		// that happens naturally around the apex of the arc, and
		// lands EXACTLY on the seated transform (see AddToScene), so
		// the swap at rocketSeatProgress is invisible.
		Vector3 GetLeftHandOffset() {
			Vector3 support = Vector3(1.0F, 6.0F, 1.0F);
			Vector3 exitLeft = Vector3(55.0F, -8.0F, 0.0F);	// out of frame, left
			// Progressive insertion (8 nov. 2026): from the high
			// hover position the rocket slides smoothly INTO the tube
			// (no more sin() round-trip that came back up). Tune the
			// depth with insertDepth: how deep the hand (and rocket)
			// sinks along -Y during the descent window. VALIDATED IN
			// GAME (10 nov.): 62 = the hand accompanies the rocket
			// right into the tube mouth (38 stopped ~16 units above
			// it: "hand hanging in the air").
			float insertDepth = 62.0F;

			// Position of the hand once the rocket is seated. The hand
			// stops holdBelowHand HIGHER than the rocket's seat: the
			// hand grips the rocket above its tail end, so it never
			// plunges into the tube mouth (clipping fix, 13 nov.).
			Vector3 seated = aboveMuzzle
				+ Vector3(0.0F, -(insertDepth - holdBelowHand), 0.0F);

			// Reload aborted/ended: ride the tube back down to the
			// grip from wherever the variant left the hand (seated
			// at the mouth for the classic, idle on the left for
			// the juggle toss).
			if (!IsReloading) {
				float back = 1.0F - reloadPose;
				Vector3 from = (reloadVariant == 1) ? idlePos : seated;
				if (back < 0.001F)
					return from;
				return Mix(from, support, SmoothStep(back));
			}

			float p = reloadProgress;

			// ---- VARIANT 1 "JUGGLE TOSS" hand path -----------------
			if (reloadVariant == 1) {
				if (p < 0.15F) {
					// hand exits the frame to the left
					return Mix(support, exitLeft, SmoothStep(p / 0.15F));
				} else if (p < 0.45F) {
					// comes back carrying the rocket, to the LEFT of
					// the screen (NOT above the tube mouth)
					return Mix(exitLeft, throwPos,
						SmoothStep((p - 0.15F) / (0.45F - 0.15F)));
				} else if (p < tossProgress) {
					// THROW, more marked (10 nov.): quick WINDUP
					// (hand pulls back left and down), then a sharp SNAP
					// back to throwPos. The rocket departs exactly at
					// the END of this window (tossProgress), so the
					// release coincides with the peak of the gesture.
					float f = (p - 0.45F) / (tossProgress - 0.45F);
					Vector3 windup = Vector3(6.0F, -7.0F, 0.0F);
					if (f < 0.55F)
						return Mix(throwPos, throwPos + windup,
							SmoothStep(f / 0.55F));
					// double SmoothStep = snappier acceleration on the
					// snap itself
					float snap = SmoothStep(
						SmoothStep((f - 0.55F) / 0.45F));
					return Mix(throwPos + windup, throwPos, snap);
				} else if (p < 0.58F) {
					// follow-through: right after the release the hand
					// still rises a bit (energy of the throw), then
					// settles to the idle position on the left.
					float g = (p - tossProgress) / (0.58F - tossProgress);
					Vector3 pos = Mix(throwPos, idlePos, SmoothStep(g));
					pos += Vector3(0.0F, 7.0F * 4.0F * g * (1.0F - g), 0.0F);
					return pos;
				} else {
					// rocket in flight: hand rests IDLE on the left,
					// then joins the grip during the flip (same
					// reloadPose-driven blend as the classic variant)
					float back = 1.0F - reloadPose;
					if (back < 0.001F)
						return idlePos;
					return Mix(idlePos, support, SmoothStep(back));
				}
			}

			// ---- VARIANT 0: classic insertion -----------------------
			// Reload phases, evaluated IN STRICT ORDER. The descent
			// into the tube comes BEFORE any return logic, so the
			// first downward move can never be skipped or preempted,
			// whatever the tunables are set to.
			if (p < 0.15F) {
				// hand exits the frame to the left
				return Mix(support, exitLeft, SmoothStep(p / 0.15F));
			} else if (p < 0.45F) {
				// comes back into frame carrying the rocket,
				// up above the tube mouth
				return Mix(exitLeft, aboveMuzzle, SmoothStep((p - 0.15F) / (0.45F - 0.15F)));
			} else if (p < rocketSeatProgress) {
				// FIRST DOWNWARD MOVE: the hand accompanies the rocket
				// INTO the tube. Smooth descent from the hover
				// position, stopping exactly when the rocket seats.
				float per = (p - 0.45F) / (rocketSeatProgress - 0.45F);
				return Mix(aboveMuzzle, seated, SmoothStep(per));
			} else {
				// Rocket seated: HOLD the hand at the mouth while the
				// pose is still vertical, then RIDE THE TUBE back down
				// to the grip during the flip. The blend is driven by
				// reloadPose (which only starts dropping at
				// flipBackStart), NOT by reloadProgress directly, so
				// this stays correct even if flipBackStart is later
				// tuned below or above rocketSeatProgress.
				float back = 1.0F - reloadPose;
				if (back < 0.001F)
					return seated; // flip not started yet: keep holding
				return Mix(seated, support, SmoothStep(back));
			}
		}

		Vector3 GetRightHandOffset() {
			Vector3 rightHandOffset = Vector3(0.0F, -8.0F, 2.0F);

			// sprint animation
			rightHandOffset -= Vector3(0.0F, 3.0F, 0.5F) * sprintSpring.position;

			return rightHandOffset;
		}

		void Draw2D() {
			BasicViewWeapon::Draw2D();

			// PNG reticle: centered on the screen, fading in with the
			// aim. TRAP (crash fix 11 nov. 2026): this engine exposes
			// NO tinted DrawImage overload (no Vector4 color, so no
			// runtime alpha) - the candidates only take Vector2/AABB2.
			// The fade is therefore done in SIZE only (the reticle
			// grows from nothing to full size); the PNG's own alpha
			// channel is still respected.
			if (!drawReticle or reticleImage is null)
				return;
			float a = AimDownSightStateSmooth;
			if (a <= reticleFadeStart)
				return;
			// 0 at reticleFadeStart, 1 at full ADS
			float fade = SmoothStep((a - reticleFadeStart)
				/ (1.0F - reticleFadeStart));
			float size = reticleSize * fade;
			if (size < 1.0F)
				return;
			Vector2 center = Vector2(renderer.ScreenWidth * 0.5F,
				renderer.ScreenHeight * 0.5F);
			// TRAP (fix 11 nov. 2026): AABB2(x1, y1, x2, y2) is NOT
			// (top-left, bottom-right) in this engine, it is
			// (base, SIZE): passing the two corners drew a huge
			// rectangle running off the bottom-right of the screen.
			// Pass (top-left, width, height).
			// The WHOLE image is drawn, its center exactly on the
			// screen center (11 nov.: reticle redrawn,
			// full-frame PNG, no crop needed).
			float dstW = size * reticleAspect;
			AABB2 dstRect(center.x - dstW * 0.5F, center.y - size * 0.5F,
				dstW, size);
			renderer.DrawImage(reticleImage, dstRect);
		}

		void AddToScene() {
			Matrix4 mat = GetViewWeaponMatrix()
				* CreateScaleMatrix(globalScale);

			// --- vertical reload pose --------------------------------------
			// Tip the RPG muzzle-up, butt low, by rotating around a
			// pivot by the butt (weapon-space -Y), then sinking it a
			// bit so the butt sits low in the view. The hands follow
			// automatically (they are transformed by mat). In this pose
			// weapon-space +Y is UP in view, which GetLeftHandOffset
			// relies on.
			if (reloadPose > 0.001F) {
				Vector3 pivot = Vector3(0.0F, -10.0F, 0.0F);	// by the butt
				mat = mat
					* CreateTranslateMatrix(pivot)
					* CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F),
						-PiF * 0.5F * reloadPose)
					* CreateTranslateMatrix(-pivot)
					* CreateTranslateMatrix(Vector3(0.0F, -42.0F, 20.0F) * reloadPose);
			}

			Vector3 leftHand, rightHand;
			leftHand = mat * GetLeftHandOffset();
			rightHand = mat * GetRightHandOffset();

			ModelRenderParam param;
			param.depthHack = true;

			Matrix4 weapMatrix = eyeMatrix * mat;

			// draw weapon body (raised by weaponRaiseOffset and pulled
			// back by weaponBackOffset; the hands keep their original
			// anchors). The kv6 is authored with its butt toward +Y
			// (front/back reversed), so a 180-degree yaw turns the
			// muzzle toward the crosshair. Same fix for the barrel
			// rocket (warhead along -Y in model space).
			Matrix4 modelFix = CreateRotateMatrix(Vector3(0.0F, 0.0F, 1.0F), PiF);
			param.matrix = weapMatrix
				* CreateTranslateMatrix(Vector3(0.0F, -weaponBackOffset, -weaponRaiseOffset))
				* modelFix;
			renderer.AddModel(gunModel, param);

			// draw iron sights (fixed, always deployed). Same
			// authoring frame as the weapon body (modelFix applied,
			// i.e. authored butt toward +Y like WeaponNoMagazine),
			// riding the raised/pulled body exactly like the
			// seated rocket does.
			param.matrix = weapMatrix
				* CreateTranslateMatrix(sightFrontSeat
					+ Vector3(0.0F, -weaponBackOffset, -weaponRaiseOffset))
				* modelFix
				* CreateEulerAnglesMatrix(Vector3(sightFrontFlipPitch,
					sightFrontFlipYaw, sightFrontFlipRoll) * PiF / 180.0F)
				* CreateScaleMatrix(sightFrontScale);
			renderer.AddModel(sightFrontModel, param);

			param.matrix = weapMatrix
				* CreateTranslateMatrix(sightRearSeat
					+ Vector3(0.0F, -weaponBackOffset, -weaponRaiseOffset))
				* modelFix
				* CreateEulerAnglesMatrix(Vector3(sightRearFlipPitch,
					sightRearFlipYaw, sightRearFlipRoll) * PiF / 180.0F)
				* CreateScaleMatrix(sightRearScale);
			renderer.AddModel(sightRearModel, param);

			// the rocket appears seated in the barrel exactly when the
			// hand's insertion stops (single shared constant)
			if (IsReloading and reloadProgress >= rocketSeatProgress)
				rocketLoaded = true;

			// rocket carried in the left hand during the reload (from
			// the moment the hand re-enters the frame until the rocket
			// is seated). modelFix (180-deg spin in the model plane)
			// makes the -Y warhead point UP in the vertical reload
			// pose (weapon-space +Y is up), tail down toward the tube
			// mouth: the rocket is inserted tail-first, RPG-7 style.
			// NOTE: must go through eyeMatrix (weapMatrix), like the
			// weapon body; rendering with mat alone puts the rocket
			// outside the view (that was the "empty hand" bug).
			if (IsReloading and reloadProgress >= 0.15F and reloadProgress < rocketSeatProgress) {
				if (reloadVariant == 1 and reloadProgress >= tossProgress) {
					// EASTER EGG "JUGGLE TOSS": the hand released the
					// rocket at the end of its flick (tossProgress).
					// Lateral BELL ARC (parabola) from throwPos to the
					// EXACT seated transform: the rocket rises to its
					// apex mid-flight, then falls back INTO the tube
					// aligned, so the swap at rocketSeatProgress is
					// invisible.
					float u = (reloadProgress - tossProgress)
						/ (rocketSeatProgress - tossProgress);
					Vector3 seatPos = rocketSeat
						+ Vector3(0.0F, -weaponBackOffset, -weaponRaiseOffset);
					// flight starts where the rocket actually was at the
					// release: holdBelowHand below the hand (throwPos)
					Vector3 pos = Mix(
						throwPos + Vector3(0.0F, -holdBelowHand, 0.0F),
						seatPos, u)
						+ Vector3(0.0F,
							juggleArc * 4.0F * u * (1.0F - u), 0.0F);
					// one full end-over-end flip in the view plane
					// (weapon-space Z axis), EASED with SmoothStep so
					// the rotation happens naturally AROUND THE APEX:
					// slow at release, fastest at the top of the arc,
					// slowing again as the rocket lines up with the
					// tube. Exactly 360 degrees over the flight, so
					// the rocket is aligned again the instant it
					// seats. The spin pivots around the rocket's
					// center (T(c) * R * T(-c)).
					float spinU = SmoothStep(u);
					param.matrix = weapMatrix
						* CreateTranslateMatrix(pos)
						* modelFix
						* CreateTranslateMatrix(Vector3(0.0F, spinCenterY, 0.0F))
						* CreateRotateMatrix(Vector3(0.0F, 0.0F, 1.0F),
							PiF * 2.0F * spinU * spinDirection)
						* CreateTranslateMatrix(Vector3(0.0F, -spinCenterY, 0.0F))
						* CreateScaleMatrix(rocketScale);
				} else {
					// classic: the rocket rides at the hand's position,
					// hanging holdBelowHand BELOW the anchor (the hand
					// grips it above the tail, not at the rocket's
					// origin), so the hand stays out of the tube mouth
					param.matrix = weapMatrix
						* CreateTranslateMatrix(
							GetLeftHandOffset()
							+ Vector3(0.0F, -holdBelowHand, 0.0F))
						* modelFix
						* CreateScaleMatrix(rocketScale);
				}
				renderer.AddModel(rocketModel, param);
			}

			// draw loaded rocket at the muzzle (follows the raised/pulled
			// weapon; rotated so the warhead (-Y in model space) points
			// forward)
			if (rocketLoaded) {
				param.matrix = weapMatrix
					* CreateTranslateMatrix(rocketSeat + Vector3(0.0F, -weaponBackOffset, -weaponRaiseOffset))
					* modelFix
					* CreateScaleMatrix(rocketScale);
				renderer.AddModel(rocketModel, param);
			}

			// (flying rocket, exhaust flame and smoke trail are drawn
			//  engine-side by RocketEntity)

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}
}