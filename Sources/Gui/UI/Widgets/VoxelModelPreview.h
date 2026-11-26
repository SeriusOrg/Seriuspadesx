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

#pragma once

#include <cstdint>
#include <vector>

#include <Core/Math.h>

namespace spades {
	class VoxelModel;

	namespace gui {
		struct SkinPreviewPart;
		struct SkinPreviewScene;
		namespace ui {
			/**
			 * An exact, lit voxel surface for a rotating thumbnail. Coplanar faces
			 * of the same color are merged, never downsampled: doing so exposed
			 * HollowFill's pink interior voxels and erased thin weapon parts.
			 *
			 * Rasterize uses its own depth buffer and antialiasing. The UI uploads
			 * the result as one ordinary image, on all backends and the VR surface,
			 * without touching the world scene, camera, fog or shared model cache.
			 */
			class VoxelModelPreview {
				struct Face {
					Vector3 origin, axisU, axisV;
					std::uint32_t color = 0;
					Vector3 normal;
					float opacity = 1;
				};
				struct Beam {
					Vector3 start, end, color;
					float radius = 0;
				};
				std::vector<Face> faces;
				std::vector<Beam> beams;
				Vector3 halfSize{0, 0, 0};

				void AppendPart(const SkinPreviewPart& part);
				void CenterScene();

			public:
				struct Quad {
					Vector2 origin, axisU, axisV;
					Vector4 color;
					float depth = 0, depthU = 0, depthV = 0;
				};

				explicit VoxelModelPreview(const VoxelModel& model);
				explicit VoxelModelPreview(const SkinPreviewScene& scene);
				bool IsEmpty() const { return faces.empty() && beams.empty(); }
				std::size_t GetFaceCount() const { return faces.size(); }
				std::vector<Quad> Project(const AABB2& bounds, float yaw) const;
				/** Straight-alpha RGBA pixels, top to bottom. Max 512 x 512. */
				std::vector<std::uint32_t> Rasterize(int width, int height,
				                                  float yaw) const;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
