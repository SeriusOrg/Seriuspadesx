/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include "BodyBurstEntity.h"
#include "Client.h"
#include "GameConstants.h"
#include "GameMap.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IRenderer.h"
#include "ParticleSpriteEntity.h"
#include "Player.h"
#include "PlayerOutfit.h"
#include "Weapon.h"
#include "World.h"
#include <Core/Settings.h>

#include <algorithm>
#include <iterator>

// Comic gore: an explosion in this mode takes the whole body apart, the
// four limbs and fat chunks of it flying rather than a ragdoll. Enabled by
// default; missing models and sound degrade gracefully.
DEFINE_SPADES_SETTING(cg_bodyBurstGore, "1");
// The volume of the pop, the skull burst's own word for it.
SPADES_SETTING(cg_headshotGoreVolume);
SPADES_SETTING(cg_particles);
SPADES_SETTING(cg_blood);
SPADES_SETTING(cg_customPlayerModels);
SPADES_SETTING(cg_classicPlayerModels);

namespace spades {

	namespace client {
		namespace {
			// total piece lifetime, in seconds: the limbs outlast the
			// chunks, for the arms and the legs are the whole corpse
			// this death leaves
			constexpr float LIMB_LIFETIME = 12.0F;
			constexpr float CHUNK_LIFETIME = 8.0F;
			// gravity (the z axis points down), same as GunCasing
			constexpr float CHUNK_GRAVITY = 32.0F;
			// velocity kept after a bounce
			constexpr float CHUNK_RESTITUTION = 0.46F;
			// burst sound / blood splat range (squared)
			constexpr float BURST_DIST_SQ = 40.0F * 40.0F;

			// Every burst's pieces at once, at most: the limbs are whole
			// player models drawn, and a busy wave bursts several bodies
			// a second. A burst over the cap gets what is left, or none.
#if defined(__ANDROID__)
			constexpr int kMaxLivePieces = 48;
#else
			constexpr int kMaxLivePieces = 256;
#endif
			int livePieces = 0;

			// the chunk models of the skull burst, this burst wears the
			// same ones twice as broad
			const char* const kChunkModels[] = {
				"Models/Effects/HeadBurst/Chunk1.kv6",
				"Models/Effects/HeadBurst/Chunk2.kv6",
				"Models/Effects/HeadBurst/Chunk3.kv6",
				"Models/Effects/HeadBurst/Chunk4.kv6",
				"Models/Effects/HeadBurst/Chunk5.kv6",
				"Models/Effects/HeadBurst/Chunk6.kv6",
				"Models/Effects/HeadBurst/Chunk7.kv6",
			};

			// burst sound files, the skull burst's own
			const char* const kBurstSounds[] = {
				"Sounds/Effects/HeadBurst/HeadBurst1.opus",
				"Sounds/Effects/HeadBurst/HeadBurst2.opus",
				"Sounds/Effects/HeadBurst/HeadBurst3.opus",
				"Sounds/Effects/HeadBurst/HeadBurst4.opus",
			};
		}

		BodyBurstEntity::BodyBurstEntity(Client& client, Player& victim,
		                                 Vector3 origin, Vector3 blastDir,
		                                 const PlayerOutfits* models)
		    : client(&client), renderer(client.GetRenderer()), origin(origin) {
			// The words the corpse of the fallen would wear (Corpse.cpp):
			// the model set, the colour the outfit was worn in, the
			// folder its own limbs are drawn from.
			std::string modelSet;
			if (models && cg_customPlayerModels)
				modelSet =
				  models->GetModelName(victim.GetId(), victim.GetTeamId());
			IntVector3 worn = victim.GetColor();
			if (models) {
				const int team =
				  models->GetColourTeam(victim.GetId(), victim.GetTeamId());
				if (team != victim.GetTeamId() && (team == 0 || team == 1))
					worn = victim.GetWorld().GetTeamColor(team);
			}
			color = ConvertColorRGB(worn);
			std::string modelPath = "Models/Player/";
			if (victim.IsZombie())
				modelPath += "Zombie/";
			else if (!cg_classicPlayerModels)
				modelPath += victim.GetWeapon().GetName() + "/";
			const auto part = [&](const char* name) {
				const std::string usual = modelPath + name + ".kv6";
				return models ? models->ResolvePart(modelSet, name, usual)
				              : usual;
			};

			// The four limbs of the fallen, the two arms and the two
			// legs, each a piece of its own: a missing part leaves its
			// piece out and the chunks stand alone.
			for (const char* name : {"ArmRight", "Arm", "LegRight", "Leg"}) {
				try {
					limbModels.push_back(
					  renderer.RegisterModel(part(name).c_str()));
				} catch (...) {
					// model file missing: skip it
				}
			}
			// The chunks of the rest of it, the skull burst's own worn
			// twice as broad.
			for (const char* name : kChunkModels) {
				try {
					chunkModels.push_back(renderer.RegisterModel(name));
				} catch (...) {
					// model file missing: skip it
				}
			}

			const float distSqr =
			  (client.GetLastSceneDef().viewOrigin - origin)
			    .GetSquaredLength2D();

			// The blood said at the stroke itself: a fountain of it at
			// the body's heart, thicker than the splats its pieces land
			// in, for a burst without its red is a fireworks show.
			if ((bool)cg_particles && distSqr < BURST_DIST_SQ) {
				Handle<IImage> img = renderer.RegisterWhiteImage();
				uint32_t col = IntVectorToColor(MakeIntVector3(127, 0, 0));
				col = client.GetWorld()->GetMap()->GetColorJit(col);
				const Vector4 blood = ConvertColorRGBA(IntVectorFromColor(col));
				const int sprays = SampleRandomInt(28, 44);
				for (int i = 0; i < sprays; i++) {
					auto ent =
					  stmp::make_unique<ParticleSpriteEntity>(client, img, blood);
					ent->SetTrajectory(origin,
					                   (RandomVector() +
					                    MakeVector3(0, 0, -0.6F)) *
					                     7.0F);
					ent->SetRadius(0.5F);
					ent->SetLifeTime(3.0F, 0.0F, 1.0F);
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
					client.AddLocalEntity(std::move(ent));
				}
			}

			// One of the burst sounds, the skull burst's own pick; silent
			// when it is not installed.
			if (!client.IsMuted() && distSqr < BURST_DIST_SQ) {
				const int pick = SampleRandomInt(
				  0, static_cast<int>(std::size(kBurstSounds)) - 1);
				Handle<IAudioChunk> sound;
				try {
					sound = client.GetAudioDevice().RegisterSound(kBurstSounds[pick]);
				} catch (...) {
					// sound file missing: stay silent
				}
				if (sound) {
					AudioParam param;
					// volume is tunable so the burst is heard over the boom
					param.volume = static_cast<float>(cg_headshotGoreVolume);
					// larger reference distance keeps the burst audible at range
					param.referenceDistance = 4.0F;
					param.pitch = 0.9F + SampleRandomFloat() * 0.2F;
					client.GetAudioDevice().Play(sound.GetPointerOrNull(),
					                             origin, param);
				}
			}

			if (limbModels.empty() && chunkModels.empty())
				return;

			// The burst rides out along the blast, the same fountain the
			// skull burst rides, its bias along the shot.
			Vector3 dir = blastDir.Normalize();
			Vector3 up = MakeVector3(0, 0, 1);
			Vector3 right;
			if (dir.x * dir.x + dir.y * dir.y < 0.000001F) {
				// blast is vertical: pick any horizontal axis instead
				right = MakeVector3(1, 0, 0);
			} else {
				right = Vector3::Cross(dir, up).Normalize();
			}
			Vector3 upAdj = Vector3::Cross(right, dir).Normalize();

			// The pieces the cap leaves room for: the limbs first, heavy
			// and ragged, then the chunks of the rest, between seven and
			// fourteen of them, twice the size the skull bursts its own.
			int room = kMaxLivePieces - livePieces;
			if (room <= 0)
				return;
			for (size_t i = 0; i < limbModels.size() && room > 0; ++i) {
				Piece p;
				p.matrix = Matrix4::Translate(origin);
				p.velocity = dir * (2.0F + SampleRandomFloat() * 3.0F)
				             + right * (SampleRandomFloat() - SampleRandomFloat()) * 4.0F
				             + upAdj * (SampleRandomFloat() - SampleRandomFloat()) * 4.0F
				             // upward geyser (the z axis points down)
				             + MakeVector3(0, 0, -1) * (4.0F + SampleRandomFloat() * 7.0F);
				p.angVel = MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
				                       SampleRandomFloat() - SampleRandomFloat(),
				                       SampleRandomFloat() - SampleRandomFloat()) * 10.0F;
				p.scale = 0.1F; // player models are authored tenfold
				p.life = LIMB_LIFETIME;
				p.modelIndex = static_cast<int>(i);
				p.limb = true;
				p.splatDone = false;
				pieces.push_back(p);
				--room;
			}
			if (!chunkModels.empty()) {
				int numChunks = std::min(SampleRandomInt(7, 14), room);
				for (int i = 0; i < numChunks; ++i) {
					Piece p;
					p.matrix = Matrix4::Translate(origin);
					p.velocity = dir * (1.5F + SampleRandomFloat() * 3.5F)
					             + right * (SampleRandomFloat() - SampleRandomFloat()) * 3.0F
					             + upAdj * (SampleRandomFloat() - SampleRandomFloat()) * 3.0F
					             + MakeVector3(0, 0, -1) * (3.0F + SampleRandomFloat() * 8.0F);
					p.angVel = MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
					                       SampleRandomFloat() - SampleRandomFloat(),
					                       SampleRandomFloat() - SampleRandomFloat()) * 8.0F;
					p.scale = 0.05F + SampleRandomFloat() * 0.04F;
					p.life = CHUNK_LIFETIME;
					p.modelIndex = SampleRandomInt(0, (int)chunkModels.size() - 1);
					p.limb = false;
					p.splatDone = false;
					pieces.push_back(p);
				}
			}
			if (pieces.empty())
				return;
			counted = static_cast<int>(pieces.size());
			livePieces += counted;
		}

		BodyBurstEntity::~BodyBurstEntity() { livePieces -= counted; }

		bool BodyBurstEntity::Update(float dt) {
			if (pieces.empty())
				return false;

			// fog cull on the burst origin
			float distSqr =
			  (client->GetLastSceneDef().viewOrigin - origin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return false;

			const Handle<GameMap>& map = client->GetWorld()->GetMap();
			SPAssert(map);

			bool anyAlive = false;
			for (auto& p : pieces) {
				if (p.life <= 0.0F)
					continue;

				p.life -= dt;

				Matrix4 lastMat = p.matrix;

				// tumble
				p.matrix = p.matrix * Matrix4::Rotate(MakeVector3(1, 0, 0), p.angVel.x * dt);
				p.matrix = p.matrix * Matrix4::Rotate(MakeVector3(0, 1, 0), p.angVel.y * dt);
				// move in world space (left-multiply, same as GunCasing:
				// right-multiplying would bend the path through the tumble)
				p.matrix = Matrix4::Translate(p.velocity * dt) * p.matrix;
				p.velocity.z += CHUNK_GRAVITY * dt;

				IntVector3 lp = p.matrix.GetOrigin().Floor();

				// dropped into water: vanish with a blood splash
				if (lp.z >= 63) {
					p.life = 0.0F;
					if (!p.splatDone) {
						p.splatDone = true;
						if (distSqr < BURST_DIST_SQ)
							BloodSplat(*map, p.matrix.GetOrigin(), p.velocity);
					}
					continue;
				}

				if (map->ClipWorld(lp.x, lp.y, lp.z)) {
					// bounced off a surface (GunCasing collision logic)
					IntVector3 lp2 = lastMat.GetOrigin().Floor();
					if (lp.z != lp2.z &&
					    ((lp.x == lp2.x && lp.y == lp2.y) ||
					     !map->ClipWorld(lp.x, lp.y, lp2.z))) {
						p.velocity.z = -p.velocity.z;
					} else if (lp.x != lp2.x &&
					           ((lp.y == lp2.y && lp.z == lp2.z) ||
					            !map->ClipWorld(lp2.x, lp.y, lp.z))) {
						p.velocity.x = -p.velocity.x;
					} else if (lp.y != lp2.y &&
					           ((lp.x == lp2.x && lp.z == lp2.z) ||
					            !map->ClipWorld(lp.x, lp2.y, lp.z))) {
						p.velocity.y = -p.velocity.y;
					} else {
						// stuck in geometry: drop this piece
						p.life = 0.0F;
						continue;
					}

					p.matrix = lastMat;
					p.velocity *= CHUNK_RESTITUTION;
					p.angVel *= CHUNK_RESTITUTION;

					// blood mark where the piece lands (first impact only)
					if (!p.splatDone) {
						p.splatDone = true;
						if (distSqr < BURST_DIST_SQ)
							BloodSplat(*map, lastMat.GetOrigin(), p.velocity);
					}
				}

				if (p.life > 0.0F)
					anyAlive = true;
			}

			return anyAlive;
		}

		void BodyBurstEntity::Render3D() {
			if (pieces.empty())
				return;

			ModelRenderParam param;
			for (const auto& p : pieces) {
				if (p.life <= 0.0F)
					continue;
				param.matrix = p.matrix * Matrix4::Scale(p.scale);
				// the limbs in the colour of the fallen, the chunks in
				// their own
				param.customColor = p.limb ? color : MakeVector3(0, 0, 0);
				renderer.RenderModel(p.limb ? *limbModels[p.modelIndex]
				                            : *chunkModels[p.modelIndex],
				                    param);
			}
		}

		void BodyBurstEntity::BloodSplat(GameMap& map, Vector3 pos, Vector3 vel) {
			// Client::Bleed is private; reproduce its particle look here
			if (!cg_particles)
				return;

			// persistent marks on the blocks (same gate as vanilla spatter)
			if ((int)cg_blood >= 2)
				client->SpatterBlood(pos, vel);

			Handle<IImage> img = renderer.RegisterWhiteImage();
			uint32_t col = IntVectorToColor(MakeIntVector3(127, 0, 0));
			col = map.GetColorJit(col); // jit the colour
			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			int splats = SampleRandomInt(7, 14);
			for (int i = 0; i < splats; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*client, img, color);
				Vector3 velBias = MakeVector3(0, 0, -0.5F);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 4.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				client->AddLocalEntity(std::move(ent));
			}
		}
	} // namespace client
} // namespace spades
