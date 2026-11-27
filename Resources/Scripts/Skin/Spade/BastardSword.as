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

// SWORD SKIN (v1) - spade skin replacement.
// Left click (Bash): combo hit 1, windup -> diagonal slash -> recovery,
// combo 2 window opens during recovery. Right click (Dig): thrust stance,
// repeated pull-back + estoc synced to the engine dig cycle. Combo stages
// 2/3 reuse hit 1 for now (own animations in v2/v3).
// Pak path: Scripts/Skin/Spade/View.as

// Tower Defense dress: the mod's own view skin, adapted. The class
// is picked by the store's word (the factory in this weapon's stock
// View.as reads the tier); every file it loads is spelled with the
// dress's pak folder in front.

namespace spades {
	class ViewSpadeSwordSkin : IToolSkin, IViewToolSkin, ISpadeSkin {
		private float sprintState;
		private float raiseState;
		private Vector3 teamColor;
		private Matrix4 eyeMatrix;
		private Vector3 swing;
		private Vector3 leftHand;
		private Vector3 rightHand;
		private SpadeActionType actionType;
		private float actionProgress;
		private float sprintStateSmooth;

		float SprintState { set { sprintState = value; } }
		float RaiseState { set { raiseState = value; } }
		Vector3 TeamColor { set { teamColor = value; } }
		bool IsMuted { set {} }
		Matrix4 EyeMatrix { set { eyeMatrix = value; } }
		Vector3 Swing { set { swing = value; } }
		Vector3 LeftHandPosition { get { return leftHand; } }
		Vector3 RightHandPosition { get { return rightHand; } }
		SpadeActionType ActionType { set { actionType = value; } }
		float ActionProgress { set { actionProgress = value; } }

		private Renderer@ renderer;
		private AudioDevice@ audioDevice;
		private Model@ spadeModel;
		private Model@ pickaxeModel;
		private Model@ swordModel;
		private Model@ model;
		private Image@ sightImage;

		protected ConfigItem cg_viewWeaponX("cg_viewWeaponX");
		protected ConfigItem cg_viewWeaponY("cg_viewWeaponY");
		protected ConfigItem cg_viewWeaponZ("cg_viewWeaponZ");
		protected ConfigItem cg_viewWeaponSide("cg_viewWeaponSide");
		protected ConfigItem n_rgbSpeed("n_rgbSpeed", "42.0");

		// ---- Sword tunables -------------------------------------------------

		private string swordModelPath = "WeaponMods/SpadeLvl2/Models/Weapons/Spade/Spade.kv6";
		private bool useSwordModel = true;   // false = vanilla spade fallback
		private float swordScale = 0.033F;   // vanilla spade scale
		private bool drawSightImage = true;

		// ---- Combo hit 1: one swing + recovery, engine timing ---------------
		// swingT = 1 - actionProgress. Two segments:
		//   swing    [0, swingHitEnd) : one eased diagonal cut
		//   recovery [hitEnd, 1]      : glide back to idle; combo 2 window
		// Rotations in RADIANS around the handle pivot.
		private float swingHitEnd = 0.90F;
		private Vector3 swingStartRot = Vector3(-1.2F, 0.9F, -0.8F);
		private Vector3 swingEndRot = Vector3(1.0F, -0.9F, 0.6F);

		// Swing path (view frame: x = left, y = forward, z = down).
		// The sword snaps to the START anchor (upper-right, +0.3 depth push
		// keeps the arms from filling the screen), sweeps to the END anchor
		// (lower-left), then recovery glides back to idle.
		private Vector3 swingStartOffset = Vector3(-0.35F, 0.3F, -0.8F);
		private Vector3 swingEndOffset = Vector3(0.35F, 0.3F, 0.45F);

		// Blade twist around its own length during the cut (not in idle,
		// eases out with recovery, mirrored hit twists the other way).
		private float swingBladeTwist = -0.7854F;  // -45 deg, rad

		// Hand anchors: hands attach at (0, 0, 2) and (0, 0, 9) on the grip;
		// offsets in MODEL units (1 unit = 0.033 m on screen). Axes at idle:
		// x = left/right, y = forward/back, z = along the blade.
		private Vector3 rightHandOffset = Vector3(0.0F, -3.0F, 4.0F);
		private Vector3 leftHandOffset = Vector3(0.0F, 0.0F, 1.0F);
		// Pivot BEHIND the grip so the rotation carries both hands.
		private Vector3 swingPivot = Vector3(0.0F, 0.0F, -4.0F);
		// Pitch of the whole swing arc: +90 deg turns the sweep into a
		// descending diagonal cut (0 = untilted).
		private float swingPlaneTilt = 1.5708F;

		// ---- Idle stance -----------------------------------------------------
		private Vector3 idleRot = Vector3(0.0F, 0.08F, -0.12F);
		private Vector3 idleOffset = Vector3(0.0F, 0.15F, 0.0F);
		private float idleBreathSpeed = 1.9F;    // rad/s
		private float idleBreathRot = 0.12F;     // roll amplitude (rad)
		private float idleBreathMove = 0.09F;    // bob amplitude
		private float animTime = 0.0F;           // drives the breathing
		private float idleBlend = 1.0F;          // micro-fade (~70 ms in)

		// ---- Combo chaining (stages 2/3 in v2/v3) ---------------------------
		// A new bash within comboChainTime seconds chains the next hit
		// (alternating mirror); slower clicks restart at 1. Based on OUR
		// clock (animTime): the engine's repeated bashes are unreliable
		// to read from progress alone.
		private float comboChainTime = 1.0F;   // seconds
		private int comboStage = 0;            // 0 = neutral, 1..3 = hit index
		private bool swingMirrored = false;    // hit 2 = mirror of hit 1
		private float swingT = 1.0F;           // current swing progress
		private bool bashWasActive = false;
		private float lastProgress = 0.0F;     // new-bash detection
		private float lastBashT = -10.0F;      // chain detection (our clock)
		// combo reset only after this grace delay: chained bashes can have
		// 1-frame gaps that would otherwise reset the combo every time
		private float comboResetT = 0.0F;
		private float comboResetDelay = 0.4F;  // seconds

		// ---- Block (right click held = Dig) ---------------------------------
		// Horizontal guard stance, random tilt re-rolled per block/estoc.
		private float blockMaxAngle = 180.0F;  // degrees, each side
		private float blockAngle = 0.0F;       // radians, rolled per block
		private Vector3 blockRot = Vector3(0.12F, 0.35F, 1.40F); // ~80 deg diagonal
		private Vector3 blockOffset = Vector3(0.18F, 0.25F, -0.12F);

		// ---- Thrust cycle (right click held = Dig) --------------------------
		// Synced to the engine: while held, the engine loops Dig actions
		// (actionProgress 0 -> 1). One estoc per dig cycle, driven by that
		// progress (zero drift). The cycle duration is MEASURED at runtime
		// (wrap-to-wrap, fallback 0.5 s) and the phases are defined in
		// SECONDS, auto-scaled down if they would overflow the cycle.
		// APEX LANDS ON THE ENGINE WRAP (= voxel disappearance). Schedule:
		// hold -> retract (blade rises) -> windup (blade lays flat) -> estoc.
		private float thrustCycle = 1.2F;        // fallback clock only (s)
		private float thrustRetractTime = 0.10F; // retract, blade rises
		private float thrustWindupTime = 0.20F;  // pull back, blade lays flat
		private float thrustTime = 0.17F;        // the FAST estoc itself
		private float thrustHoldTime = 0.03F;    // hold at full reach
		private float thrustDropZ = 0.22F;       // sink while blade is flat
		private float digCycleDur = 0.5F;        // measured engine cycle (s)
		private float digClock = 0.0F;           // time since the last wrap
		private float lastDigProgress = -1.0F;
		private float thrustPhase = 0.0F;        // 0..1, engine-locked
		private float thrustWindup = 0.25F;      // pull-back distance
		private float thrustReach = 0.35F;       // push past the guard
		private float thrustWindupZ = -0.05F;    // slight raise on the windup
		private float thrustReachZ = 0.08F;      // slight dip at full reach
		private float thrustLayRot = 1.5708F;    // 90 deg = blade fully flat
		private float blockAnimT = 0.0F;         // hold time (fallback clock)
		private float lastFallPhase = 0.0F;      // twist reroll mark (fallback)
		private float blockBlend = 0.0F;         // 0 = idle, 1 = full block
		private bool blockWasActive = false;

		// Portable euler rotation: CreateEulerAnglesMatrix is not registered
		// in every build (missing in ZeroSpades X 0.3.4), so the poses are
		// applied as three axis rotations instead.
		Matrix4 EulerRot(Vector3 r) {
			return CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), r.x)
				* CreateRotateMatrix(Vector3(0.0F, 1.0F, 0.0F), r.y)
				* CreateRotateMatrix(Vector3(0.0F, 0.0F, 1.0F), r.z);
		}

		ViewSpadeSwordSkin(Renderer@ r, AudioDevice@ dev) {
			@renderer = r;
			@audioDevice = dev;
			@spadeModel	= renderer.RegisterModel("WeaponMods/SpadeLvl2/Models/Weapons/Spade/Spade.kv6");
			@pickaxeModel = renderer.RegisterModel("Models/Weapons/Spade/Pickaxe.kv6");
			// RegisterModel THROWS a fatal exception when the file is missing
			// (unlike RegisterImage, which silently returns null). Guard and
			// fall back to the vanilla spade model.
			try {
				@swordModel = renderer.RegisterModel(swordModelPath);
			} catch {
				@swordModel = null;
			}
			@model = @spadeModel;
			@sightImage = renderer.RegisterImage("Gfx/Sight.tga");
		}

		// ---- Rainbow effect on the custom-colored voxels --------------------
		// Time-cycling hue fed to param.customColor. In the kv6, the voxels
		// that take customColor are the ones painted NEUTRAL GREY (128,128,128)
		// in slab6 (same mechanism as team colors).
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

		Vector3 RainbowRGB(float speed) {
			speed = 0.002F * speed;
			auto h = (int(animTime * 1000) % int(1.0F / speed)) * speed;
			return HSV2RGB(h, 1.0F, 1.0F);
		}

		void Update(float dt) {
			animTime += dt;
			float sprintStateSS = sprintState * sprintState;
			if (sprintStateSS > sprintStateSmooth)
				sprintStateSmooth = Mix(sprintStateSmooth, sprintStateSS, 1.0F - pow(0.001F, dt));
			else
				sprintStateSmooth = sprintStateSS;

			// ---- combo / swing state machine ---------------------------
			bool bashActive = (actionType == spades::SpadeActionType::Bash);
			if (bashActive) {
				float t = 1.0F - actionProgress;   // 0 = click, 1 = done
				// New-bash detection: while the fire button is HELD the engine
				// chains bashes back-to-back (actionType stays Bash, no gap),
				// so a new swing = progress JUMPING back toward 1.
				bool newBash = (not bashWasActive)
					or (actionProgress > lastProgress + 0.3F);
				if (newBash) {
					comboResetT = 0.0F;
					if (animTime - lastBashT < comboChainTime
						and comboStage > 0 and comboStage < 3) {
						comboStage++;
						swingMirrored = not swingMirrored;
					} else {
						comboStage = 1;
						swingMirrored = false;
					}
					lastBashT = animTime;
				}
				lastProgress = actionProgress;
				swingT = t;
			} else {
				// Bash over: finish the animation on our own clock (the engine
				// may end the bash without progress reaching exactly 0, which
				// used to freeze swingT below 1 forever and kill the idle).
				if (swingT < 1.0F)
					swingT = Min(1.0F, swingT + dt * 3.0F);
				if (swingT >= 1.0F) {
					comboResetT += dt;
					if (comboResetT > comboResetDelay)
						comboStage = 0;
				}
			}
			bashWasActive = bashActive;

			// ---- block state machine ------------------------------------
			bool blockActive = (actionType == spades::SpadeActionType::DigStart
				or actionType == spades::SpadeActionType::Dig);
			if (blockActive and !blockWasActive) {
				blockAngle = (GetRandom() * 2.0F - 1.0F) * blockMaxAngle * PiF / 180.0F;
			}
			blockWasActive = blockActive;
			// Thrust phase: locked to the engine dig cycle while a Dig runs
			// (one estoc per dig, exact sync); fallback clock otherwise
			// (DigStart, engine gaps) so the motion never freezes.
			if (blockActive) {
				blockAnimT += dt;
				if (actionType == spades::SpadeActionType::Dig) {
					// measure the cycle wrap-to-wrap, then stay locked on it
					if (lastDigProgress >= 0.0F
						and actionProgress < lastDigProgress) {
						digCycleDur = Max(digClock, 0.1F);
						digClock = 0.0F;
						// fresh tilt for the new thrust
						blockAngle = (GetRandom() * 2.0F - 1.0F)
							* blockMaxAngle * PiF / 180.0F;
					}
					digClock += dt;
					lastDigProgress = actionProgress;
					thrustPhase = actionProgress;
				} else {
					thrustPhase = blockAnimT / thrustCycle;
					// fallback clock: reroll the twist at each wrap too
					if (thrustPhase >= lastFallPhase + 1.0F) {
						lastFallPhase = thrustPhase;
						blockAngle = (GetRandom() * 2.0F - 1.0F)
							* blockMaxAngle * PiF / 180.0F;
					}
				}
			} else {
				blockAnimT = 0.0F;
				thrustPhase = 0.0F;
				digClock = 0.0F;
				lastDigProgress = -1.0F;
				lastFallPhase = 0.0F;
			}
			float blockTarget = blockActive ? 1.0F : 0.0F;
			// fast chase (~80 ms full transition), no jump
			float blockStep = dt / 0.08F;
			if (blockBlend < blockTarget)
				blockBlend = Min(blockBlend + blockStep, blockTarget);
			else
				blockBlend = Max(blockBlend - blockStep, blockTarget);

			// ---- idle blend -------------------------------
			// OUT is INSTANT (maximizes the visible cut time);
			// IN fades quickly (~70 ms) when the sword settles back.
			bool idleActive = not (bashActive
				or (comboStage >= 1 and swingT < 1.0F)
				or blockActive);
			if (idleActive) {
				float idleStep = dt / 0.07F;
				idleBlend = Min(idleBlend + idleStep, 1.0F);
			} else {
				idleBlend = 0.0F;
			}
		}

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(swordScale);

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			if (sprintStateSmooth > 0.0F or raiseState < 1.0F) {
				float per = Max(sprintStateSmooth, 1.0F - raiseState);
				mat = CreateRotateMatrix(Vector3(0.0F, 1.0F, 0.0F), per * 1.3F) * mat;
				mat = CreateTranslateMatrix(Vector3(0.3F, -0.4F, -0.1F) * per) * mat;
			}
			mat = CreateTranslateMatrix(0.0F, (1.0F - raiseState) * -0.3F, 0.0F) * mat;

			// model selection: sword for every action, vanilla spade/pickaxe
			// as fallback when useSwordModel = false
			if (useSwordModel and not (swordModel is null)) {
				@model = @swordModel;
			} else if (actionType == spades::SpadeActionType::Bash) {
				@model = @pickaxeModel;
			} else {
				@model = @spadeModel;
			}

			// ---- IDLE: relaxed stance with breathing --------------------
			{
				float idleF = idleBlend * (1.0F - blockBlend);
				if (idleF > 0.001F) {
					float breath = sin(animTime * idleBreathSpeed);
					mat = mat
						* CreateTranslateMatrix(swingPivot)
						* EulerRot((idleRot
							+ Vector3(0.0F, 0.0F, breath * idleBreathRot))
							* idleF)
						* CreateTranslateMatrix(-swingPivot)
						* CreateTranslateMatrix((idleOffset
							+ Vector3(0.0F, 0.0F, breath * idleBreathMove))
							* idleF);
				}
			}

			// ---- COMBO HIT 1: one swing + recovery ----------------------
			if (comboStage >= 1 and swingT < 1.0F) {
				Vector3 rot;
				Vector3 move(0.0F, 0.0F, 0.0F);
				float tilt;
				// Hit 2 = mirror across the vertical plane: negate YAW and
				// ROLL (y, z) of the rotations - NOT the pitch (x), which
				// would flip the blade to point backward - and negate x of
				// the path anchors (left <-> right).
				Vector3 startRot = swingStartRot;
				Vector3 endRot = swingEndRot;
				Vector3 startOff = swingStartOffset;
				Vector3 endOff = swingEndOffset;
				if (swingMirrored) {
					startRot.y = -startRot.y; startRot.z = -startRot.z;
					endRot.y = -endRot.y;     endRot.z = -endRot.z;
					startOff.x = -startOff.x; endOff.x = -endOff.x;
				}
				if (swingT < swingHitEnd) {
					// the swing: one eased path between the two anchors
					float pose = SmoothStep(swingT / swingHitEnd);
					rot = Mix(startRot, endRot, pose);
					// blade twist around its own length during the cut
					rot.z = rot.z + (swingMirrored
						? -swingBladeTwist : swingBladeTwist);
					move = Mix(startOff, endOff, pose);
					tilt = 1.0F;
				} else {
					// recovery: glide back to idle (all ease out together)
					float pose = SmoothStep((swingT - swingHitEnd)
						/ (1.0F - swingHitEnd));
					rot = endRot * (1.0F - pose);
					rot.z = rot.z + (swingMirrored
						? -swingBladeTwist : swingBladeTwist)
						* (1.0F - pose);
					move = endOff * (1.0F - pose);
					tilt = 1.0F - pose;
				}
				mat = mat
					* CreateTranslateMatrix(swingPivot)
					// tilt the arc: horizontal sweep -> descending cut
					* EulerRot(Vector3(swingPlaneTilt * tilt, 0.0F, 0.0F))
					* EulerRot(rot)
					* CreateTranslateMatrix(-swingPivot);
				// Whole-weapon movement, view-aligned, applied OUTSIDE the
				// rotation block. NOTE the order: T(move) must be LEFT of
				// mat (leftmost = applied last = outer frame); mat * T(move)
				// would apply it in MODEL space, rotated by the swing.
				mat = CreateTranslateMatrix(move) * mat;
			}

			// ---- BLOCK: guard stance + thrust cycle ---------------------
			if (blockBlend > 0.001F) {
				Vector3 rot = blockRot;
				rot.z = blockRot.z + blockAngle;   // diagonal base + random tilt
				// Schedule in seconds across the measured engine cycle,
				// apex on the wrap:
				//   [0, hEnd)      hold at full reach (apex just hit)
				//   [hEnd, rEnd)   retract, blade rises VERTICAL
				//   [rEnd, wEnd)   pull back, blade lays FLAT
				//   [tStart, dur)  estoc, apex ON the wrap
				float cyc = thrustPhase;
				while (cyc >= 1.0F) cyc = cyc - 1.0F;   // wrap fallback clock
				float sched = thrustHoldTime + thrustRetractTime
					+ thrustWindupTime + thrustTime;
				float sc = 1.0F;
				if (sched > digCycleDur * 0.98F)
					sc = digCycleDur * 0.98F / sched;
				float hEnd = thrustHoldTime * sc;
				float rEnd = hEnd + thrustRetractTime * sc;
				float wEnd = rEnd + thrustWindupTime * sc;
				float tStart = digCycleDur - thrustTime * sc;
				float t = cyc * digCycleDur;
				Vector3 thrustMove(0.0F, 0.0F, 0.0F);
				float lay = 1.0F;   // 1 = flat forward, 0 = vertical
				if (t < hEnd) {
					// hold at full reach right after the apex
					thrustMove = Vector3(0.0F, thrustReach, thrustReachZ);
					lay = 1.0F;
				} else if (t < rEnd) {
					// retract: back from the reach, blade stands back up
					float p = SmoothStep((t - hEnd)
						/ Max(rEnd - hEnd, 0.001F));
					thrustMove = Vector3(0.0F, thrustReach, thrustReachZ)
						* (1.0F - p);
					lay = 1.0F - p;
				} else if (t < wEnd) {
					// windup: pull back, blade lays flat (guard)
					float p = SmoothStep((t - rEnd)
						/ Max(wEnd - rEnd, 0.001F));
					thrustMove = Vector3(0.0F,
						-thrustWindup * p, thrustWindupZ * p);
					lay = p;
				} else if (t >= tStart) {
					// the estoc: sharp forward, apex ON the wrap
					float p = SmoothStep((t - tStart)
						/ Max(digCycleDur - tStart, 0.001F));
					thrustMove = Mix(
						Vector3(0.0F, -thrustWindup, thrustWindupZ),
						Vector3(0.0F, thrustReach, thrustReachZ), p);
					lay = 1.0F;
				} else {
					// cycle longer than the schedule: stay wound up
					// until the estoc window opens
					thrustMove = Vector3(0.0F, -thrustWindup, thrustWindupZ);
					lay = 1.0F;
				}
				// sink below the eye line while the blade is flat, so the
				// blade stays visible under the view center
				thrustMove.z = thrustMove.z + thrustDropZ * lay;
				mat = mat
					* CreateTranslateMatrix(swingPivot)
					* EulerRot(Vector3(thrustLayRot * lay * blockBlend,
						0.0F, 0.0F))
					* EulerRot(rot * blockBlend)
					* CreateTranslateMatrix(-swingPivot)
					* CreateTranslateMatrix(blockOffset * blockBlend);
				// thrust movement in the view frame (y = forward), outside
				// the rotation block, same rule as the swing path
				mat = CreateTranslateMatrix(thrustMove * blockBlend) * mat;
			}

			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);
			trans += Vector3(-0.3F, 0.7F, 0.3F);

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue;
			trans.y += cg_viewWeaponY.FloatValue;
			trans.z += cg_viewWeaponZ.FloatValue;
			trans.x *= abs(weapSide);

			// Engine swing vector: the engine animates its own thrust during
			// a bash; our combo swing already covers that motion, so drop it
			// while the swing plays (otherwise two swings per click).
			if (not (comboStage >= 1 and swingT < 1.0F))
				trans += swing;

			mat = CreateTranslateMatrix(trans) * mat;

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			// hands offset (model units; see rightHandOffset for the axes)
			leftHand = mat * (Vector3(0.0F, 0.0F, 9.0F) + leftHandOffset);
			rightHand = mat * (Vector3(0.0F, 0.0F, 2.0F) + rightHandOffset);

			ModelRenderParam param;
			param.depthHack = true;
			param.matrix = eyeMatrix * mat;
			param.customColor = RainbowRGB(n_rgbSpeed.FloatValue);
			renderer.AddModel(model, param);
		}

		void Draw2D() {
			if (!drawSightImage)
				return;
			renderer.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			renderer.DrawImage(sightImage,
				Vector2((renderer.ScreenWidth - sightImage.Width) * 0.5F,
						(renderer.ScreenHeight - sightImage.Height) * 0.5F));
		}
	}

}
