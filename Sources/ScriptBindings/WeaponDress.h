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
	 * the rifle, 1 the SMG, 2 the shotgun, 3 the spade, 4 the grenade. */
	void SetWeaponDress(int gun, const std::string& prefix);

	/** The tier of the dress each gun wears, zero when none: the charm
	 * a gun shows is chosen by tier, so a store that sells upgrades
	 * of one skin family hands the buyer a new charm with each rung. */
	void SetWeaponDressTier(int gun, int tier);

	/** The charm the gun's dress tier wears: the tier's own when a
	 * dress is worn, `stock` when it is not. */
	std::string WeaponCharmPath(int gun, const std::string& stock);

	/** Whether the paks hold this file. */
	bool PakFileExists(const std::string& path);

	/** Whether the dress the gun wears ships the file at `path`: true
	 * with no dress worn, and with one only when its pak carries the
	 * file, so a dress that ships none of its own sights leaves them
	 * out rather than carrying the stock ones on a body they were
	 * never carved for. */
	bool WeaponDressShips(int gun, const std::string& path);
}
