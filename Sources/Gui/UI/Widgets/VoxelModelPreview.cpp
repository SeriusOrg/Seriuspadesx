/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades. If not, see <http://www.gnu.org/licenses/>.
 */

#include "VoxelModelPreview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <Core/Exception.h>
#include <Core/VoxelModel.h>
#include <Gui/SkinPreviewScene.h>

namespace spades {
	namespace gui {
		namespace ui {
			namespace {
				const Vector3 kNormals[] = {{-1, 0, 0}, {1, 0, 0},  {0, -1, 0},
				                            {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
				// A pathological checkerboard must not stall the game or allocate an
				// unbounded mesh. Real weapons are well below this, with no
				// loss of detail.
				constexpr std::size_t kMaxFaces = 100000;
			} // namespace

			VoxelModelPreview::VoxelModelPreview(const VoxelModel& model) {
				SkinPreviewPart part;
				part.model = Handle<VoxelModel>(const_cast<VoxelModel&>(model));
				AppendPart(part);
				CenterScene();
			}

			VoxelModelPreview::VoxelModelPreview(const SkinPreviewScene& scene) {
				if (scene.parts.size() > 128 || scene.beams.size() > 64)
					SPRaise("Too many preview draws");
				for (const auto& part : scene.parts)
					AppendPart(part);
				for (const auto& beam : scene.beams) {
					const auto delta = beam.end - beam.start;
					const float length = delta.GetLength();
					if (!std::isfinite(length) || length < 0.00001F || length > 10000 ||
					    !std::isfinite(beam.radius) || beam.radius <= 0 || beam.radius > 100 ||
					    !std::isfinite(beam.color.x) || !std::isfinite(beam.color.y) ||
					    !std::isfinite(beam.color.z))
						SPRaise("Invalid preview beam");
					beams.push_back({beam.start, beam.end, beam.color, beam.radius});
				}
				CenterScene();
			}

			void VoxelModelPreview::AppendPart(const SkinPreviewPart& part) {
				if (!part.model)
					SPRaise("Missing preview component");
				for (const float f : part.matrix.m)
					if (!std::isfinite(f) || std::fabs(f) > 100000)
						SPRaise("Invalid preview transform");
				if (!std::isfinite(part.opacity) || part.opacity < 0 || part.opacity > 1)
					SPRaise("Invalid preview opacity");
				if (part.opacity == 0)
					return;
				const VoxelModel& model = *part.model;
				const auto a = part.matrix.GetAxis(0), b = part.matrix.GetAxis(1),
				           c = part.matrix.GetAxis(2);
				const float det = Vector3::Dot(a, Vector3::Cross(b, c));
				if (!std::isfinite(det) || det == 0)
					SPRaise("Singular preview transform");
				std::uint32_t customColor = 0;
				const float channels[] = {part.customColor.x, part.customColor.y,
				                          part.customColor.z};
				for (int i = 0; i < 3; ++i) {
					if (!std::isfinite(channels[i]))
						SPRaise("Invalid preview color");
					customColor |= std::uint32_t(Clamp(channels[i], 0.0F, 1.0F) * 255 + 0.5F)
					               << (8 * i);
				}
				std::array<int, 3> lo{model.GetWidth(), model.GetHeight(),
				                    model.GetDepth()};
				std::array<int, 3> hi{0, 0, 0};
				for (int x = 0; x < model.GetWidth(); ++x)
					for (int y = 0; y < model.GetHeight(); ++y) {
						const auto bits = model.GetSolidBitsAtUnchecked(x, y);
						for (int z = 0; z < model.GetDepth(); ++z) {
							if (!((bits >> z) & 1))
								continue;
							const int p[] = {x, y, z};
							for (int axis = 0; axis < 3; ++axis) {
								lo[axis] = std::min(lo[axis], p[axis]);
								hi[axis] = std::max(hi[axis], p[axis] + 1);
							}
						}
					}
				if (hi[0] == 0)
					return;

				// Greedy rectangles on each surface plane preserve every silhouette,
				// gap and surface color, including on high-resolution custom models.
				for (int dir = 0; dir < 6; ++dir) {
					const int axis = dir / 2, u = (axis + 1) % 3, v = (axis + 2) % 3;
					const int w = hi[u] - lo[u], h = hi[v] - lo[v];
					std::vector<std::uint32_t> mask(w * h);
					for (int slice = lo[axis]; slice < hi[axis]; ++slice) {
						for (int y = 0; y < h; ++y)
							for (int x = 0; x < w; ++x) {
								int p[3];
								p[axis] = slice;
								p[u] = x + lo[u];
								p[v] = y + lo[v];
								auto& color = mask[x + y * w];
								color = 0;
								if (!model.IsSolid(p[0], p[1], p[2]))
									continue;
								const auto rgb = model.GetColorUnchecked(p[0], p[1], p[2]);
								p[axis] += (dir & 1) ? 1 : -1;
								if (!model.IsSolid(p[0], p[1], p[2]))
									color = (rgb & 0xFFFFFF) | 0x1000000;
							}
						for (int y = 0; y < h; ++y)
							for (int x = 0; x < w;) {
								const auto color = mask[x + y * w];
								if (color == 0) {
									++x;
									continue;
								}
								int rw = 1, rh = 1;
								while (x + rw < w && mask[x + rw + y * w] == color)
									++rw;
								for (; y + rh < h; ++rh) {
									int n = 0;
									while (n < rw && mask[x + n + (y + rh) * w] == color)
										++n;
									if (n != rw)
										break;
								}
								for (int dy = 0; dy < rh; ++dy)
									std::fill_n(mask.begin() + x + (y + dy) * w, rw, 0);
								float p[3], du[3]{}, dv[3]{};
								p[axis] = float(slice + (dir & 1));
								p[u] = float(lo[u] + x);
								p[v] = float(lo[v] + y);
								du[u] = float(rw);
								dv[v] = float(rh);
								if (faces.size() == kMaxFaces)
									SPRaise("Preview surface is too complex");
								const auto normal = kNormals[dir];
								// Inverse-transpose, including nonuniform/mirrored scales.
								const auto transformedNormal = ((Vector3::Cross(b, c) * normal.x +
								                                 Vector3::Cross(c, a) * normal.y +
								                                 Vector3::Cross(a, b) * normal.z) /
								                                det)
								                                 .Normalize();
								const auto rgb = color & 0xFFFFFF;
								faces.push_back(
								  {(part.matrix *
								    (MakeVector3(p[0], p[1], p[2]) + model.GetOrigin()))
								     .GetXYZ(),
								   a * du[0] + b * du[1] + c * du[2],
								   a * dv[0] + b * dv[1] + c * dv[2], rgb == 0 ? customColor : rgb,
								   transformedNormal, part.opacity});
								x += rw;
							}
					}
				}
			}

			void VoxelModelPreview::CenterScene() {
				if (IsEmpty())
					return;
				Vector3 lo = faces.empty() ? beams.front().start
				                             : faces.front().origin;
				Vector3 hi = lo;
				auto include = [&](Vector3 p) {
					const float xyz[] = {p.x, p.y, p.z};
					for (const auto value : xyz)
						if (!std::isfinite(value) || std::fabs(value) > 1000000)
							SPRaise("Invalid preview bounds");
					lo = MakeVector3(std::min(lo.x, p.x), std::min(lo.y, p.y),
					                  std::min(lo.z, p.z));
					hi = MakeVector3(std::max(hi.x, p.x), std::max(hi.y, p.y),
					                  std::max(hi.z, p.z));
				};
				for (const auto& face : faces)
					for (const auto p :
					     {face.origin, face.origin + face.axisU, face.origin + face.axisV,
					      face.origin + face.axisU + face.axisV})
						include(p);
				for (const auto& beam : beams) {
					const auto radius = MakeVector3(beam.radius, beam.radius, beam.radius);
					include(beam.start - radius);
					include(beam.start + radius);
					include(beam.end - radius);
					include(beam.end + radius);
				}
				halfSize = (hi - lo) * 0.5F;
				const auto center = (hi + lo) * 0.5F;
				for (auto& face : faces)
					face.origin -= center;
				for (auto& beam : beams) {
					beam.start -= center;
					beam.end -= center;
				}
			}

			std::vector<VoxelModelPreview::Quad> VoxelModelPreview::Project(
			    const AABB2& bounds, float yaw) const {
				std::vector<Quad> result;
				if (faces.empty() || bounds.GetWidth() <= 0 || bounds.GetHeight() <= 0 ||
				    !std::isfinite(yaw))
					return result;
				const float s = std::sin(yaw), c = std::cos(yaw);
				constexpr float tilt = 0.16F;
				const float st = std::sin(tilt), ct = std::cos(tilt);
				const Vector3 right = MakeVector3(c, s, 0);
				const Vector3 down = MakeVector3(s * st, -c * st, ct);
				const Vector3 toward = MakeVector3(s * ct, -c * ct, -st);
				const float radius = std::hypot(halfSize.x, halfSize.y);
				const float height = halfSize.z * ct + radius * st;
				// Stable full-turn framing, using occupied bounds rather than the
				// model pivot/padding. A shallow studio view gives rifles more room.
				const float scale = 0.94F * std::min(bounds.GetWidth() / (2 * radius),
				                                     bounds.GetHeight() / (2 * height));
				const Vector2 center = (bounds.min + bounds.max) * 0.5F;
				auto project = [&](Vector3 p) {
					return MakeVector2(Vector3::Dot(p, right), Vector3::Dot(p, down)) * scale;
				};
				result.reserve(faces.size() / 2);
				for (const Face& face : faces) {
					const auto normal = face.normal;
					const float facing = Vector3::Dot(normal, toward);
					if (facing <= 0.0001F)
						continue;
					const float light =
					  std::max(0.0F, -0.4F * Vector3::Dot(normal, right) -
					                   0.6F * Vector3::Dot(normal, down) + 0.69F * facing);
					const auto channel = [&](int shift) {
						// UI images already contain display/sRGB bytes. A per-channel
						// gamma lift here made dark metals pale and desaturated paint.
						// Neutral scalar lighting keeps the KV6 palette's hue/contrast.
						// Exactly-black voxels already carry this draw's custom color.
						const float value = float((face.color >> shift) & 255) / 255.0F;
						return value * (0.76F + 0.24F * light);
					};
					result.push_back(
					  {center + project(face.origin), project(face.axisU), project(face.axisV),
					   MakeVector4(channel(0), channel(8), channel(16), face.opacity),
					   Vector3::Dot(face.origin, toward), Vector3::Dot(face.axisU, toward),
					   Vector3::Dot(face.axisV, toward)});
				}
				return result;
			}

			std::vector<std::uint32_t> VoxelModelPreview::Rasterize(
			    int width, int height, float yaw) const {
				if (width < 1 || height < 1 || width > 512 || height > 512)
					SPRaise("Invalid preview image size");
				// Four coverage/depth samples per output pixel, including on SW.
				const int w = width * 2, h = height * 2;
				std::vector<std::uint32_t> pixels(w * h, 0);
				std::vector<float> depth(w * h, -std::numeric_limits<float>::infinity());
				auto quads = Project(AABB2(0, 0, float(w), float(h)), yaw);
				const auto transparent = std::partition(
				  quads.begin(), quads.end(), [](const Quad& q) { return q.color.w == 1; });
				std::sort(transparent, quads.end(), [](const Quad& a, const Quad& b) {
					return a.depth + (a.depthU + a.depthV) * 0.5F <
					       b.depth + (b.depthU + b.depthV) * 0.5F;
				});
				for (const Quad& q : quads) {
					const float det = q.axisU.x * q.axisV.y - q.axisU.y * q.axisV.x;
					if (std::fabs(det) < 0.00001F)
						continue;
					const Vector2 corners[] = {q.origin, q.origin + q.axisU,
					                           q.origin + q.axisV,
					                           q.origin + q.axisU + q.axisV};
					Vector2 lo = corners[0], hi = corners[0];
					for (const auto& p : corners) {
						lo.x = std::min(lo.x, p.x);
						lo.y = std::min(lo.y, p.y);
						hi.x = std::max(hi.x, p.x);
						hi.y = std::max(hi.y, p.y);
					}
					const int x0 = std::max(0, int(std::floor(lo.x)));
					const int y0 = std::max(0, int(std::floor(lo.y)));
					const int x1 = std::min(w, int(std::ceil(hi.x)));
					const int y1 = std::min(h, int(std::ceil(hi.y)));
					const auto rgba = 0xFF000000U | std::uint32_t(q.color.x * 255 + 0.5F) |
					                  (std::uint32_t(q.color.y * 255 + 0.5F) << 8) |
					                  (std::uint32_t(q.color.z * 255 + 0.5F) << 16);
					for (int y = y0; y < y1; ++y)
						for (int x = x0; x < x1; ++x) {
							const auto p = MakeVector2(x + 0.5F, y + 0.5F) - q.origin;
							const float u = (p.x * q.axisV.y - p.y * q.axisV.x) / det;
							const float v = (q.axisU.x * p.y - q.axisU.y * p.x) / det;
							if (u < 0 || v < 0 || u > 1 || v > 1)
								continue;
							const float z = q.depth + u * q.depthU + v * q.depthV;
							const int i = x + y * w;
							if (z > depth[i]) {
								if (q.color.w == 1) {
									depth[i] = z;
									pixels[i] = rgba;
								} else {
									const float alpha = q.color.w;
									const float oldAlpha = float(pixels[i] >> 24) / 255;
									const float outAlpha = alpha + oldAlpha * (1 - alpha);
									if (outAlpha <= 0)
										continue;
									auto blended = std::uint32_t(outAlpha * 255 + 0.5F) << 24;
									for (int shift : {0, 8, 16}) {
										const float value = (float((rgba >> shift) & 255) * alpha +
										                     float((pixels[i] >> shift) & 255) *
										                       oldAlpha * (1 - alpha)) /
										                    outAlpha;
										blended |= std::uint32_t(value + 0.5F) << shift;
									}
									pixels[i] = blended;
								}
							}
						}
				}
				if (!beams.empty() && std::isfinite(yaw)) {
					const float sn = std::sin(yaw), cs = std::cos(yaw);
					const float st = std::sin(0.16F), ct = std::cos(0.16F);
					const Vector3 right = MakeVector3(cs, sn, 0);
					const Vector3 down = MakeVector3(sn * st, -cs * st, ct);
					const Vector3 toward = MakeVector3(sn * ct, -cs * ct, -st);
					const float radius = std::hypot(halfSize.x, halfSize.y);
					const float scale =
					  0.94F * std::min(float(w) / (2 * radius),
					                   float(h) / (2 * (halfSize.z * ct + radius * st)));
					auto project = [&](Vector3 p) {
						return MakeVector2(float(w) * 0.5F + Vector3::Dot(p, right) * scale,
						                   float(h) * 0.5F + Vector3::Dot(p, down) * scale);
					};
					for (const auto& beam : beams) {
						const auto a = project(beam.start), b = project(beam.end), ab = b - a;
						const float length2 = Vector2::Dot(ab, ab);
						const float r = beam.radius * scale;
						const int x0 = std::max(0, int(std::floor(std::min(a.x, b.x) - r)));
						const int y0 = std::max(0, int(std::floor(std::min(a.y, b.y) - r)));
						const int x1 = std::min(w, int(std::ceil(std::max(a.x, b.x) + r)));
						const int y1 = std::min(h, int(std::ceil(std::max(a.y, b.y) + r)));
						for (int y = y0; y < y1; ++y)
							for (int x = x0; x < x1; ++x) {
								const auto p = MakeVector2(float(x) + 0.5F, float(y) + 0.5F);
								const float t =
								  length2 > 0 ? Clamp(Vector2::Dot(p - a, ab) / length2, 0.0F, 1.0F)
								              : 0;
								const auto distance = p - (a + ab * t);
								const float falloff =
								  1 - Vector2::Dot(distance, distance) / (r * r);
								if (falloff <= 0)
									continue;
								const auto center = Mix(beam.start, beam.end, t);
								const int i = x + y * w;
								if (Vector3::Dot(center, toward) + beam.radius <= depth[i])
									continue;
								const float weight = falloff * falloff;
								const float oldAlpha = float(pixels[i] >> 24) / 255;
								const float coverage = Clamp(
								  std::max({beam.color.x, beam.color.y, beam.color.z}) * weight,
								  0.0F, 1.0F);
								const float alpha = coverage + oldAlpha * (1 - coverage);
								if (alpha <= 0)
									continue;
								auto rgba = std::uint32_t(alpha * 255 + 0.5F) << 24;
								const float color[] = {beam.color.x, beam.color.y, beam.color.z};
								for (int channel = 0; channel < 3; ++channel) {
									const float value =
									  (float((pixels[i] >> (8 * channel)) & 255) / 255 * oldAlpha +
									   std::max(0.0F, color[channel]) * weight) /
									  alpha;
									rgba |= std::uint32_t(Clamp(value, 0.0F, 1.0F) * 255 + 0.5F)
									        << (8 * channel);
								}
								pixels[i] = rgba;
							}
					}
				}

				std::vector<std::uint32_t> result(width * height, 0);
				for (int y = 0; y < height; ++y)
					for (int x = 0; x < width; ++x) {
						unsigned count = 0, r = 0, g = 0, b = 0;
						for (int dy = 0; dy < 2; ++dy)
							for (int dx = 0; dx < 2; ++dx) {
								const auto p = pixels[x * 2 + dx + (y * 2 + dy) * w];
								const unsigned alpha = p >> 24;
								count += alpha;
								r += (p & 255) * alpha;
								g += ((p >> 8) & 255) * alpha;
								b += ((p >> 16) & 255) * alpha;
							}
						if (count)
							result[x + y * width] = (count / 4 << 24) | (r / count) |
							                        (g / count << 8) | (b / count << 16);
					}
				return result;
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
