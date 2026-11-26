/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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

#include <cmath>
#include <cstdlib>
#include <iterator>

#include "Client.h"

#include <Core/ConcurrentDispatch.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

#include "IAudioChunk.h"
#include "IAudioDevice.h"

#include "BloodMarks.h"
#include "Corpse.h"
#include "ILocalEntity.h"
#include "ParticleSpriteEntity.h"
#include "SmokeSpriteEntity.h"

#include "GameMap.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"

DEFINE_SPADES_SETTING(cg_blood, "2");
DEFINE_SPADES_SETTING(cg_particles, "2");
DEFINE_SPADES_SETTING(cg_particlesBloodNum, "4");
DEFINE_SPADES_SETTING(cg_particlesGrenadeNum, "64");
DEFINE_SPADES_SETTING(cg_waterImpact, "1");

DEFINE_SPADES_SETTING(cg_corpseSoftLimit, "6");
DEFINE_SPADES_SETTING(cg_corpseHardLimit, "16");

SPADES_SETTING(cg_manualFocus);
DEFINE_SPADES_SETTING(cg_autoFocusSpeed, "0.4");

namespace spades {
	namespace client {

#pragma mark - Local Entities / Effects

		void Client::RemoveCorpses() {
			SPADES_MARK_FUNCTION();

			int numCorpses = static_cast<int>(corpses.size());
			if (numCorpses > (int)cg_corpseHardLimit) {
				if (corpses.front().get() == lastLocalCorpse)
					lastLocalCorpse = nullptr;
				corpses.pop_front();
			} else if (numCorpses > (int)cg_corpseSoftLimit) {
				RemoveInvisibleCorpses();
			}
		}

		void Client::RemoveInvisibleCorpses() {
			SPADES_MARK_FUNCTION();

			int numCorpses = static_cast<int>(corpses.size());
			int cnt = numCorpses - (int)cg_corpseSoftLimit;

			decltype(corpses)::iterator it;
			std::vector<decltype(it)> its;
			for (it = corpses.begin(); it != corpses.end(); it++) {
				if (cnt <= 0)
					break;

				auto& c = *it;
				if (c->IsVisibleFrom(lastSceneDef.viewOrigin))
					continue;

				if (c.get() == lastLocalCorpse)
					lastLocalCorpse = nullptr;
				its.push_back(it);
				cnt--;
			}

			for (const auto& it : its)
				corpses.erase(it);
		}

		void Client::RemoveCorpseForPlayer(int playerId) {
			for (auto it = corpses.begin(); it != corpses.end();) {
				auto cur = it;
				++it;

				auto& c = *cur;
				if (c->GetPlayerId() != playerId)
					continue;

				if (c.get() == lastLocalCorpse)
					lastLocalCorpse = nullptr;
				corpses.erase(cur);
			}
		}

		void Client::RemoveAllLocalEntities() {
			SPADES_MARK_FUNCTION();

			// remove all corpses
			lastLocalCorpse = nullptr;
			corpses.clear();

			damageIndicators.clear();
			soundFeedbackIndicators.clear();
			localEntities.clear();
			smokes.Clear(SmokeParams::kAnonymous);
			// A path, unlike a smoke, does not burn out on its own: it
			// belongs to the world that was sent it, and is owed its
			// clear when that world is replaced.
			trails.ForgetAll();
			grenadeTracers.clear();

			if (bloodMarks)
				bloodMarks->Clear();
		}

		stmp::optional<std::tuple<Player&, hitTag_t>> Client::HotTrackedPlayer(bool includeEnemies) {
			if (!IsFirstPerson(GetCameraMode()))
				return {};

			SPAssert(world);

			auto& camTarget = GetCameraTargetPlayer();

			Vector3 origin = camTarget.GetEye();
			Vector3 dir = camTarget.GetFront();
			World::WeaponRayCastResult res = world->WeaponRayCast(origin, dir, camTarget.GetId());

			if (res.hit == false || !res.playerId)
				return {};

			Player& p = world->GetPlayer(res.playerId.value()).value();

			// don't hot track enemies (non-spectator only)
			if (!includeEnemies && !camTarget.IsTeammate(p) && !camTarget.IsSpectator())
				return {};

			return std::tuple<Player&, hitTag_t>{p, res.hitFlag};
		}

		bool Client::IsMuted() {
			// prevent to play loud sound at connection
			// caused by saved packets
			return time - worldSetTime < 0.05F;
		}

		void Client::Bleed(spades::Vector3 pos, bool bounce) {
			SPADES_MARK_FUNCTION();

			if (!cg_blood)
				return;

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			Vector3 velBias = {0, 0, -0.5F};

			// fragments
			Handle<IImage> img = renderer->RegisterWhiteImage();

			uint32_t col = IntVectorToColor(MakeIntVector3(127, 0, 0));
			col = map->GetColorJit(col); // jit the colour

			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			int particlesNum = cg_particlesBloodNum;
			for (int i = 0; i < particlesNum; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 8.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ || bounce)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			color = MakeVector4(0.7F, 0.35F, 0.37F, 0.6F);
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 100.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, RandomVector() * 0.7F, 0.8F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.5F + SampleRandomFloat() * SampleRandomFloat() * 0.2F, 2.0F);
				ent->SetLifeTime(0.2F + SampleRandomFloat() * 0.2F, 0.06F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			color.w *= 0.1F;
			{
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 40.0F);
				ent->SetTrajectory(pos, RandomVector() * 0.7F, 0.8F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.7F + SampleRandomFloat() * SampleRandomFloat() * 0.2F, 2.0F, 0.1F);
				ent->SetLifeTime(0.8F + SampleRandomFloat() * 0.4F, 0.06F, 1.0F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::EmitBlockFragments(spades::Vector3 pos, IntVector3 col) {
			SPADES_MARK_FUNCTION();

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			// determines free space around for SetTrajectory
			Vector3 velBias = {0, 0, 0};
			if (!map->ClipBox(pos.x, pos.y, pos.z)) {
				if (map->ClipBox(pos.x + 1.0F, pos.y, pos.z))
					velBias.x -= 1.0F;
				if (map->ClipBox(pos.x - 1.0F, pos.y, pos.z))
					velBias.x += 1.0F;
				if (map->ClipBox(pos.x, pos.y + 1.0F, pos.z))
					velBias.y -= 1.0F;
				if (map->ClipBox(pos.x, pos.y - 1.0F, pos.z))
					velBias.y += 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z + 1.0F))
					velBias.z -= 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z - 1.0F))
					velBias.z += 1.0F;
			}

			// fragments
			Handle<IImage> img = renderer->RegisterWhiteImage();

			Vector4 color = ConvertColorRGBA(col);

			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				Vector3 dir = RandomVector() + velBias * 0.5F;
				ent->SetTrajectory(pos + dir * 0.2F, dir * 8.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			if (distSqr < 32.0F * 32.0F) {
				for (int i = 0; i < 8; i++) {
					auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
					ent->SetTrajectory(pos, RandomVector() * 12.0F, 1.0F, 0.9F);
					ent->SetRotation(SampleRandomFloat() * kTwoPi);
					ent->SetRadius(0.2F + SampleRandomFloat() * SampleRandomFloat() * 0.25F);
					ent->SetLifeTime(3.0F, 0.0F, 1.0F);
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
					localEntities.emplace_back(std::move(ent));
				}
			}

			color += (MakeVector4(1, 1, 1, 1) - color) * 0.2F;
			color.w *= 0.2F;
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 100.0F);
				ent->SetTrajectory(pos, RandomVector() * 0.7F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.2F, 0.8F);
				ent->SetLifeTime(0.3F + SampleRandomFloat() * 0.3F, 0.06F, 0.4F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::EmitBlockDestroyFragments(spades::IntVector3 pos) {
			SPADES_MARK_FUNCTION();

			if (!cg_particles)
				return;

			Vector3 origin = MakeVector3(pos) + 0.5F;

			// distance cull
			float distSqr = (origin - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			// fragments
			Handle<IImage> img = renderer->RegisterWhiteImage();

			uint32_t col = map->GetColor(pos.x, pos.y, pos.z);
			col = map->GetColorJit(col); // jit the colour
			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(origin, RandomVector() * 8.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::MuzzleFire(spades::Vector3 pos, bool smoke) {
			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			DynamicLightParam l;
			l.type = DynamicLightTypePoint;
			l.origin = pos;
			l.radius = 5.0F;
			l.color = MakeVector3(3.0F, 1.6F, 0.5F);
			flashDlights.push_back(l);

			if ((int)cg_particles < 2 || !smoke)
				return;

			Vector3 velBias = {0, 0, -0.5F};
			Vector4 color = MakeVector4(0.8F, 0.8F, 0.8F, 0.3F);

			// rapid smoke
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 120.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 0.3F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.4F, 3.0F, 0.0000005F);
				ent->SetLifeTime(0.2F + SampleRandomFloat() * 0.1F, 0.0F, 0.3F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// fire smoke
			color = MakeVector4(1.0F, 0.6F, 0.4F, 0.2F) * 5.0F;
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 120.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 0.3F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.2F + SampleRandomFloat() * SampleRandomFloat() * 0.3F, 3.0F, 0.0000005F);
				ent->SetLifeTime(0.01F + SampleRandomFloat() * 0.02F, 0.0F, 0.01F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::StartSmoke(const SmokeParams& params) {
			SPADES_MARK_FUNCTION();
			if (!world)
				return; // before the map: the server sends it again once it is in
			// A blast goes off at a stroke, not poured for a duration:
			// the clouds, the light and the boom, the dark climb.
			if (params.style == SmokeParams::Blast) {
				SpawnBlast(params.origin);
				return;
			}
			Handle<IAudioChunk> hiss;
			if (params.sound && !IsMuted())
				hiss = audioDevice->RegisterSound("Sounds/Misc/Smoke.opus");
			// Drawn whatever the particle setting: smoke can be cover, and turning
			// the particles down must not see through it.
			localEntities.emplace_back(stmp::make_unique<SmokeEntity>(
			  *this, audioDevice.GetPointerOrNull(), hiss, params, smokes.Start(params.id)));
		}

		namespace {
			// The brief bright light of a blast, decaying over half a
			// second, and the boom riding it. The boom plays in 2D with
			// a manual distance falloff: the fork's spatial path eats a
			// sound born at a block face, so the falloff is said by hand.
			class BlastFlashEntity : public ILocalEntity {
			public:
				BlastFlashEntity(Client& client, IAudioDevice* audio,
				                  Handle<IAudioChunk> boom, Vector3 origin);
				bool Update(float dt) override;
				void Render3D() override;

			private:
				Client& client;
				IAudioDevice* audio; // may be null when muted
				Handle<IAudioChunk> boom;
				Vector3 origin;
				float age = 0.0F;
			};

			BlastFlashEntity::BlastFlashEntity(Client& cli, IAudioDevice* audio,
			                                   Handle<IAudioChunk> boom,
			                                   Vector3 origin)
			    : client(cli), audio(audio), boom(std::move(boom)),
			      origin(origin) {
				if (audio && boom) {
					float dist = 1.0F;
					if (client.GetWorld()) {
						auto lp = client.GetWorld()->GetLocalPlayer();
						if (lp)
							dist = (origin - lp->GetEye()).GetLength();
					}
					// The volume the engine gives its own grenade
					// explosions, over a falloff that keeps the distant
					// ones audible and the close ones louder.
					AudioParam param;
					param.volume = 1.5F * 6.0F / (1.0F + dist / 12.0F);
					audio->PlayLocal(boom.GetPointerOrNull(), param);
				}
			}

			bool BlastFlashEntity::Update(float dt) {
				age += dt;
				return age < 0.5F;
			}

			void BlastFlashEntity::Render3D() {
				// Four times a rifle bullet flash, dying to nothing.
				const float per =
				  age >= 0.5F ? 1.0F : age / 0.5F;
				DynamicLightParam light;
				light.type = DynamicLightTypePoint;
				light.origin = origin;
				light.radius = 6.0F * (1.0F - per);
				light.color = MakeVector3(1.0F, 0.75F, 0.4F);
				client.GetRenderer().AddLight(light);
			}
		} // namespace

		void Client::SpawnBlast(const Vector3& pos) {
			SPADES_MARK_FUNCTION();
			// The burst clouds: wider and denser than a grenade's, and
			// they linger long after it.
			for (int i = 0; i < 8; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(
				  *this, MakeVector4(0.9F, 0.9F, 0.9F, 1.0F), 60.0F,
				  SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, RandomUnitVector() * 1.5F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(
				  1.8F + SampleRandomFloat() * SampleRandomFloat() * 0.9F,
				  4.5F, 0.2F);
				ent->SetLifeTime(8.1F + SampleRandomFloat() * 0.4F, 0.0F, 1.5F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
			// The light and the boom.
			Handle<IAudioChunk> boom;
			if (!IsMuted())
				boom = audioDevice->RegisterSound(
				  "Sounds/Weapons/Grenade/Explode1.opus");
			localEntities.emplace_back(stmp::make_unique<BlastFlashEntity>(
			  *this, audioDevice.GetPointerOrNull(), std::move(boom), pos));
			// The dark smoke that stays about 2.5 s and climbs about 7
			// blocks, as wide as the burst zone.
			for (int i = 0; i < 6; i++) {
				Vector3 off = RandomUnitVector();
				off.z = 0.0F;
				auto ent = stmp::make_unique<SmokeSpriteEntity>(
				  *this, MakeVector4(0.18F, 0.18F, 0.18F, 0.95F), 60.0F,
				  SmokeSpriteEntity::Type::Steady);
				ent->SetTrajectory(pos + off * 0.4F,
				                   MakeVector3(0.0F, 0.0F, -2.7F), 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(2.2F + SampleRandomFloat() * 0.5F, 0.12F);
				ent->SetLifeTime(2.5F + SampleRandomFloat() * 0.5F, 0.3F,
				                   0.8F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		namespace {
			// The floor a crumb of a way in runs over: the nearest solid
			// top about the height the line itself was drawn at, so the
			// crumbs sit on the ground the waypoints cross rather than on
			// the straight slope drawn between them - the waypoints are
			// the turns, and the ground between turns wanders. Nothing
			// solid near, and the line's own height stands.
			int TrailFloor(const GameMap& map, int x, int y, int z) {
				static const int kSweep[7] = {0, -1, 1, -2, 2, -3, 3};
				for (int k = 0; k < 7; k++) {
					const int b = z + kSweep[k];
					if (b >= 0 && b < map.Depth() &&
					    map.IsSolidWrapped(x, y, b) && !map.IsSolidWrapped(x, y, b - 1))
						return b;
				}
				return z;
			}
		} // namespace

		void Client::SetTrailPath(std::uint8_t front, const Vector3& color,
		                          std::vector<Vector3> points) {
			SPADES_MARK_FUNCTION();
			if (!world)
				return; // before the map: the server sends it again once it is in
			const Handle<GameMap>& map = world->GetMap();
			SPAssert(map);
			if (!map)
				return;

			// A way somewhere else than the world says nothing here:
			// dropped whole rather than drawn folded back onto the map.
			for (const Vector3& p : points) {
				if (p.x < 0.0F || p.y < 0.0F || p.z < 0.0F ||
				    p.x >= static_cast<float>(map->Width()) ||
				    p.y >= static_cast<float>(map->Height()) ||
				    p.z >= static_cast<float>(map->Depth()))
					return;
			}

			// The waypoints opened into crumbs, a block of way between
			// them: the crumb a defender reads the line by is the dot at
			// the eye's own spacing, and the way is never a gap if a
			// client's setting did not thin it. The two ends of the way
			// are crumbs whatever the length falls on: the walk's own
			// front and the core's doorstep both stand marked even on a
			// short line that rounds to no crumb between them.
			std::vector<Vector3> crumbs;
			crumbs.reserve(points.size() * 2 + 2);

			// One way's own side of the road. Ways that share a corridor -
			// fronts funnelled through one door, most of their way walked
			// over another's - would lay crumb upon crumb, the one colour
			// lost in the other along all they share. So every front
			// keeps its own side of the way it walks, held off the line's
			// centre by its own hand's breadth, always the same side, the
			// left of the walk: two fronts through one corridor stand as
			// two lines side by side, both read, and where ways cross they
			// cross with a knot of two colours, not a fight over pixels.
			// The breadth comes off the front's own number, no two fronts
			// on one side among the first dozen, and the half-block into
			// the cell keeps the crumb of a way near its centre to its
			// own column's floor (the cell it is pushed into is the cell
			// it stands on).
			const float lat =
			  (static_cast<float>((front * 7U + 3U) % 13U) - 6.0F) * 0.1F;
			// The side kept over one stretch of the way: always the left
			// of the walk, so bends bend the side with them and a side
			// stays a side however the way turns. A stretch walked
			// straight up or down has no left worth speaking of; its
			// crumbs keep the centre, one way through a stair standing
			// where the stair is.
			auto sideOf = [&](const Vector3& a, const Vector3& b, float& px, float& py) {
				px = a.y - b.y;
				py = b.x - a.x;
				const float len = std::sqrt(px * px + py * py);
				if (len < 1.0e-4F) {
					px = 0.0F;
					py = 0.0F;
					return;
				}
				const float k = lat / len;
				px *= k;
				py *= k;
			};
			auto add = [&](const Vector3& c, float px, float py) {
				const int x = static_cast<int>(c.x + px);
				const int y = static_cast<int>(c.y + py);
				const int z = TrailFloor(*map, x, y, static_cast<int>(c.z + 0.5F));
				// A half-block into the cell, a little above the floor:
				// the crumb stands on the way it marks, half in it, not
				// buried in the ground it runs over.
				crumbs.push_back(MakeVector3(c.x + px + 0.5F, c.y + py + 0.5F,
				                             static_cast<float>(z) - 0.35F));
			};
			if (!points.empty()) {
				if (points.size() > 1) {
					float px, py;
					// The two ends of the way are crumbs whatever the
					// length falls on: the walk's own front and the
					// core's doorstep both stand marked even on a short
					// line that rounds to no crumb between them.
					sideOf(points[0], points[1], px, py);
					add(points.front(), px, py);
					// Where the next crumb falls, in blocks of the way
					// walked since the last one.
					float toNext = 1.0F;
					for (size_t s = 0; s + 1 < points.size(); s++) {
						const Vector3& a = points[s];
						const Vector3& b = points[s + 1];
						const float seg = (b - a).GetLength();
						sideOf(a, b, px, py);
						float walked = 0.0F;
						while (walked + toNext <= seg) {
							walked += toNext;
							toNext = 1.0F;
							add(a + (b - a) * (walked / seg), px, py);
						}
						toNext -= seg - walked;
						// A float's own dust, after a hundred turns of the
						// way: a step that lands a hair before the last
						// crumb would draw its crumb before the walk's
						// own line, back along the way it came.
						if (toNext < 0.0F)
							toNext = 0.0F;
					}
					sideOf(points[points.size() - 2], points.back(), px, py);
					add(points.back(), px, py);
				} else {
					// A way stood on one spot: no walk, no side to keep.
					add(points.front(), 0.0F, 0.0F);
				}
			}
			if (crumbs.empty())
				trails.Forget(front);
			else
				trails.SetPath(front, color, std::move(crumbs));
		}

		void Client::KickCamera(float strength) {
			grenadeVibration = std::min(grenadeVibration + strength, 0.4F);
			grenadeVibrationSlow = std::min(grenadeVibrationSlow + strength * 5.0F, 0.4F);
		}

		void Client::GrenadeExplosion(spades::Vector3 pos) {
			// distance cull
			float dist = (pos - lastSceneDef.viewOrigin).GetLength();
			if (dist > FOG_DISTANCE + 16.0F)
				return;

			DynamicLightParam l;
			l.type = DynamicLightTypePoint;
			l.origin = pos;
			l.radius = 16.0F;
			l.color = MakeVector3(3.0F, 1.6F, 0.5F);
			l.useLensFlare = true;
			flashDlights.push_back(l);

			if (dist <= FOG_DISTANCE)
				KickCamera(2.0F / (dist + 5.0F));

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// determines free space around for SetTrajectory
			Vector3 velBias = {0, 0, 0};
			if (!map->ClipBox(pos.x, pos.y, pos.z)) {
				if (map->ClipBox(pos.x + 1.0F, pos.y, pos.z))
					velBias.x -= 1.0F;
				if (map->ClipBox(pos.x - 1.0F, pos.y, pos.z))
					velBias.x += 1.0F;
				if (map->ClipBox(pos.x, pos.y + 1.0F, pos.z))
					velBias.y -= 1.0F;
				if (map->ClipBox(pos.x, pos.y - 1.0F, pos.z))
					velBias.y += 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z + 1.0F))
					velBias.z -= 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z - 1.0F))
					velBias.z += 1.0F;
			}

			// fragments
			Handle<IImage> img = renderer->RegisterWhiteImage();

			uint32_t col = IntVectorToColor(MakeIntVector3(70, 70, 70));
			col = map->GetColorJit(col); // jit the colour
			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			int particlesNum = cg_particlesGrenadeNum;
			for (int i = 0; i < particlesNum; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				Vector3 dir = RandomUnitVector() + velBias * 0.5F;
				float radius = 0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.3F;
				ent->SetTrajectory(pos + dir * 0.2F, dir * 16.0F, 0.1F + radius * 3.0F);
				ent->SetRadius(radius);
				ent->SetLifeTime(3.5F + SampleRandomFloat() * 2.0F, 0.0F, 1.0F);
				if (dist < PARTICLE_BOUNCE_DIST)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			// rapid smoke
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 60.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomUnitVector() + velBias * 0.5F) * 2.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 2.0F, 0.2F);
				ent->SetLifeTime(1.8F + SampleRandomFloat() * 0.1F, 0.0F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// slow smoke
			color.w = 0.25F;
			for (int i = 0; i < 8; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 30.0F);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
							   SampleRandomFloat() - SampleRandomFloat(),
							   (SampleRandomFloat() - SampleRandomFloat()) * 0.2F)) * 2.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(1.5F + SampleRandomFloat() * SampleRandomFloat() * 0.8F, 0.2F);
				switch (particleLevel) {
					case 1: ent->SetLifeTime(0.8F + SampleRandomFloat() * 1.0F, 0.1F, 8.0F); break;
					case 2: ent->SetLifeTime(1.5F + SampleRandomFloat() * 2.0F, 0.1F, 8.0F); break;
					case 3:
					default: ent->SetLifeTime(2.0F + SampleRandomFloat() * 5.0F, 0.1F, 8.0F); break;
				}
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// fire smoke
			color = MakeVector4(1, 0.7F, 0.4F, 0.2F) * 5.0F;
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 120.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomUnitVector() + velBias) * 6.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 3.0F, 0.1F);
				ent->SetLifeTime(0.18F + SampleRandomFloat() * 0.03F, 0.0F, 0.1F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::GrenadeExplosionUnderwater(spades::Vector3 pos) {
			// distance cull
			float dist = (pos - lastSceneDef.viewOrigin).GetLength();
			if (dist > FOG_DISTANCE + 16.0F)
				return;

			if (dist <= FOG_DISTANCE)
				KickCamera(1.0F / (dist + 5.0F));

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			Vector3 velBias = {0, 0, -1.0F};

			// fragments
			Handle<IImage> img = renderer->RegisterWhiteImage();

			Vector4 color = MakeVector4(1, 1, 1, 0.6F);

			int particlesNum = cg_particlesGrenadeNum;
			for (int i = 0; i < particlesNum; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				Vector3 dir = RandomUnitVector() + velBias * 0.5F;
				float radius = 0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.3F;
				ent->SetTrajectory(pos + dir * 0.2F, dir * 16.0F, 0.1F + radius * 3.0F);
				ent->SetRadius(radius);
				ent->SetLifeTime(3.5F + SampleRandomFloat() * 2.0F, 0.0F, 1.0F);
				if (dist < PARTICLE_BOUNCE_DIST)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			// water1
			img = renderer->RegisterImage("Textures/WaterExpl.png");
			color = MakeVector4(0.95F, 0.95F, 0.95F, 0.6F);
			for (int i = 0; i < 7; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(0.0F, 0.0F, -SampleRandomFloat() * 7.0F)) * 2.5F, 0.3F);
				ent->SetRadius(1.2F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 0.6F);
				ent->SetLifeTime(2.0F + SampleRandomFloat() * 0.3F, 0.1F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// water2
			img = renderer->RegisterImage("Textures/Fluid.png");
			color.w = 0.9F;
			for (int i = 0; i < 16; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
													 SampleRandomFloat() - SampleRandomFloat(),
													 -SampleRandomFloat() * 7.0F)) * 3.5F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.3F, 0.5F);
				ent->SetLifeTime(2.0F + SampleRandomFloat() * 0.3F, 0.1F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// slow smoke
			color.w = 0.3F;
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 10.0F);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
							   SampleRandomFloat() - SampleRandomFloat(),
							   (SampleRandomFloat() - SampleRandomFloat()) * 0.2F)) * 2.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(1.5F + SampleRandomFloat() * SampleRandomFloat() * 0.6F, 0.2F);
				ent->SetLifeTime(2.0F + SampleRandomFloat() * 0.3F, 0.2F, 1.5F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// TODO: wave?
		}

		void Client::BulletHitWaterSurface(spades::Vector3 pos) {
			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			Vector3 velBias = {0, 0, -1.0F};

			// fragments
			Handle<IImage> img = renderer->RegisterWhiteImage();

			Vector4 color = MakeVector4(1, 1, 1, 0.6F);

			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 8.0F);
				ent->SetRadius(0.3F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2 || !cg_waterImpact)
				return;

			// water1
			img = renderer->RegisterImage("Textures/WaterExpl.png");
			color = MakeVector4(0.95F, 0.95F, 0.95F, 0.3F);
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
												SampleRandomFloat() - SampleRandomFloat(),
												-SampleRandomFloat() * 7.0F)), 0.3F, 0.6F);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 0.7F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				ent->SetLifeTime(3.0F + SampleRandomFloat() * 0.3F, 0.1F, 0.6F);
				localEntities.emplace_back(std::move(ent));
			}

			// water2
			img = renderer->RegisterImage("Textures/Fluid.png");
			color.w = 0.9F;
			for (int i = 0; i < 6; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
												SampleRandomFloat() - SampleRandomFloat(),
												-SampleRandomFloat() * 16.0F)));
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.6F, 0.6F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				ent->SetLifeTime(3.0F + SampleRandomFloat() * 0.3F, SampleRandomFloat() * 0.3F, 0.6F);
				localEntities.emplace_back(std::move(ent));
			}

			// TODO: wave?
		}

		void Client::EmitSnowflakes(spades::Vector3 pos) {
			SPADES_MARK_FUNCTION();

			if (time - lastSnowDropTime < 0.2F)
				return;

			// fragments
			Handle<IImage> img = renderer->RegisterWhiteImage();

			Vector4 color = MakeVector4(1, 1, 1, 0.9F);

			const float spawnRange = 140.0F;
			const float spawnHeight = 64.0F;
			const int particlesNum = 20;

			for (int i = 0; i < particlesNum; ++i) {
				Vector3 spawnPos;
				spawnPos.x = pos.x + (SampleRandomFloat() * 2.0F - 1.0F) * spawnRange;
				spawnPos.y = pos.y + (SampleRandomFloat() * 2.0F - 1.0F) * spawnRange;
				spawnPos.z = pos.z - spawnHeight;

				Vector3 vel = RandomVector() * 8.0F;
				vel.z = Mix(0.5F, 1.0F, SampleRandomFloat());

				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(spawnPos, vel, 0.8F, 0.1F);
				ent->SetRadius(0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.2F);
				ent->SetLifeTime(10.0F, 0.5F, 1.0F);
				ent->SetBlockHitAction(BlockHitAction::Stick);
				localEntities.emplace_back(std::move(ent));
			}

			lastSnowDropTime = time;
		}

#pragma mark - Camera Control

		enum { AutoFocusPoints = 4 };
		void Client::UpdateAutoFocus(float dt) {
			if (world && autoFocusEnabled && cg_manualFocus) {
				// Compute focal length
				float measureRange = tanf(lastSceneDef.fovY * 0.5F) * 0.2F;
				const Vector3 camPos = lastSceneDef.viewOrigin;
				const Vector3 camDir = lastSceneDef.viewAxis[2];
				const Vector3 camX = lastSceneDef.viewAxis[0].Normalize() * measureRange;
				const Vector3 camY = lastSceneDef.viewAxis[1].Normalize() * measureRange;

				float distances[AutoFocusPoints * AutoFocusPoints];
				std::size_t numValidDistances = 0;
				Vector3 camDir1 = camDir.Normalize() - camX - camY;
				const Vector3 camDX = camX * (2.0F / (AutoFocusPoints - 1));
				const Vector3 camDY = camY * (2.0F / (AutoFocusPoints - 1));
				for (int x = 0; x < AutoFocusPoints; ++x) {
					Vector3 camDir2 = camDir1;
					for (int y = 0; y < AutoFocusPoints; ++y) {
						float dist = RayCastForAutoFocus(camPos, camDir2);
						dist *= (1.0F / camDir.GetLength());

						if (std::isfinite(dist) && dist > 0.8F)
							distances[numValidDistances++] = dist;

						camDir2 += camDY;
					}

					camDir1 += camDX;
				}

				if (numValidDistances > 0) {
					// Take median
					std::sort(distances, distances + numValidDistances);

					float dist = (numValidDistances & 1)
								   ? distances[numValidDistances >> 1]
								   : (distances[numValidDistances >> 1] +
									  distances[(numValidDistances >> 1) - 1]) * 0.5F;

					targetFocalLength = dist;
				}
			}

			// Change the actual focal length slowly
			{
				float dist = 1.0F / targetFocalLength;
				float curDist = 1.0F / focalLength;
				const float maxSpeed = cg_autoFocusSpeed;

				if (dist > curDist)
					curDist = std::min(dist, curDist + maxSpeed * dt);
				else
					curDist = std::max(dist, curDist - maxSpeed * dt);

				focalLength = 1.0F / curDist;
			}
		}
		float Client::RayCastForAutoFocus(const Vector3& origin, const Vector3& direction) {
			SPAssert(world);

			World::WeaponRayCastResult res = world->WeaponRayCast(origin, direction, {});
			if (res.hit)
				return Vector3::Dot(res.hitPos - origin, lastSceneDef.viewAxis[2]);

			return NAN;
		}
	} // namespace client
} // namespace spades