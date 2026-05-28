/*
 * (C) Copyright 2001-2026 Diomidis Spinellis
 *
 * This file is part of CScout.
 *
 * CScout is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CScout is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CScout.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * CScout invocation options - static member definitions.
 *
 */

#include <string>
#include "idquery.h"
#include "options.h"

using namespace std;

e_process CscoutOptions::process_mode = pm_unspecified;
int CscoutOptions::portno = 8081;
string CscoutOptions::db_engine;
bool CscoutOptions::quiet = false;
IdQuery CscoutOptions::monitor;