/*******************************************************************************
 * This file is part of LineGuardian.                                          *
 *                                                                             *
 * Copyright (C) 2015-2016 by SukkoPera <software@sukkology.net>               *
 *                                                                             *
 * LineGuardian is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU General Public License as published by        *
 * the Free Software Foundation, either version 3 of the License, or           *
 * (at your option) any later version.                                         *
 *                                                                             *
 * LineGuardian is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 * GNU General Public License for more details.                                *
 *                                                                             *
 * You should have received a copy of the GNU General Public License           *
 * along with LineGuardian. If not, see <http://www.gnu.org/licenses/>.        *
 *******************************************************************************
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#ifndef NDEBUG
	#define DSTART(bps) do {Serial.begin (bps); while (!Serial);} while (0)
	#define DPRINT(...) Serial.print(__VA_ARGS__)
	#define DPRINTLN(...) Serial.println(__VA_ARGS__)
#else
	#define DSTART(...) do {} while (0)
	#define DPRINT(...) do {} while (0)
	#define DPRINTLN(...) do {} while (0)
#endif

#endif
