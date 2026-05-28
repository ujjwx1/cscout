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
 * REST/JSON API handlers for CScout.
 * Each handler is registered with swill_handle() in cscout.cpp and
 * returns a JSON response on its /api/ endpoint.
 * All handlers are read-only — no files are modified here.
 *
 */

#include <string>
#include <vector>
#include <set>
#include <cstdio>

#include "swill.h"
#include "attr.h"
#include "metrics.h"
#include "fileid.h"
#include "tokid.h"
#include "eclass.h"
#include "call.h"
#include "fcall.h"
#include "filedetails.h"
#include "idquery.h"
#include "funmetrics.h"
#include "filemetrics.h"
#include "rest.h"

using namespace std;

/* Escape a string for safe embedding in a JSON value.
 * Handles backslash, double-quote, and common control characters. */
static string
json_escape(const string &s)
{
	string r;
	for (string::const_iterator i = s.begin(); i != s.end(); i++) {
		switch (*i) {
		case '"': r += "\\\""; break;
		case '\\': r += "\\\\"; break;
		case '\n': r += "\\n"; break;
		case '\r': r += "\\r"; break;
		case '\t': r += "\\t"; break;
		default: r += *i; break;
		}
	}
	return r;
}

/* GET /api/identifiers
 * Returns all identifiers with their attributes as a JSON array. */
int
api_identifiers(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	fprintf(of, "[");
	bool first = true;
	for (IdProp::iterator i = Identifier::ids.begin(); i != Identifier::ids.end(); i++) {
		Eclass *e = i->first;
		Identifier &id = i->second;
		if (!first) fprintf(of, ",");
		first = false;
		fprintf(of, "{\"eid\":\"%p\","
			"\"name\":\"%s\","
			"\"readonly\":%s,"
			"\"macro\":%s,"
			"\"macroarg\":%s,"
			"\"ordinary\":%s,"
			"\"suetag\":%s,"
			"\"sumember\":%s,"
			"\"label\":%s,"
			"\"typedef\":%s,"
			"\"enumeration\":%s,"
			"\"yacc\":%s,"
			"\"fun\":%s,"
			"\"cscope\":%s,"
			"\"lscope\":%s,"
			"\"unused\":%s,"
			"\"xfile\":%s}",
			(void *)e,
			json_escape(id.get_id()).c_str(),
			e->get_attribute(is_readonly) ? "true" : "false",
			e->get_attribute(is_macro) ? "true" : "false",
			e->get_attribute(is_macro_arg) ? "true" : "false",
			e->get_attribute(is_ordinary) ? "true" : "false",
			e->get_attribute(is_suetag) ? "true" : "false",
			e->get_attribute(is_sumember) ? "true" : "false",
			e->get_attribute(is_label) ? "true" : "false",
			e->get_attribute(is_typedef) ? "true" : "false",
			e->get_attribute(is_enumeration) ? "true" : "false",
			e->get_attribute(is_yacc) ? "true" : "false",
			e->get_attribute(is_cfunction) ? "true" : "false",
			e->get_attribute(is_cscope) ? "true" : "false",
			e->get_attribute(is_lscope) ? "true" : "false",
			e->is_unused() ? "true" : "false",
			id.get_xfile() ? "true" : "false");
	}
	fprintf(of, "]");
	return 0;
}

/* GET /api/files
 * Returns all analyzed source files as a JSON array. */
int
api_files(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	vector <Fileid> files = Fileid::files(true);
	fprintf(of, "[");
	bool first = true;
	for (vector <Fileid>::iterator i = files.begin(); i != files.end(); i++) {
		if (!first) fprintf(of, ",");
		first = false;
		fprintf(of, "{\"fid\":%d,"
			"\"name\":\"%s\","
			"\"readonly\":%s}",
			i->get_id(),
			json_escape(i->get_path()).c_str(),
			i->get_readonly() ? "true" : "false");
	}
	fprintf(of, "]");
	return 0;
}

/* GET /api/functions
 * Returns all functions with fan-in and fan-out counts as a JSON array. */
int
api_functions(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	fprintf(of, "[");
	bool first = true;
	for (Call::const_fmap_iterator_type i = Call::fbegin(); i != Call::fend(); i++) {
		Call *fun = i->second;
		if (!first) fprintf(of, ",");
		first = false;
		fprintf(of, "{\"id\":\"%p\","
			"\"name\":\"%s\","
			"\"is_macro\":%s,"
			"\"is_defined\":%s,"
			"\"is_declared\":%s,"
			"\"is_file_scoped\":%s,"
			"\"fid\":%d,"
			"\"fanin\":%d,"
			"\"fanout\":%d}",
			(void *)fun,
			json_escape(fun->get_name()).c_str(),
			fun->is_macro() ? "true" : "false",
			fun->is_defined() ? "true" : "false",
			fun->is_declared() ? "true" : "false",
			fun->is_file_scoped() ? "true" : "false",
			fun->get_fileid().get_id(),
			fun->get_num_caller(),
			fun->get_num_call());
	}
	fprintf(of, "]");
	return 0;
}

/* GET /api/projects
 * Returns all projects in the current workspace as a JSON array. */
int
api_projects(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	const Project::proj_map_type &m = Project::get_project_map();
	fprintf(of, "[");
	bool first = true;
	for (Project::proj_map_type::const_iterator i = m.begin(); i != m.end(); i++) {
		if (!first) fprintf(of, ",");
		first = false;
		fprintf(of, "{\"pid\":%d,\"name\":\"%s\"}",
			(int)i->second,
			json_escape(i->first).c_str());
	}
	fprintf(of, "]");
	return 0;
}

/* GET /api/id?id=EID
 * Returns a single identifier's details and all its source locations. */
int
api_id(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	Eclass *e;
	if (!swill_getargs("p(id)", &e)) {
		fprintf(of, "{\"error\":\"Missing or invalid id parameter\"}");
		return 0;
	}

	IdProp::iterator idi = Identifier::ids.find(e);
	if (idi == Identifier::ids.end()) {
		fprintf(of, "{\"error\":\"Identifier not found\"}");
		return 0;
	}

	Identifier &id = idi->second;
	fprintf(of, "{\"eid\":\"%p\","
		"\"name\":\"%s\","
		"\"unused\":%s,"
		"\"xfile\":%s,"
		"\"locations\":[",
		(void *)e,
		json_escape(id.get_id()).c_str(),
		e->is_unused() ? "true" : "false",
		id.get_xfile() ? "true" : "false");

	const setTokid &members = e->get_members();
	bool first = true;
	for (setTokid::const_iterator j = members.begin(); j != members.end(); j++) {
		if (!first) fprintf(of, ",");
		first = false;
		int line = Filedetails::get_line_number(j->get_fileid(), j->get_streampos());
		fprintf(of, "{\"fid\":%d,"
			"\"file\":\"%s\","
			"\"line\":%d,"
			"\"offset\":%lu}",
			j->get_fileid().get_id(),
			json_escape(j->get_fileid().get_path()).c_str(),
			line,
			(unsigned long)j->get_streampos());
	}
	fprintf(of, "]}");
	return 0;
}

/* GET /api/funcs?callers=FID or ?callees=FID
 * Returns callers or callees of a function as a JSON array. */
int
api_funcs(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	Call *f;
	bool want_callers = false;
	bool want_callees = false;

	if (swill_getargs("p(callers)", &f)) {
		want_callers = true;
	} else if (swill_getargs("p(callees)", &f)) {
		want_callees = true;
	} else {
		fprintf(of, "{\"error\":\"Missing callers or callees parameter\"}");
		return 0;
	}

	fprintf(of, "[");
	bool first = true;

	if (want_callers) {
		for (Call::const_fiterator_type i = f->caller_begin(); i != f->caller_end(); i++) {
			if (!first) fprintf(of, ",");
			first = false;
			fprintf(of, "{\"id\":\"%p\",\"name\":\"%s\"}",
				(void *)*i,
				json_escape((*i)->get_name()).c_str());
		}
	} else if (want_callees) {
		for (Call::const_fiterator_type i = f->call_begin(); i != f->call_end(); i++) {
			if (!first) fprintf(of, ",");
			first = false;
			fprintf(of, "{\"id\":\"%p\",\"name\":\"%s\"}",
				(void *)*i,
				json_escape((*i)->get_name()).c_str());
		}
	}

	fprintf(of, "]");
	return 0;
}

/* GET /api/filemetrics?id=FID
 * Returns metrics for a single file as a JSON object. */
int
api_filemetrics(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	int fid;
	if (!swill_getargs("i(id)", &fid)) {
		fprintf(of, "{\"error\":\"Missing or invalid id parameter\"}");
		return 0;
	}

	Fileid fi(fid);
	fprintf(of, "{\"fid\":%d,\"name\":\"%s\",\"metrics\":{",
		fid, json_escape(fi.get_path()).c_str());

	bool first = true;
	for (int j = 0; j < FileMetrics::metric_max; j++) {
		if (Metrics::is_internal<FileMetrics>(j))
			continue;
		if (!first) fprintf(of, ",");
		first = false;
		fprintf(of, "\"%s\":%g",
			Metrics::get_dbfield<FileMetrics>(j).c_str(),
			Filedetails::get_pre_cpp_metrics(fi).get_metric(j));
	}

	fprintf(of, "}}");
	return 0;
}

/* GET /api/projectfiles?projid=PID
 * Returns all files belonging to a project as a JSON array. */
int
api_projectfiles(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	int pid;
	if (!swill_getargs("i(projid)", &pid)) {
		fprintf(of, "{\"error\":\"Missing or invalid projid parameter\"}");
		return 0;
	}

	vector <Fileid> files = Fileid::files(true);
	fprintf(of, "[");
	bool first = true;
	for (vector <Fileid>::iterator i = files.begin(); i != files.end(); i++) {
		if (!Filedetails::get_attribute(*i, pid))
			continue;
		if (!first) fprintf(of, ",");
		first = false;
		fprintf(of, "{\"fid\":%d,\"name\":\"%s\",\"readonly\":%s}",
			i->get_id(),
			json_escape(i->get_path()).c_str(),
			i->get_readonly() ? "true" : "false");
	}
	fprintf(of, "]");
	return 0;
}

/* GET /api/funmetrics?id=FID
 * Returns metrics for a single function as a JSON object. */
int
api_funmetrics(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	Call *f;
	if (!swill_getargs("p(id)", &f)) {
		fprintf(of, "{\"error\":\"Missing or invalid id parameter\"}");
		return 0;
	}

	fprintf(of, "{\"id\":\"%p\",\"name\":\"%s\",\"metrics\":{",
		(void *)f, json_escape(f->get_name()).c_str());

	bool first = true;
	for (int j = 0; j < FunMetrics::metric_max; j++) {
		if (Metrics::is_internal<FunMetrics>(j))
			continue;
		if (!first) fprintf(of, ",");
		first = false;
		if (Metrics::is_pre_cpp<FunMetrics>(j))
			fprintf(of, "\"%s\":%g",
				Metrics::get_dbfield<FunMetrics>(j).c_str(),
				f->get_pre_cpp_metrics().get_metric(j));
		else if (Metrics::is_post_cpp<FunMetrics>(j))
			fprintf(of, "\"%s\":%g",
				Metrics::get_dbfield<FunMetrics>(j).c_str(),
				f->get_post_cpp_metrics().get_metric(j));
		else
			fprintf(of, "\"%s\":null",
				Metrics::get_dbfield<FunMetrics>(j).c_str());
	}

	fprintf(of, "}}");
	return 0;
}

/* GET /api/refactor?id=EID&newname=NAME
 * Returns a preview of renaming an identifier.
 * Strictly read-only — modification_state is never touched,
 * file_refactor() is never called. */
int
api_refactor(FILE *of, void *)
{
	swill_setheader("content-type", "application/json");

	Eclass *e;
	if (!swill_getargs("p(id)", &e)) {
		fprintf(of, "{\"error\":\"Missing or invalid id parameter\"}");
		return 0;
	}

	char *newname = swill_getvar("newname");
	if (!newname || !*newname) {
		fprintf(of, "{\"error\":\"Missing newname parameter\"}");
		return 0;
	}

	IdProp::iterator idi = Identifier::ids.find(e);
	if (idi == Identifier::ids.end()) {
		fprintf(of, "{\"error\":\"Identifier not found\"}");
		return 0;
	}

	Identifier &id = idi->second;
	string oldname = id.get_id();

	const setTokid &members = e->get_members();
	set<Fileid> affected_files;
	for (setTokid::const_iterator j = members.begin(); j != members.end(); j++)
		affected_files.insert(j->get_fileid());

	fprintf(of, "{\"eid\":\"%p\","
		"\"old_name\":\"%s\","
		"\"new_name\":\"%s\","
		"\"affected_files\":%d,"
		"\"total_replacements\":%d,"
		"\"changes\":[",
		(void *)e,
		json_escape(oldname).c_str(),
		json_escape(newname).c_str(),
		(int)affected_files.size(),
		(int)members.size());

	bool first_file = true;
	for (set<Fileid>::const_iterator fi = affected_files.begin(); fi != affected_files.end(); fi++) {
		if (!first_file) fprintf(of, ",");
		first_file = false;

		fprintf(of, "{\"fid\":%d,"
			"\"file\":\"%s\","
			"\"replacements\":[",
			fi->get_id(),
			json_escape(fi->get_path()).c_str());

		bool first_loc = true;
		for (setTokid::const_iterator j = members.begin(); j != members.end(); j++) {
			if (j->get_fileid() != *fi)
				continue;
			if (!first_loc) fprintf(of, ",");
			first_loc = false;
			int line = Filedetails::get_line_number(j->get_fileid(), j->get_streampos());
			fprintf(of, "{\"line\":%d,"
				"\"offset\":%lu,"
				"\"length\":%d,"
				"\"old\":\"%s\","
				"\"new\":\"%s\"}",
				line,
				(unsigned long)j->get_streampos(),
				e->get_len(),
				json_escape(oldname).c_str(),
				json_escape(newname).c_str());
		}

		fprintf(of, "]}");
	}

	fprintf(of, "]}");
	return 0;
}