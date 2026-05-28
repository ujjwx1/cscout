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
 * CScout invocation options.
 * Wraps command-line flags set during startup in main() and read
 * across the codebase.  All members are static - there is one
 * global instance of these options for the lifetime of the process.
 *
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>
#include "idquery.h"

using namespace std;

/* Processing mode set by command-line flags (-E, -c, etc.) */
enum e_process {
	pm_unspecified,		/* Default (web front-end) must be 0 */
	pm_preprocess,		/* Preprocess-only (-E) */
	pm_compile,		/* Compile-only (-c) */
	pm_report,		/* Generate a warning report */
	pm_database,
	pm_obfuscation,
	pm_call_graph
};

class CscoutOptions {
public:
	static e_process process_mode;
	static int portno;
	static string db_engine;
	static bool quiet;
	static IdQuery monitor;
};

#endif /* OPTIONS_H */