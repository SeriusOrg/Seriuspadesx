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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.
 
 */
 
#include "BasicViewWeapon.as"
// The Tower Defense dresses' own view skins, one class a dress: the
// factories in the stock View.as files pick the class by the tier the
// store's word says, and each class spells the dress's pak folder in
// front of every file it loads.
#include "Rifle/M14.as"
#include "Rifle/Ballista.as"
#include "Rifle/Rifolver.as"
#include "Rifle/SV99.as"
#include "SMG/MP5K.as"
#include "SMG/AK47.as"
#include "SMG/M4Magpul.as"
#include "SMG/M134.as"
#include "Shotgun/SPAS12.as"
#include "Shotgun/Remington.as"
#include "Spade/BastardSword.as"
#include "Spade/Lightsaber.as"
#include "Spade/ThirdPerson.as"
#include "Spade/View.as"
#include "Block/ThirdPerson.as"
#include "Block/View.as"
#include "Grenade/ThirdPerson.as"
#include "Grenade/RPG.as"
#include "Grenade/View.as"
#include "Rifle/ThirdPerson.as"
#include "Rifle/View.as"
#include "SMG/ThirdPerson.as"
#include "SMG/View.as"
#include "Shotgun/ThirdPerson.as"
#include "Shotgun/View.as"