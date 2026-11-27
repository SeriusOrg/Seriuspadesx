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

 namespace spades {
	class ViewSpadeSaberSkin : IToolSkin, IViewToolSkin, ISpadeSkin {
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
		bool IsMuted { set {} } // nothing to do
		Matrix4 EyeMatrix { set { eyeMatrix = value; } }
		Vector3 Swing { set { swing = value; } }
		Vector3 LeftHandPosition { get { return leftHand; } }
		Vector3 RightHandPosition { get { return rightHand; } }
		SpadeActionType ActionType { set { actionType = value; } }
		float ActionProgress { set { actionProgress = value; } }

		private Renderer@ renderer;
		private AudioDevice@ audioDevice;
		private Model@ model;
		private Image@ sightImage;
		private Image@ ballImage;

		private float currentTime;
		
		protected ConfigItem cg_viewWeaponX("cg_viewWeaponX");
		protected ConfigItem cg_viewWeaponY("cg_viewWeaponY");
		protected ConfigItem cg_viewWeaponZ("cg_viewWeaponZ");
		protected ConfigItem cg_viewWeaponSide("cg_viewWeaponSide");
		
		protected ConfigItem gg1("gg1", "1");
		protected ConfigItem gg2("gg2", "1");
		protected ConfigItem gg3("gg3", "1");
		protected ConfigItem gg4("gg4", "1");
		protected ConfigItem gg5("gg5", "1");
		protected ConfigItem gg6("gg6", "1");
		protected ConfigItem gg7("gg7", "1");
		protected ConfigItem gg8("gg8", "0");
		protected ConfigItem gg9("gg9", "0");
		
		ViewSpadeSaberSkin(Renderer@ r, AudioDevice@ dev) {
			@renderer = r;
			@audioDevice = dev;
			@model = renderer.RegisterModel("WeaponMods/SpadeLvl3/Models/Weapons/Spade/Spade.kv6");
			@sightImage = renderer.RegisterImage("Gfx/Sight.tga");
			@ballImage = renderer.RegisterImage("Gfx/Ball.png");
			
			currentTime = 0.0F;
		}

		void Update(float dt) {
			 currentTime += dt;
		
			float sprintStateSS = sprintState * sprintState;
			if (sprintStateSS > sprintStateSmooth)
				sprintStateSmooth = Mix(sprintStateSmooth, sprintStateSS, 1.0F - pow(0.001F, dt));
			else
				sprintStateSmooth = sprintStateSS;
		}

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.033F);
			
			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			if (sprintStateSmooth > 0.0F or raiseState < 1.0F) {
				float per = Max(sprintStateSmooth, 1.0F - raiseState);
				mat = CreateRotateMatrix(Vector3(0.0F, -5.0F, 0.0F), per * 1.3F) * mat;
				mat = CreateTranslateMatrix(Vector3(0.0F, 0.0F, 0.5F) * per) * mat;
			}
			mat = CreateTranslateMatrix(0.0F, (1.0F - raiseState) * -0.3F, 0.0F) * mat;

			if (actionType == spades::SpadeActionType::Bash) {
				float per = 1.0F - actionProgress;
				mat = CreateRotateMatrix(Vector3(0, 1, 0), (0.5F - per) * 1.5F) * mat;
				mat = CreateTranslateMatrix(0.5F * per, 0.0F, 0.0F) * mat;
				mat = CreateTranslateMatrix(0.0F, per * 0.1F, 0.0F) * mat;
			} else if (actionType == spades::SpadeActionType::DigStart or actionType == spades::SpadeActionType::Dig) {
				float per = actionProgress;

				// some tunes
				const float readyFront = -1.2F;
				const float digAngle = 0.6F;
				const float readyAngle = 0.6F;

				float angle;
				float front = readyFront;
				float side = 1.0F;

				if (per < 0.5F) {
					if (actionType == spades::SpadeActionType::DigStart) {
						// bringing to the dig position
						per = 4.0F * per * per;
						angle = per * readyAngle;
						side = per;
						front = per * readyFront;
					} else {
						// soon after digging
						angle = readyAngle;
						per = (0.5F - per) / 0.5F;
						per *= per;
						per *= per;
						angle += per * digAngle;
						front += per * 2.0F;
					}
				} else {
					per = (per - 0.5F) / 0.5F;
					per = 1.0F - (1.0F - per) * (1.0F - per);
					angle = readyAngle + per * digAngle;
					front += per * 2.0F;
				}

				mat = CreateRotateMatrix(Vector3(1, 0, 0), angle) * mat;
				mat = CreateRotateMatrix(Vector3(0, 0, 1), front * 0.125F) * mat;

				side *= 0.3F;
				front *= 0.1F;

				mat = CreateTranslateMatrix(side, front, front * 0.2F) * mat;
			}
			
			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);
			trans += Vector3(-0.4F, 1.0F, 0.15F);

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue;
			trans.y += cg_viewWeaponY.FloatValue;
			trans.z += cg_viewWeaponZ.FloatValue;
			trans.x *= abs(weapSide);

			// add weapon sway
			trans += swing;

			mat = CreateTranslateMatrix(trans) * mat;
			
			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			// hands offset
			leftHand = Vector3(0.0F, 0.0F, 0.0F);
			rightHand = mat * Vector3(-1.0F, -0.5F, 10.0F);

			ModelRenderParam param;
			param.depthHack = true;
			param.matrix = eyeMatrix * mat;
			renderer.AddModel(model, param);
		
			Vector4 baseColor = Vector4(0.3F, 0.3F, 1.0F, 0.0F); // blue
			//Vector4 baseColor = Vector4(1.0F, 0.3F, 0.3F, 0.0F); // red
			
			Vector3 p1 = param.matrix * Vector3(0, 0, 2.0F);
			Vector3 p2 = param.matrix * Vector3(0, 0, -30.0F);
			
			if (raiseState < 1.0F) {
				float per = (1.0F - raiseState) * 5.0F;
				p2 = param.matrix * Vector3(0, 0, -20.0F * (1.0F - per));
			}
		
			DynamicLightParam light;
			light.origin = p1;
			light.point2 = p2;
			light.color = Vector3(baseColor.x, baseColor.y, baseColor.z);
			light.color = light.color * GetRandom(0.7F, 1.0F);
			light.useLensFlare = true;
			light.radius = 3.0F;
			light.type = spades::DynamicLightType::Linear;
			renderer.AddLight(light);
			
			for (int i = 0; i < 4; ++i) {
				float thickness = 0.03F * pow(2.0F, i) + GetRandom(0.0F, 0.01F);
				float intensity = (1.0F - (i * 0.2F)) + GetRandom(0.0F, 0.05F);
				renderer.Color = baseColor * intensity;
				renderer.AddLongSprite(ballImage, p1, p2, thickness);
			}
		}

		void Draw2D() {
			renderer.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			renderer.DrawImage(sightImage,
				Vector2((renderer.ScreenWidth - sightImage.Width) * 0.5F,
						(renderer.ScreenHeight - sightImage.Height) * 0.5F));
		}
	}

}
