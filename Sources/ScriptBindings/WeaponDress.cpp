/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "ScriptManager.h"

#include "WeaponDress.h"

#include <Core/FileManager.h>
#include <Core/IStream.h>

#include <string>

namespace spades {

	// The dresses worn right now, one a gun: set by the client just
	// before it builds a player's skins, so the skin scripts loading
	// their files in that one breath read the player's own dress and
	// nothing of anyone else's.
	static std::string g_dress[4];

	void SetWeaponDress(int gun, const std::string& prefix) {
		if (gun < 0 || gun > 3)
			return;
		g_dress[gun] = prefix;
	}

	bool PakFileExists(const std::string& path) {
		try {
			auto stream = FileManager::OpenForReading(path.c_str());
			return stream != nullptr;
		} catch (const std::exception&) {
			return false;
		}
	}

	// What the skin scripts ask for: the path of the file they load,
	// wearing the dress when its pak ships that file, the stock one
	// when it does not, so a dress that misses a piece leaves the
	// weapon whole rather than broken.
	static std::string DressPath(int gun, const std::string& path) {
		if (gun < 0 || gun > 3)
			return path;
		const std::string& dress = g_dress[gun];
		if (!dress.empty()) {
			const std::string dressed = dress + path;
			if (PakFileExists(dressed))
				return dressed;
		}
		return path;
	}

	class WeaponDressScriptObjectRegistrar : public ScriptObjectRegistrar {
	public:
		WeaponDressScriptObjectRegistrar()
			: ScriptObjectRegistrar("WeaponDress") {}

		virtual void Register(ScriptManager* manager, Phase phase) {
			asIScriptEngine* eng = manager->GetEngine();
			int r;
			eng->SetDefaultNamespace("spades");
			switch (phase) {
				case PhaseObjectMember:
					r = eng->RegisterGlobalFunction(
					  "string WeaponDressPath(int, const string& in)",
					  asFUNCTION(DressPath), asCALL_CDECL);
					manager->CheckError(r);
					break;
				default:
					break;
			}
			eng->SetDefaultNamespace("");
		}
	};

	static WeaponDressScriptObjectRegistrar registrar;
} // namespace spades
