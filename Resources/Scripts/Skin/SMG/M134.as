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

This script was created using Mistral AI's Vibe (fast/low energy setting)

 */

// The multi-trail state uses PARALLEL PLAIN ARRAYS (array<Vector3>,
// array<float>, array<Matrix4>): one index = one live trail.

// Tower Defense dress: the mod's own view skin, adapted. The class
// is picked by the store's word (the factory in this weapon's stock
// View.as reads the tier); every file it loads is spelled with the
// dress's pak folder in front.

namespace spades {
	class ViewMinigunSkin : BasicViewWeapon {
		private AudioDevice@ audioDevice;

		private Model@ gunModel;
		private Model@ barrelModel;
		private Model@ magazineModel;
		private Model@ rearSightModel;

		private AudioChunk@[] fireSounds(4);
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@[] fireSmallReverbSounds(4);
		private AudioChunk@[] fireLargeReverbSounds(4);
		private AudioChunk@ reloadSound;
		private AudioChunk@ spinLoopSound;
		private AudioChunk@ spinDownSound;
		private AudioChunk@ spinTickSound;
		private AudioChunk@ sightClickSound;

		// ---- Settings ------------------------------------------------------

		private float globalScale = 0.03F;

		// Barrel assembly position on the body (model units).
		// Axes: x = right, y = forward, z = up.
		private Vector3 barrelAttachment = Vector3(0.0F, -1.0F, 9.5F);

		// Offset from the kv6 pivot to the true barrel axis (model units).
		// The spin is applied as T(offset) * R * T(-offset), so the barrels
		// rotate around this point regardless of the model pivot.
		// Only x and z matter (spin axis is y). Tune if barrels wobble.
		private Vector3 barrelPivotOffset = Vector3(-0.5F, 0.0F, -0.5F);

		private Vector3 barrelMuzzleAttachment = Vector3(-1.0F, 40.0F, 10.0F);

		private Vector3 caseEjectAttachment = Vector3(1.5F, -11.0F, 7.0F);

		private Vector3 magazineAttachment = Vector3(-1.0F, -10.0F, 8.0F);

		// ---- Custom reload animation ---------------------------------------
		// The holding hand rolls the weapon 90 deg to the RIGHT to show
		// its underside (where the magazine is), the left hand swaps
		// the magazine, then the weapon rolls back. All keyframes are
		// fractions of reloadProgress (0.0-1.0).

		// Pose taken while the magazine is swapped: 90 deg roll to the
		// right on y, slight tilt on x and z (radians), plus a small
		// offset.
		// The y component is the roll: POSITIVE tips the weapon's top
		// to the RIGHT in this space (x = right, y = forward,
		// z points down) - validated in game. Flip the sign if it
		// rolls the wrong way.
		// The x component tips the BARRELS UP (validated, keeps the
		// body away from the camera). It is applied through tiltBlend
		// (see below), NOT scaled by the roll itself.
		private Vector3 reloadFlipRotation = Vector3(-0.8F, 1.5708F, 0.18F);
		private Vector3 reloadFlipOffset = Vector3(1.0F, 0.0F, -18.0F);
		// Where the spent magazine is carried off screen (model units,
		// z points down); the fresh one comes back from the same side.
		private Vector3 magazineOutOffset = Vector3(-28.0F, -28.0F, 6.0F);
		// Offset of the left hand relative to the magazine while swapping.
		private Vector3 handOnMagazineOffset = Vector3(0.0F, 0.0F, 15.0F);
		// ---------------------------------------------------------------------

		// Barrel spin: max speed (rad/s), accel per shot, decel (rad/s^2).
		private float barrelSpinMax = 25.0F;
		private float barrelSpinAccel = 6.0F;
		private float barrelSpinDown = 8.0F;

		// Fire sounds volume (V2Local1-4.opus and their reverb tails).
		private float fireVolume = 0.23F;

		// Barrel spin audio: a single loop chunk, replayed on a timer;
		// pitch and volume follow the spin speed, giving a natural
		// windup/winddown ramp. spinLoopInterval must match the length
		// of SpinLoop.opus (seconds, keep it short: ~0.25-0.5s).
		private bool drawSpinSound = true;
		private float spinLoopInterval = 0.4F;
		private float spinLoopMinPitch = 0.5F;
		private float spinLoopMaxPitch = 1.5F;
		// stop retriggering the loop below this speed (rad/s);
		// with barrelSpinDown = 8, 4.0 stops the loop ~0.5s early
		private float spinStopThreshold = 4.0F;

		// Winddown: a single ~1.5s SpinDown.opus is played once when the
		// barrels start decelerating, at a pitch matching the current spin
		// speed. It plays out fully; if firing resumes before it ends, the
		// loop simply plays on top of the remaining tail (overlap by design).
		private float decelDelay = 0.12F;
		private float spinDownVolume = 2.0F;

		// Barrel rotation noise: a constant-pitch chunk triggered N times per
		// barrel revolution (interval = 2*pi / (spin speed * N)). The
		// retrigger rate, not the pitch, follows the spin speed.
		// Sample length: <= one tick interval at barrelSpinMax = 25
		// (0.25s / N; at N = 2 that's ~0.125s).
		private bool drawSpinTick = true;
		private float spinTickVolume = 0.08F;
		private float spinTickMinSpeed = 1.0F;	// rad/s; below: no ticking
		private float spinTicksPerRevolution = 2.0F;

		// Weapon-only vertical raise (voxels). NOTE: the z axis points DOWN
		// in this space, so raising the weapon means a NEGATIVE value here.
		// Hands stay at their normal position; only the rendered weapon
		// and its attachments (muzzle, case eject) move.
		private float weaponRaiseVoxels = -2.0F;

		// Rear sight (SightRear.kv6). Tune the position (model units,
		// z points down) and scale until it lines up with the body.
		private Vector3 rearSightAttachment = Vector3(0.075F, -10.0F, 1.33F);
		private float rearSightScale = 0.15F;
		// Folding sight: folded flat backwards while not aiming (ADS = 0),
		// standing up when aiming (ADS = 1). The sight rotates around
		// the hinge point (relative to rearSightAttachment, model units).
		// Flip the sign of the angle if it folds the wrong way.
		private bool drawFoldingSight = true;
		private Vector3 sightHingeOffset = Vector3(0.0F, 0.0F, 4.0F);
		private float sightFoldAngle = -1.5708F;	// radians; 90 degrees

		// ADS transition sound: a single SightClic.opus played when the
		// sight is almost fully deployed (aiming, late) and right after
		// the aim is released (closing, early).
		private bool drawSightSound = true;
		private float sightSoundVolume = 1.95F;
		private float sightClickUpThreshold = 0.85F;	// opening: near the end
		private float sightClickDownThreshold = 0.15F;	// closing: near the start

		private bool drawCharm = false;
		private Vector3 charmAttachment = Vector3(1.95F, -2.5F, -1.8F);

		// ---- Bullet smoke trail ---------------------------------------------
		// Same mechanic as the skin-side RPG rocket trail: on each
		// shot a straight line of puffs races from the muzzle along
		// the firing direction, each puff carrying the distance fade
		// of its spawn point, so the trail dissolves progressively
		// and is gone at the draw limit.
		private bool smokeTrailEnabled = true;

		// ================= TRAIL TUNING =================
		// All the visual knobs live here.

		// DENSITY: launch a trail 1 shot out of N (trails are
		// independent, this never resets the ones in flight).
		//   N=1  = one trail per shot (very dense, ~16 trails/s)
		//   N=3  = one trail every ~0.18 s
		//   N=10 = one trail every ~0.6 s (sparse)
		private int smokeTrailShotInterval = 1;

		// PUFF SIZE: billboard radius at spawn (world units).
		private float smokePuffRadius = 0.1F;
		// PUFF GROWTH: extra radius gained over the puff life
		// (the puff grows, then collapses to zero = fake fade).
		private float smokePuffGrowth = 0.5F;
		// PUFF SPACING: one puff every N units of flight
		// (smaller = a more continuous line, larger = dotted).
		private float smokePuffSpacing = 0.5F;
		// PUFF LIFE: seconds before the puff collapses and dies.
		private float smokePuffLife = 0.1F;
		// PUFF SPIN: billboard rotation speed (rad/s).
		private float smokePuffSpin = 1.6F;

		// ================= TRAIL BEHAVIOR =================
		// trail flight speed (world units per second)
		private float smokeTrailSpeed = 220.0F;
		// draw range: the trail fades out progressively and is gone
		// exactly at this distance
		private float smokeTrailMaxDistance = 20.0F;
		// spawn point in MODEL UNITS, same weapon space as
		// barrelMuzzleAttachment (x = right, y = forward,
		// z points DOWN in view-weapon space). Same value as
		// barrelMuzzleAttachment: the trail starts at the muzzle.
		private Vector3 smokeTrailSeat = Vector3(3.0F, 45.0F, 10.0F);
		// launch point offset in VIEW SPACE along X (+X = left, so a
		// NEGATIVE value shifts the spawn point to the RIGHT of the
		// muzzle, away from the viewport center: kills the near-plane
		// clipping)
		private float smokeTrailSideOffset = -0.15F;
		// round soft puff sprite (resolved from the base pak)
		private string smokeTrailSpritePath = "Textures/WaterExpl.png";

		// ---- Muzzle flash -------------------------------------------------
		// Smoke puff + coordinated dynamic light at the muzzle for a
		// few frames after each shot. The sprite is WaterExpl.png
		// (AddSprite has NO tint), so the yellow "flash" feel comes
		// from the LIGHT color below, not from the sprite.
		private bool smokeFlashEnabled = true;
		// flash lifetime (s). At ~16 shots/s this leaves a constant
		// flicker of ~1 live flash at the muzzle while firing
		private float smokeFlashLife = 0.05F;
		// flash radius (world units) = half the diameter (0.35)
		private float smokeFlashRadius = 1.25F;
		// billboard rotation speed (rad/s), visual variety
		private float smokeFlashSpin = 2.6F;
		// spawn point in MODEL UNITS, same weapon space as
		// smokeTrailSeat (x = right, y = forward, z points DOWN)
		private Vector3 smokeFlashSeat = Vector3(0.0F, 95.0F, 15.0F);
		// side offset in VIEW SPACE (+X = left, negative = right of
		// the muzzle), same near-viewport dodge as the trail
		private float smokeFlashSideOffset = 0.0F;
		private string smokeFlashSpritePath = "Textures/WaterExpl.png";
		// ---- Muzzle flash LIGHT -------------------------------------------
		// renderer.AddLight(DynamicLightParam) at the SAME position as
		// the smoke puff, same life; radius and color fade out with
		// the puff. Point light (engine default type), warm yellow.
		private float smokeFlashLightRadius = 5.0F;
		private Vector3 smokeFlashLightColor = Vector3(1.0F, 0.8F, 0.3F);

		// --------------------------------------------------------------------

		// A bunch of springs.
		private ViewWeaponSpring reloadPitchSpring = ViewWeaponSpring(150, 12);
		private ViewWeaponSpring reloadRollSpring = ViewWeaponSpring(150, 16);
		private ViewWeaponSpring reloadOffsetSpring = ViewWeaponSpring(150, 12);
		private ViewWeaponSpring recoilVerticalSpring = ViewWeaponSpring(300, 24);
		private ViewWeaponSpring recoilBackSpring = ViewWeaponSpring(300, 16);
		private ViewWeaponSpring recoilRotationSpring = ViewWeaponSpring(50, 8);
		private ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 10);
		private ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);

		// A bunch of events.
		private ViewWeaponEvent magazineTouched = ViewWeaponEvent();
		private ViewWeaponEvent magazineRemoved = ViewWeaponEvent();
		private ViewWeaponEvent magazineInserted = ViewWeaponEvent();

		// Barrel spin state.
		private float barrelSpin = 0.0F;
		private float barrelSpinVel = 0.0F;
		private float spinLoopTimer = 0.0F;
		private float spinTickTimer = 0.0F;
		private bool windDownTriggered = false;
		private float timeSinceFired = 9999.0F;
		private bool sightAimed = false;

		// Smoke trail state. Each live trail is one INDEX in parallel
		// plain arrays (RPG-trail manner, no script-class handles):
		// several trails fly at the same time, a new launch never
		// resets the others. The puffs (smokePuff*) age out on their
		// own, fully decoupled.
		private Image@ smokeTrailImage;
		private int smokeTrailShotCount = 0;  // shots fired since the last trail launch
		private array<Vector3> trailOrigins;   // world-space launch point (side offset applied)
		private array<Matrix4> trailEyes;      // eye matrix captured AT THE SHOT
		private array<Vector3> trailEyeOrigins; // eye world position at the shot
		private array<float> trailDistances;    // units flown since the launch
		private array<float> trailNexts;       // flight distance of the next puff
		private array<Vector3> smokePuffPositions;
		private array<float> smokePuffAges;
		private array<float> smokePuffFade;        // distance fade at spawn (1 at the muzzle, 0 at the draw limit)

		// Muzzle flash state (one live flash per recent shot).
		private Image@ smokeFlashImage;
		private array<Vector3> smokeFlashPositions;
		private array<float> smokeFlashAges;
		// random initial billboard angle per flash (drawn once at
		// spawn), so successive flashes never look like a copy-paste
		private array<float> smokeFlashAngles;

		// Reload flip fade, driven by TIME (not by reloadProgress):
		// immune to a reload that ends before p reaches 1.0 (the last
		// degrees of the flip snapping = the end-of-reload jolt).
		private float flipBlend = 0.0F;

		// Nose-up tilt blend (the x part of reloadFlipRotation).
		// The reload motion is a STRICT SEQUENCE: the tilt completes
		// BEFORE the flip starts, and the weapon straightens only
		// AFTER the flip back is over. The flip therefore never runs
		// while the weapon is partially tilted, and the horizontal
		// rest pose is never visible mid-flip (that was the camera
		// clip, at the start AND at the end of the flip).
		private float tiltBlend = 0.0F;
		private float tiltInTime = 0.08F;	// s, tilt-up duration
		private float tiltOutTime = 0.35F;	// s, straighten duration

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		Matrix4 AdjustToReload(Matrix4 mat) {
			if (reloadProgress < 0.6) {
				reloadPitchSpring.desired = 0.6;
				reloadRollSpring.desired = 0.6;
			} else if (reloadProgress < 0.9) {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = -0.6;
			} else {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = 0;
			}

			if (magazineTouched.WasActivated()) {
				magazineTouched.Acknowledge();
				reloadPitchSpring.velocity = 4;
			}

			if (magazineRemoved.WasActivated()) {
				magazineRemoved.Acknowledge();
				reloadPitchSpring.velocity = -4;
			}

			if (magazineInserted.WasActivated()) {
				magazineInserted.Acknowledge();
				reloadPitchSpring.velocity = 8;
			}

			float sp = 1.0F - AimDownSightStateSmooth;

			mat *= CreateRotateMatrix(Vector3(0, 1, 0), 0.6F * reloadRollSpring.position * sp);
			mat *= CreateRotateMatrix(Vector3(-1, 0, 0), 0.25F * reloadPitchSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0.0F, -1.0F, 0.0F) * reloadOffsetSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0.0F, 0.0F, -globalScale) * reloadPitchSpring.position * sp);

			return mat;
		}

		Vector3 GetMagazineOffset() {
			if (!IsReloading)
				return magazineAttachment;

			// keyframes: hold, detach, out of screen, back in, seat, done
			float p = reloadProgress;
			Vector3 hover = magazineAttachment + Vector3(0.0F, 0.0F, 2.5F);

			if (p < 0.15F) {
				return magazineAttachment;
			} else if (p < 0.32F) {
				magazineRemoved.Activate();
				float per = SmoothStep((p - 0.15F) / (0.32F - 0.15F));
				return Mix(magazineAttachment, magazineOutOffset, per);
			} else if (p < 0.46F) {
				return magazineOutOffset;
			} else if (p < 0.62F) {
				float per = SmoothStep((p - 0.46F) / (0.62F - 0.46F));
				return Mix(magazineOutOffset, hover, per);
			} else if (p < 0.74F) {
				// seat the magazine: push it up with a small overshoot
				float per = (p - 0.62F) / (0.74F - 0.62F);
				Vector3 pos = Mix(hover, magazineAttachment, SmoothStep(per));
				pos -= Vector3(0.0F, 0.0F, 0.6F) * sin(per * PiF);
				if (per > 0.9F)
					magazineInserted.Activate();
				return pos;
			} else {
				return magazineAttachment;
			}
		}

		Vector3 GetLeftHandOffset() {
			Vector3 support = Vector3(1.0F, 3.0F, 0.5F);

			if (!IsReloading)
				return support;

			// during the reload the hand rides on the magazine:
			// it carries the spent one out and brings the fresh one
			// back.
			// TIME-DRIVEN blend (flipBlend, the same fade as the
			// flip in AddToScene): the previous progress-keyed mix
			// SmoothStep((p - 0.78) / 0.22) SNAPPED when the reload
			// ended on a frame where p had not reached 1.0 yet
			// (GetReloadProgress is unclamped and IsReloading can drop
			// early) - the hand jumped the remaining ~40% of its path
			// in one frame = the end-of-reload jolt. flipBlend chases
			// its target at a fixed rate, so an early end de-fades
			// smoothly instead of snapping.
			Vector3 onMag = GetMagazineOffset() + handOnMagazineOffset;
			return Mix(support, onMag, flipBlend);
		}

		// World-space flight direction is computed inline in Update()
		// (per-trail, frozen at the shot, RPG tracerDirection manner).

		Vector3 GetRightHandOffset() {
			Vector3 rightHandOffset = Vector3(0.0F, -8.0F, 2.0F);

			// sprint animation
			rightHandOffset -= Vector3(0.0F, 3.0F, 0.5F) * sprintSpring.position;

			return rightHandOffset;
		}

		ViewMinigunSkin(Renderer@ r, AudioDevice@ dev) {
			super(r);
			@audioDevice = dev;

			// load models
			@gunModel = renderer.RegisterModel("WeaponMods/SMGLvl4/Models/Weapons/SMG/WeaponNoMagazine.kv6");
			@barrelModel = renderer.RegisterModel("WeaponMods/SMGLvl4/Models/Weapons/SMG/Barrels.kv6");
			@magazineModel = renderer.RegisterModel("WeaponMods/SMGLvl4/Models/Weapons/SMG/Magazine.kv6");
			@rearSightModel = renderer.RegisterModel("WeaponMods/SMGLvl4/Models/Weapons/SMG/SightRear.kv6");

			// load sounds
			@fireSounds[0] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2Local1.opus");
			@fireSounds[1] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2Local2.opus");
			@fireSounds[2] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2Local3.opus");
			@fireSounds[3] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2Local4.opus");
			@fireFarSound = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/FireStereo.opus");
			@fireSmallReverbSounds[0] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceSmall1.opus");
			@fireSmallReverbSounds[1] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceSmall2.opus");
			@fireSmallReverbSounds[2] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceSmall3.opus");
			@fireSmallReverbSounds[3] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceSmall4.opus");
			@fireLargeReverbSounds[0] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceLarge1.opus");
			@fireLargeReverbSounds[1] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceLarge2.opus");
			@fireLargeReverbSounds[2] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceLarge3.opus");
			@fireLargeReverbSounds[3] = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/V2AmbienceLarge4.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/SMG/ReloadLocal.opus");
			@spinLoopSound = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/SpinLoop.opus");
			@spinDownSound = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/SpinDown.opus");
			@spinTickSound = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/SpinTick.opus");
			@sightClickSound = dev.RegisterSound("WeaponMods/SMGLvl4/Sounds/Weapons/SMG/SightClic.opus");

			// smoke trail sprite. NOTE: RegisterImage returns null
			// SILENTLY when a path does not resolve (no log line,
			// no script error) and the trail never spawns at all.
			@smokeTrailImage = renderer.RegisterImage(smokeTrailSpritePath);

			// muzzle flash sprite (same WaterExpl puff as the trail;
			// the flash feel comes from the coordinated dynamic light,
			// see smokeFlashLight*)
			@smokeFlashImage = renderer.RegisterImage(smokeFlashSpritePath);

			// initial raise spring
			raiseSpring.position = 1;
			charmRaiseSpring.position = 1;
		}

		void Update(float dt) {
			// Clamp dt: the skin springs (stiffness 150-300) are
			// integrated explicitly and EXPLODE on a frame hitch
			// (dt > ~0.16 s): the weapon models tumble on their own
			// for a moment and then settle back (observed at the end
			// of the reload). Anything slower than 20 fps is clamped;
			// timers/trails simply run in slowed time for that frame.
			dt = Min(dt, 0.05F);

			BasicViewWeapon::Update(dt);

			reloadPitchSpring.Update(dt);
			reloadRollSpring.Update(dt);
			reloadOffsetSpring.Update(dt);

			// smoothly increase recoil damping while aiming down sights
			recoilVerticalSpring.damping = Mix(16, 24, AimDownSightStateSmooth);
			recoilBackSpring.damping = Mix(24, 20, AimDownSightStateSmooth);
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

			// ---- Reload motion: strict sequence ---------------------------
			//   1. tilt the weapon nose-up (tiltBlend, fast)
			//   2. flip it over (flipBlend) - only once FULLY tilted
			//   3. magazine swap (GetMagazineOffset keyframes)
			//   4. flip back (flipBlend -> 0) from 76% of the reload
			//   5. straighten to rest (tiltBlend -> 0) - only once the
			//      flip back is FULLY over
			// Each stage waits for the previous one. History: the old
			// un-flip formula fed UNCLAMPED GetReloadProgress() into
			// UNCLAMPED SmoothStep() (it extrapolates past x = 1), so
			// the flip target rose again past the trough then snapped
			// to 0 when IsReloading dropped (the end-of-reload jolt);
			// hence the explicit Clamp(p) and the hard flipTarget = 0.
			float tiltTarget = 0.0F;
			float flipTarget = 0.0F;
			if (IsReloading) {
				float p = Clamp(reloadProgress, 0.0F, 1.0F);
				tiltTarget = 1.0F;						// stage 1
				if (tiltBlend >= 0.999F)					// stage 2
					flipTarget = 1.0F;
				if (p >= 0.76F)						// stage 4
					flipTarget = 0.0F;
				if (flipBlend <= 0.001F and p >= 0.76F)			// stage 5
					tiltTarget = 0.0F;
			}
			// both fades chase their targets at fixed rates: never a
			// jump, and even if IsReloading drops mid-sequence the
			// stages still run their course in order (the flip back
			// finishes before the weapon straightens)
			float flipStep = dt / 0.25F;
			if (flipBlend < flipTarget)
				flipBlend = Min(flipBlend + flipStep, flipTarget);
			else
				flipBlend = Max(flipBlend - flipStep, flipTarget);
			float tiltStep = dt / ((tiltTarget > tiltBlend) ? tiltInTime : tiltOutTime);
			if (tiltBlend < tiltTarget)
				tiltBlend = Min(tiltBlend + tiltStep, tiltTarget);
			else
				tiltBlend = Max(tiltBlend - tiltStep, tiltTarget);

			// barrel spin (inertia, then deceleration)
			barrelSpin += barrelSpinVel * dt;
			barrelSpinVel = Max(0.0F, barrelSpinVel - barrelSpinDown * dt);

			// avoid float precision loss on long sessions
			float fullTurn = PiF * 2.0F;
			if (barrelSpin > fullTurn * 100.0F)
				barrelSpin -= fullTurn * 100.0F;

			// ---- ADS transition sound ------------------------------------
			// asymmetric thresholds: late click when opening,
			// early click when closing
			{
				bool playClick = false;
				if (!sightAimed and AimDownSightStateSmooth > sightClickUpThreshold) {
					sightAimed = true;
					playClick = true;
				} else if (sightAimed and AimDownSightStateSmooth < sightClickDownThreshold) {
					sightAimed = false;
					playClick = true;
				}
				if (playClick and drawSightSound and !IsMuted) {
					AudioParam param;
					param.volume = sightSoundVolume;
					audioDevice.PlayLocal(sightClickSound, Vector3(0.4F, -0.3F, 0.5F), param);
				}
			}

			// ---- barrel spin audio -------------------------------------
			timeSinceFired += dt;
			bool decelPhase = (timeSinceFired > decelDelay) and (barrelSpinVel > 0.0F);

			if (!decelPhase) {
				// accelerating / at speed: pitch-modulated loop
				windDownTriggered = false;
				if (barrelSpinVel > spinStopThreshold) {
					spinLoopTimer -= dt;
					if (spinLoopTimer <= 0.0F) {
						if (drawSpinSound and !IsMuted) {
							float sp2 = barrelSpinVel / barrelSpinMax;
							AudioParam param;
							param.pitch = Mix(spinLoopMinPitch, spinLoopMaxPitch, sp2);
							param.volume = 2.5F * sp2;
							audioDevice.PlayLocal(spinLoopSound, Vector3(0.4F, -0.3F, 0.5F), param);
						}
						spinLoopTimer = spinLoopInterval;
					}
				} else {
					spinLoopTimer = 0.0F;
				}
			} else {
				// decelerating: play the winddown once, at the current pitch
				spinLoopTimer = 0.0F;
				if (!windDownTriggered) {
					windDownTriggered = true;
					if (drawSpinSound and !IsMuted) {
						AudioParam param;
						param.pitch = Mix(spinLoopMinPitch, spinLoopMaxPitch,
							barrelSpinVel / barrelSpinMax);
						param.volume = spinDownVolume;
						audioDevice.PlayLocal(spinDownSound, Vector3(0.4F, -0.3F, 0.5F), param);
					}
				}
			}

			// barrel rotation noise: one tick per revolution
			if (barrelSpinVel > spinTickMinSpeed) {
				spinTickTimer -= dt;
				if (spinTickTimer <= 0.0F) {
					if (drawSpinTick and !IsMuted) {
						AudioParam param;
						param.volume = spinTickVolume;
						audioDevice.PlayLocal(spinTickSound, Vector3(0.4F, -0.3F, 0.5F), param);
					}
					spinTickTimer += (PiF * 2.0F) / (barrelSpinVel * spinTicksPerRevolution);
				}
			} else {
				spinTickTimer = 0.0F;
			}

			// ---- bullet smoke trail ---------------------------------------
			// straight lines from the muzzle, puffs spawned along the
			// flown distance, each carrying the distance fade of its
			// spawn point: every trail tapers off toward the draw
			// limit. All the live trails are updated independently.
			for (int ti = trailDistances.length() - 1; ti >= 0; ti--) {
				trailDistances[ti] += smokeTrailSpeed * dt;
				// per-trail flight direction, computed INLINE exactly
				// like the RPG's tracerDirection(): the view forward
				// (+Y) through the rotation part of the eye matrix
				// captured at the shot (frozen orientation)
				Matrix4 trailRot = CreateTranslateMatrix(-trailEyeOrigins[ti]) * trailEyes[ti];
				Vector3 trailDir = trailRot * Vector3(0.0F, 1.0F, 0.0F);
				while (trailNexts[ti] <= trailDistances[ti]
					and trailNexts[ti] <= smokeTrailMaxDistance) {
					if (not (smokeTrailImage is null)) {
						smokePuffPositions.insertLast(
							trailOrigins[ti] + trailDir * trailNexts[ti]);
						smokePuffAges.insertLast(0.0F);
						smokePuffFade.insertLast(
							1.0F - trailNexts[ti] / smokeTrailMaxDistance);
					}
					trailNexts[ti] += smokePuffSpacing;
				}
				// trail done: it has drawn its full run, retire it
				// (removeAt on ALL FIVE parallel arrays, same index)
				if (trailDistances[ti] >= smokeTrailMaxDistance) {
					trailOrigins.removeAt(ti);
					trailEyes.removeAt(ti);
					trailEyeOrigins.removeAt(ti);
					trailDistances.removeAt(ti);
					trailNexts.removeAt(ti);
				}
			}
			// age the smoke puffs, remove the dead ones
			for (int i = smokePuffAges.length() - 1; i >= 0; i--) {
				smokePuffAges[i] += dt;
				if (smokePuffAges[i] >= smokePuffLife) {
					smokePuffAges.removeAt(i);
					smokePuffPositions.removeAt(i);
					smokePuffFade.removeAt(i);
				}
			}

			// age the muzzle flashes, remove the dead ones
			for (int i = smokeFlashAges.length() - 1; i >= 0; i--) {
				smokeFlashAges[i] += dt;
				if (smokeFlashAges[i] >= smokeFlashLife) {
					smokeFlashAges.removeAt(i);
					smokeFlashPositions.removeAt(i);
					smokeFlashAngles.removeAt(i);
				}
			}

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

			// each shot spins the barrels up, up to max speed
			barrelSpinVel = Min(barrelSpinVel + barrelSpinAccel, barrelSpinMax);
			timeSinceFired = 0.0F;

			// launch a NEW independent smoke trail 1 shot out of
			// smokeTrailShotInterval: it never resets the trails
			// already flying, so several can be on screen at once
			smokeTrailShotCount++;
			if (smokeTrailEnabled and smokeTrailShotCount >= smokeTrailShotInterval) {
				smokeTrailShotCount = 0;
				Matrix4 capturedEye = eyeMatrix;
				Matrix4 rot = CreateTranslateMatrix(
					-(capturedEye * Vector3(0.0F, 0.0F, 0.0F))) * capturedEye;
				// launch point = the muzzle (view space, mapped through
				// eyeMatrix - same trap as the RPG tracer), shifted
				// sideways in view space to dodge the near-viewport
				// clipping
				Vector3 launchOrigin = eyeMatrix * (
					CreateTranslateMatrix(Vector3(0.0F, 0.0F, weaponRaiseVoxels * globalScale))
					* GetViewWeaponMatrix()
					* (smokeTrailSeat * globalScale));
				launchOrigin += rot * Vector3(smokeTrailSideOffset, 0.0F, 0.0F);
				// freeze the eye orientation AT THE SHOT and push ONE
				// ENTRY into each parallel array (RPG-trail manner;
				// the AS API exposes no GetOrigin/SetOrigin on
				// Matrix4, the eye origin is read via the transform)
				trailOrigins.insertLast(launchOrigin);
				trailEyes.insertLast(capturedEye);
				trailEyeOrigins.insertLast(capturedEye * Vector3(0.0F, 0.0F, 0.0F));
				trailDistances.insertLast(0.0F);
				trailNexts.insertLast(0.0F);
			}

			// ---- muzzle flash ------------------------------------------
			// one bright yellow puff at the muzzle for EVERY shot
			// (independent of smokeTrailShotInterval)
			if (smokeFlashEnabled and not (smokeFlashImage is null)) {
				// same view-space mapping as the trail launch point
				Matrix4 flashRot = CreateTranslateMatrix(
					-(eyeMatrix * Vector3(0.0F, 0.0F, 0.0F))) * eyeMatrix;
				Vector3 flashPos = eyeMatrix * (
					CreateTranslateMatrix(Vector3(0.0F, 0.0F, weaponRaiseVoxels * globalScale))
					* GetViewWeaponMatrix()
					* (smokeFlashSeat * globalScale));
				flashPos += flashRot * Vector3(smokeFlashSideOffset, 0.0F, 0.0F);
				smokeFlashPositions.insertLast(flashPos);
				smokeFlashAges.insertLast(0.0F);
				// random billboard angle at spawn: every flash shows
				// a different face of the sprite (kills the
				// repetitive look of ~16 identical puffs per second)
				smokeFlashAngles.insertLast(GetRandom() * PiF * 2.0F);
			}

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = fireVolume;
				audioDevice.PlayLocal(fireSounds[GetRandom(fireSounds.length)], origin, param);

				param.volume = fireVolume * environmentRoom;
				audioDevice.PlayLocal((environmentSize < 0.5F)
					? fireSmallReverbSounds[GetRandom(fireSmallReverbSounds.length)]
					: fireLargeReverbSounds[GetRandom(fireLargeReverbSounds.length)],
					origin, param);

				param.referenceDistance = 4.0F;
				param.volume = 1.0F;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.0F;
				audioDevice.PlayLocal(fireStereoSound, origin, param);
			}

			recoilVerticalSpring.velocity += 0.75;
			recoilBackSpring.position += 0.1;
			recoilRotationSpring.velocity += (GetRandom() * 2 - 1);

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;
		}

		void ReloadingWeapon() {
			magazineTouched.Reset();
			magazineRemoved.Reset();
			magazineInserted.Reset();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 0.2F;
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
			recoilRot.y = 0.2F * recoilRotationSpring.position;
			recoilRot.z = 0.2F * recoilRotationSpring.position;
			Vector3 recoilOffset = Vector3(0.0F, 0.0F, 0.0F);
			recoilOffset.y -= Mix(0.5F, 0.25F, sp) * recoilBackSpring.position;
			recoilOffset.z -= 0.1F * recoilVerticalSpring.position * sp;
			mat = CreateEulerAnglesMatrix(recoilRot * sp) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset);

			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);
			trans += Vector3(-0.13F * sp, 0.5F, GetZPos());

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
		// muzzle and case eject follow the raised weapon
		Vector3 MuzzlePosition { get { return CreateTranslateMatrix(Vector3(0.0F, 0.0F, weaponRaiseVoxels * globalScale)) * GetViewWeaponMatrix() * (barrelMuzzleAttachment * globalScale); } }
		Vector3 CaseEjectPosition { get { return CreateTranslateMatrix(Vector3(0.0F, 0.0F, weaponRaiseVoxels * globalScale)) * GetViewWeaponMatrix() * (caseEjectAttachment * globalScale); } }

		void Draw2D() {
			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
			Matrix4 mat = GetViewWeaponMatrix()
				* CreateScaleMatrix(globalScale);

			// custom reload: roll the weapon 90 deg to the right to
			// show its underside.
			// The fades are driven by time (updated in Update): no jump
			// possible at the end of the reload.
			// The nose-up tilt (x) rides tiltBlend, which is fully up
			// BEFORE the roll starts and fully down only AFTER the
			// roll is over: the body clears the camera during both
			// ramp phases. The y/z roll keeps its own fade (flipBlend).
			float flip = flipBlend;
			if (tiltBlend > 0.0F) {
				mat = mat * CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F),
					reloadFlipRotation.x * tiltBlend);
			}
			if (flip > 0.0F) {
				mat = mat * CreateEulerAnglesMatrix(Vector3(0.0F,
					reloadFlipRotation.y * flip, reloadFlipRotation.z * flip));
				mat = mat * CreateTranslateMatrix(reloadFlipOffset * flip);
			}

			Vector3 leftHand, rightHand;
			leftHand = mat * GetLeftHandOffset();
			rightHand = mat * GetRightHandOffset();

			ModelRenderParam param;
			param.depthHack = true;

			// weapon raised independently: hands are computed from mat,
			// models are drawn from weapMatrix
			Matrix4 weapMatrix = eyeMatrix
				* CreateTranslateMatrix(Vector3(0.0F, 0.0F, weaponRaiseVoxels * globalScale))
				* mat;

			// draw weapon (body)
			param.matrix = weapMatrix;
			renderer.AddModel(gunModel, param);

			// draw barrels: rotate around barrelPivotOffset,
			// i.e. T(offset) * R * T(-offset), regardless of the kv6 pivot
			param.matrix = weapMatrix
				* CreateTranslateMatrix(barrelAttachment)
				* CreateTranslateMatrix(barrelPivotOffset)
				* CreateRotateMatrix(Vector3(0, 1, 0), barrelSpin)
				* CreateTranslateMatrix(-barrelPivotOffset);
			renderer.AddModel(barrelModel, param);

			// draw magazine (under the body)
			param.matrix = weapMatrix
				* CreateTranslateMatrix(GetMagazineOffset());
			renderer.AddModel(magazineModel, param);

			// draw rear sight (folds back when not aiming)
			{
				Matrix4 sightMat = weapMatrix
					* CreateTranslateMatrix(rearSightAttachment);
				if (drawFoldingSight and AimDownSightStateSmooth < 0.999F) {
					// rotate the fold amount around the hinge point:
					// T(hinge) * R * T(-hinge)
					float fold = (1.0F - AimDownSightStateSmooth) * sightFoldAngle;
					sightMat = sightMat
						* CreateTranslateMatrix(sightHingeOffset)
						* CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), fold)
						* CreateTranslateMatrix(-sightHingeOffset);
				}
				param.matrix = sightMat
					* CreateScaleMatrix(rearSightScale);
				renderer.AddModel(rearSightModel, param);
			}

			// draw the smoke trail puffs (world-space billboards):
			// grow for 70% of the life, then collapse to zero radius
			// (fake fade, no pop-out), scaled by the distance fade
			// captured at spawn so the trail dissolves toward the
			// draw limit
			if (not (smokeTrailImage is null)) {
				for (uint i = 0; i < smokePuffAges.length(); i++) {
					float t = smokePuffAges[i] / smokePuffLife;
					float radius;
					if (t < 0.7F)
						radius = smokePuffRadius
							+ smokePuffGrowth * (t / 0.7F);
					else
						radius = (smokePuffRadius + smokePuffGrowth)
							* (1.0F - (t - 0.7F) / 0.3F);
					radius *= smokePuffFade[i];
					renderer.AddSprite(smokeTrailImage,
						smokePuffPositions[i], radius,
						smokePuffAges[i] * smokePuffSpin);
				}
			}

			// draw the muzzle flashes (world-space billboards): the
			// flash SHRINKS linearly to zero radius over its life
			// (fake fade, AddSprite has no alpha control), starting
			// from a RANDOM angle picked at spawn (the small spin on
			// top keeps each flash alive on its own), with a
			// coordinated warm-yellow POINT light at the same
			// position (radius and color fade with the puff). The
			// light only illuminates blocks, not the view model,
			// so the sprite + light combo reads as a muzzle flash.
			if (not (smokeFlashImage is null)) {
				for (uint i = 0; i < smokeFlashAges.length(); i++) {
					float ft = smokeFlashAges[i] / smokeFlashLife;
					renderer.AddSprite(smokeFlashImage,
						smokeFlashPositions[i],
						smokeFlashRadius * (1.0F - ft),
						smokeFlashAngles[i] + smokeFlashAges[i] * smokeFlashSpin);

					DynamicLightParam light;
					light.origin = smokeFlashPositions[i];
					light.radius = smokeFlashLightRadius * (1.0F - ft);
					light.color = smokeFlashLightColor * (1.0F - ft);
					renderer.AddLight(light);
				}
			}

			// draw charms
			if (drawCharm and cg_weaponCharms.BoolValue) {
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
					* CreateTranslateMatrix(charmAttachment)
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
