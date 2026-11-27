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

#pragma once

#include <string>

namespace spades {
	/** The Tower Defense store's weapon dresses: the pak folder a bought
	 * skin rides in, worn by nothing until the client sets it. Gun 0 is
	 * the rifle, 1 the SMG, 2 the shotgun, 3 the spade. */
	void SetWeaponDress(int gun, const std::string& prefix);

	/** Whether the paks hold this file. */
	bool PakFileExists(const std::string& path);
}
