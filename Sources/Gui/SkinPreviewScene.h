/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.
 Distributed under the GNU General Public License, version 3 or later.
 See <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <vector>

#include <Core/Math.h>
#include <Core/VoxelModel.h>

namespace spades {
	namespace gui {
		/** One actual draw, not every model registered by a skin's constructor. */
		struct SkinPreviewPart {
			Handle<VoxelModel> model;
			std::string path;
			Matrix4 matrix = Matrix4::Identity();
			Vector3 customColor = MakeVector3(0, 0, 0);
			float opacity = 1;
		};

		/** Captured long sprites (e.g. a lightsaber's blade), in scene
		 * coordinates. */
		struct SkinPreviewBeam {
			Vector3 start = MakeVector3(0, 0, 0), end = MakeVector3(0, 0, 0);
			float radius = 0;
			Vector3 color = MakeVector3(0, 0, 0);
		};

		struct SkinPreviewScene {
			std::vector<SkinPreviewPart> parts;
			std::vector<SkinPreviewBeam> beams;
			bool scripted = false;
			// Non-empty when a script could not be captured and a fallback was used.
			std::string diagnostic;
		};
	} // namespace gui
} // namespace spades
