/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

// The puffs, their drift and the hiss are Serius_Sim's smoke grenade, made
// into one emitter the server drives.

#include "Smoke.h"

#include <algorithm>
#include <vector>
#include <cmath>

#include "Client.h"
#include "CTFGameMode.h"
#include "IAudioDevice.h"
#include "NetProtocol.h"
#include "StoreState.h"
#include "StoreParser.h"
#include <Training/Protocol.h>
#include "World.h"
#include "SentryEntity.h"
#include "SmokeSpriteEntity.h"
#include <Core/Debug.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_smokeVolume, "1");

namespace spades {
	namespace client {
		namespace {
			// The hiss lasts about 9 s; while a smoke keeps coming it is played
			// again a little before it ends, so the two overlap rather than cut.
			constexpr float kSoundPeriod = 8.5F;
			// A puff lives this long at most, so a long smoke keeps a steady
			// number of them rather than piling up; it is topped up as they go.
			constexpr float kMaxPuffLife = 8.0F;
			constexpr float kPuffFadeIn = 0.25F;
		} // namespace

		SmokeEntity::SmokeEntity(Client& client, IAudioDevice* audio, Handle<IAudioChunk> sound,
		                         const SmokeParams& params, std::shared_ptr<bool> stop)
		    : client(client), audio(audio), sound(std::move(sound)), params(params),
		      stop(std::move(stop)) {}

		void SmokeEntity::Puff(float untilGone) {
			const float r = params.radius;
			// How much of a grenade's cloud (radius 5) this one is: puffs as
			// big and as many in proportion, within what reads as smoke.
			const float k = std::max(0.2F, std::min(r / 5.0F, 4.0F));

			Vector3 off = RandomUnitVector();
			off.z = std::fabs(off.z) * 0.4F; // z is down: near the ground, not under it
			Vector3 vel;
			float size, growth, damp;
			if (params.style == SmokeParams::Column) {
				// A thin plume rising
				off.x *= r * 0.15F;
				off.y *= r * 0.15F;
				vel = MakeVector3((SampleRandomFloat() - 0.5F) * 0.6F,
				                  (SampleRandomFloat() - 0.5F) * 0.6F,
				                  -7.0F - SampleRandomFloat() * 5.0F);
				size = (0.9F + SampleRandomFloat() * 0.5F) * std::max(0.6F, k);
				growth = 0.22F;
				damp = 0.9F;
			} else if (params.style == SmokeParams::Ground) {
				// A heavy vapour lying in the floor: what lift it has it
				// loses, the drift along the ground is a crawl, and its
				// life is short enough that the bed never stops looking
				// freshly poured - and it stays low, so a trail of it
				// reads as fog on the way in, never a plume over it. Its
				// weight is the look: a puff pours big, wide and slow,
				// barely stirs, and swells at its own leisure rather
				// than blooming - what motion it has is its own inertia, a
				// settling that only ever goes down.
				// The bed is a valley, not a lid: the pour at the centre
				// sits on the floor, thick and out in the open, while the
				// pour at the rim is born sinking into the ground - z is
				// down, so the further a puff lays from the bed's middle,
				// the deeper it starts under the floor's surface, and the
				// earth's own occlusion thins it to a fade at the sides.
				off.x *= r * 0.6F;
				off.y *= r * 0.6F;
				off.z = 0.36F + std::sqrt(off.x * off.x + off.y * off.y) * 0.72F;
				vel = MakeVector3((SampleRandomFloat() - 0.5F) * 0.15F,
				                  (SampleRandomFloat() - 0.5F) * 0.15F,
				                  (SampleRandomFloat() - 0.5F) * 0.06F - 0.035F);
				size = (1.0F + SampleRandomFloat() * 0.5F) * std::max(0.6F, std::min(k, 1.5F));
				growth = 0.035F;
				damp = 0.992F;
			} else if (params.style == SmokeParams::Spray) {
				// The tell a breath charges: drops of the colour itself,
				// thrown up off the origin to fall again - the splash of
				// blood read in the shade the server sends, a drop half
				// a block across reads, gone almost as soon as it lands.
				off = RandomUnitVector();
				off *= 0.3F;
				vel = MakeVector3((SampleRandomFloat() - 0.5F) * 3.0F,
				                  (SampleRandomFloat() - 0.5F) * 3.0F,
				                  -2.5F - SampleRandomFloat() * 3.5F);
				auto drop = stmp::make_unique<ParticleSpriteEntity>(
				  client, client.GetRenderer().RegisterWhiteImage(),
				  params.color);
				drop->SetTrajectory(params.origin + off, vel, 1.0F, 1.0F);
				drop->SetRadius(0.32F + SampleRandomFloat() * 0.20F);
				const float dropLife = 0.55F + SampleRandomFloat() * 0.30F;
				drop->SetLifeTime(dropLife, 0.02F, dropLife * 0.5F);
				drop->SetBlockHitAction(BlockHitAction::BounceWeak);
				client.AddLocalEntity(std::move(drop));
				return;
			} else {
				// A wide cloud, spreading low
				off.x *= r;
				off.y *= r;
				vel = MakeVector3((SampleRandomFloat() - 0.5F) * 8.0F * k,
				                  (SampleRandomFloat() - 0.5F) * 8.0F * k,
				                  -4.35F - SampleRandomFloat() * 4.3F);
				size = (3.2F + SampleRandomFloat() * 1.5F) * std::max(0.5F, std::min(k, 1.5F));
				growth = 0.8F;
				damp = 0.94F;
			}

			auto ent = stmp::make_unique<SmokeSpriteEntity>(client, params.color, 60.0F,
			                                               SmokeSpriteEntity::Type::Steady);
			ent->SetTrajectory(params.origin + off, vel, damp, 0.0F);
			ent->SetRotation(SampleRandomFloat() * kTwoPi);
			ent->SetRadius(size, growth, 0.3F);
		// A ground bed is gone almost as soon as it is poured, so a
		// trail of it swaps its colour fast; the others can linger.
			const float life = std::max(0.5F, std::min(untilGone, kMaxPuffLife)) +
			                   SampleRandomFloat() *
			                       (params.style == SmokeParams::Ground ? 0.15F : 0.5F);
			ent->SetLifeTime(life, kPuffFadeIn, std::min(params.fade, life * 0.5F) + 0.01F);
			ent->SetBlockHitAction(BlockHitAction::Ignore);
			client.AddLocalEntity(std::move(ent));
		}

		bool SmokeEntity::Update(float dt) {
			age += dt;
			if (*stop || age >= params.duration)
				return false; // the puffs out there fade by themselves

			// The hiss, again while it keeps coming
			if (params.sound && audio && sound) {
				soundTimer -= dt;
				if (soundTimer <= 0.0F) {
					soundTimer += kSoundPeriod;
					AudioParam p;
					p.volume = static_cast<float>(cg_smokeVolume);
					audio->Play(sound.GetPointerOrNull(), params.origin, p);
				}
			}

			// A grenade's cloud (radius 5) puffs 40 times a second, a column 14;
			// a bigger or smaller one in proportion to its area. A ground bed
			// keeps its own steady pace: freshness is the look, not volume.
			const float k = std::max(0.2F, std::min(params.radius / 5.0F, 4.0F));
			float interval;
			if (params.style == SmokeParams::Column)
				interval = 0.07F / std::max(1.0F, k);
			else if (params.style == SmokeParams::Ground)
				interval = 0.05F;
			else if (params.style == SmokeParams::Spray)
				interval = 0.03F;
			else
				interval = std::max(0.01F, 0.025F / (k * k));
			puffTimer -= dt;
			int budget = 64; // after a long frame, catch up only so far
			while (puffTimer <= 0.0F && budget-- > 0) {
				puffTimer += interval;
				Puff(params.duration + params.fade - age);
			}
			if (puffTimer < 0.0F)
				puffTimer = 0.0F;
			return true;
		}

		std::shared_ptr<bool> SmokeRegistry::Start(int id) {
			auto flag = std::make_shared<bool>(false);
			if (id == SmokeParams::kAnonymous)
				return flag;
			Clear(id);
			smokes[id] = flag;
			return flag;
		}

		void SmokeRegistry::Clear(int id) {
			if (id == SmokeParams::kAnonymous) {
				for (auto& s : smokes)
					*s.second = true;
				smokes.clear();
				return;
			}
			auto it = smokes.find(id);
			if (it != smokes.end()) {
				*it->second = true;
				smokes.erase(it);
			}
		}

		void ApplySmokePacket(NetPacketReader& r, Client& client) {
			if (r.GetNumRemainingBytes() < 1) {
				SPLog("Dropped a truncated Smoke packet");
				return;
			}
			const int sub = r.ReadByte();
			switch (sub) {
				case SmokeRegistry::kSubSmoke: {
					// id style xyz radius rgb opacity duration fade flags
					if (r.GetNumRemainingBytes() < 1 + 1 + 12 + 1 + 4 + 2 + 1 + 1) {
						SPLog("Dropped a Smoke of the wrong size");
						return;
					}
					SmokeParams p;
					p.id = r.ReadByte();
					p.style = r.ReadByte();
					if (p.style != SmokeParams::Column && p.style != SmokeParams::Ground &&
					    p.style != SmokeParams::Blast && p.style != SmokeParams::Spray)
						p.style = SmokeParams::Cloud; // an unknown style: a cloud
					const float x = r.ReadFloat(), y = r.ReadFloat(), z = r.ReadFloat();
					p.origin = MakeVector3(x, y, z);
					p.radius = std::max(1.0F, static_cast<float>(r.ReadByte()));
					const float cr = r.ReadByte() / 255.0F, cg = r.ReadByte() / 255.0F,
					            cb = r.ReadByte() / 255.0F, ca = r.ReadByte() / 255.0F;
					p.color = MakeVector4(cr, cg, cb, ca);
					p.duration = static_cast<float>(r.ReadShort()) / 10.0F;
					p.fade = static_cast<float>(r.ReadByte()) / 10.0F;
					p.sound = (r.ReadByte() & 1) != 0;
					if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
						SPLog("Dropped a Smoke somewhere that is nowhere");
						return;
					}
					client.StartSmoke(p);
				} break;
				case SmokeRegistry::kSubClear: {
					if (r.GetNumRemainingBytes() < 1) {
						SPLog("Dropped a Clear Smoke of the wrong size");
						return;
					}
					client.ClearSmoke(r.ReadByte());
				} break;
				case SmokeRegistry::kSubPath: {
					// id r g b count then the way as its turns: the first
					// point whole, each later one a byte of delta apiece.
					if (r.GetNumRemainingBytes() < 1 + 3 + 1) {
						SPLog("Dropped a Path of the wrong size");
						return;
					}
					const int front = r.ReadByte();
					const float cr = r.ReadByte() / 255.0F, cg = r.ReadByte() / 255.0F,
					            cb = r.ReadByte() / 255.0F;
					const int count = r.ReadByte();
					if (count == 0) {
						client.ClearTrailPath(static_cast<std::uint8_t>(front));
						break;
					}
					if (r.GetNumRemainingBytes() < 5 + 3 * (count - 1)) {
						SPLog("Dropped a Path truncated in the middle of its way");
						return;
					}
					std::vector<Vector3> pts;
					pts.reserve(count);
					int x = static_cast<int16_t>(r.ReadShort());
					int y = static_cast<int16_t>(r.ReadShort());
					int z = r.ReadByte();
					pts.push_back(MakeVector3(static_cast<float>(x),
					                          static_cast<float>(y), static_cast<float>(z)));
					for (int k = 1; k < count; ++k) {
						x += static_cast<int8_t>(r.ReadByte());
						y += static_cast<int8_t>(r.ReadByte());
						z += static_cast<int8_t>(r.ReadByte());
						pts.push_back(MakeVector3(static_cast<float>(x),
						                          static_cast<float>(y),
						                          static_cast<float>(z)));
					}
					client.SetTrailPath(static_cast<std::uint8_t>(front),
					                     MakeVector3(cr, cg, cb), std::move(pts));
				} break;
				case SmokeRegistry::kSubTurret: {
					// count then each gun as 9 bytes: id level, x y as
					// i16, z as u8, locked, the quantized aim. The
					// whole set of the server's guns, replacing
					// everything sent before: count 0 is a whole truth
					// too - none are standing.
					if (r.GetNumRemainingBytes() < 1) {
						SPLog("Dropped a Turrets of the wrong size");
						return;
					}
					const int count = r.ReadByte();
					if (r.GetNumRemainingBytes() < 9 * count) {
						SPLog("Dropped a Turrets truncated in the middle of its guns");
						return;
					}
					std::vector<ServerTurret> guns;
					guns.reserve(count);
					for (int k = 0; k < count; ++k) {
						ServerTurret t;
						t.id = static_cast<std::uint8_t>(r.ReadByte());
						t.level = static_cast<std::uint8_t>(r.ReadByte());
						t.x = static_cast<int16_t>(r.ReadShort());
						t.y = static_cast<int16_t>(r.ReadShort());
						t.z = r.ReadByte();
						t.locked = (r.ReadByte() & 1) != 0;
						t.yaw_pack = static_cast<std::uint8_t>(r.ReadByte());
						t.pitch_pack = static_cast<std::uint8_t>(r.ReadByte());
						guns.push_back(t);
					}
					client.ApplyTurrets(std::move(guns));
				} break;
				case SmokeRegistry::kSubTurretShot: {
					// one id, the gun that fired, at the tick the shot
					// landed: the tracer, the flash, the report
					if (r.GetNumRemainingBytes() < 1) {
						SPLog("Dropped a Turret Shot of the wrong size");
						return;
					}
					client.ServerTurretFired(static_cast<std::uint8_t>(r.ReadByte()));
				} break;
				case SmokeRegistry::kSubCore: {
					// x y as i16 and the plane of the column's top as u8:
					// the cell the core stands on, whose flag this moves -
					// the only packet that can say a move, a vanilla one
					// always telling of a capture or a drop nobody made.
					if (r.GetNumRemainingBytes() < 2 + 2 + 1) {
						SPLog("Dropped a Core of the wrong size");
						return;
					}
					const auto x = static_cast<std::int16_t>(r.ReadShort());
					const auto y = static_cast<std::int16_t>(r.ReadShort());
					const auto z = static_cast<float>(r.ReadByte());
					World* world = client.GetWorld();
					if (!world)
						break;
					stmp::optional<IGameMode&> mode = world->GetMode();
					if (mode && mode->ModeType() == IGameMode::m_CTF) {
						auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
						// The flag hops to the top of the column, and the base
						// one plane beneath it: the tent the client draws for
						// the base stands at the core's foot, so a mid game
						// move takes the dress along with the flag.
						ctf.GetTeam(0).flagPos = MakeVector3(static_cast<float>(x) + 0.5F,
						                                     static_cast<float>(y) + 0.5F, z);
						ctf.GetTeam(0).basePos = MakeVector3(static_cast<float>(x) + 0.5F,
						                                      static_cast<float>(y) + 0.5F,
						                                      z + 1.0F);
					}
				} break;
				case SmokeRegistry::kSubStore: {
				// The store's own word, which only a tower defense world
				// speaks. One player's purse, the wave and core, and every
				// upgrade they own.
				//
				// The packet id and this sub packet byte have both been
				// eaten already - the dispatcher took the id, and the
				// switch above took the sub - so what is left is the
				// store's body, and that is what is read. Asking for the
				// whole packet here instead would have the reader looking
				// at the purse's first two bytes and calling them a packet
				// id, which is how a store can arrive and be dropped.
				const std::size_t from = r.GetPosition();
				const uint8_t* body =
				  reinterpret_cast<const uint8_t*>(r.GetData().data()) + from;
				const std::size_t body_len = r.GetNumRemainingBytes();
				StoreState state;
				if (!ParseStoreBody(body, body_len, state)) {
					SPLog("Dropped a Store the store could not read");
					return;
				}
				client.ApplyStoreState(state);
				// Said on every word so a buy that never shows on the
				// hand can be told from a store that was never heard.
				SPLog("Store word heard: %d owned, %d Zedcreds",
				      static_cast<int>(state.ownedCount),
				      static_cast<int>(state.zedcreds));
			} break;
				default:
					SPLog("Ignoring an unknown Smoke sub packet %d", sub);
					break;
			}
		}
	} // namespace client
} // namespace spades
