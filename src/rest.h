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
 * REST/JSON API handler declarations.
 * Implementations live in rest.cpp.
 * Handlers are registered with swill_handle() in cscout.cpp.
 *
 */

#ifndef REST_H
#define REST_H

#include <cstdio>

int api_identifiers(FILE *of, void *);
int api_files(FILE *of, void *);
int api_functions(FILE *of, void *);
int api_projects(FILE *of, void *);
int api_id(FILE *of, void *);
int api_funcs(FILE *of, void *);
int api_filemetrics(FILE *of, void *);
int api_projectfiles(FILE *of, void *);
int api_funmetrics(FILE *of, void *);
int api_refactor(FILE *of, void *);

#endif /* REST_H */