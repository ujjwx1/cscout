/*
 * (C) Copyright 2008-2026 Diomidis Spinellis
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
 * HTML utility functions
 *
 */

#include <map>
#include <string>
#include <deque>
#include <vector>
#include <stack>
#include <iterator>
#include <iostream>
#include <fstream>
#include <list>
#include <set>
#include <utility>
#include <functional>
#include <algorithm>		// set_difference
#include <cctype>
#include <sstream>		// ostringstream
#include <cstdio>		// perror, rename
#include <cstdlib>		// atoi
#include <cerrno>		// errno

#include "swill.h"
#include "getopt.h"

#include "cpp.h"
#include "debug.h"
#include "error.h"
#include "parse.tab.h"
#include "attr.h"
#include "metrics.h"
#include "fileid.h"
#include "tokid.h"
#include "token.h"
#include "ptoken.h"
#include "fchar.h"
#include "pltoken.h"
#include "macro.h"
#include "pdtoken.h"
#include "eclass.h"
#include "ctoken.h"
#include "type.h"
#include "stab.h"
#include "fdep.h"
#include "version.h"
#include "call.h"
#include "html.h"
#include "fileutils.h"
#include "option.h"
#include "attr.h"
#include "idquery.h"
#include "funquery.h"
#include "options.h"

/* HTML interface state variables.
 * Declared extern in html.h so cscout.cpp can access them too. */
enum e_modification_state modification_state = ms_unmodified;
bool browse_only = false;
bool must_exit = false;
Attributes::size_type current_project = 0;

/*
 * Return as a C string the HTML equivalent of character c
 * Handles tab-stop expansion provided all output is processed through this
 * function
 */
const char *
html(char c)
{
	static char str[2];
	static int column = 0;
	static vector<string> spaces(0);
	int space_idx;

	switch (c) {
	case '&': column++; return "&amp;";
	case '<': column++; return "&lt;";
	case '>': column++; return "&gt;";
	case '"': column++; return "&quot;";
	case ' ': column++; return "&nbsp;";
	case '\t':
		if ((int)(spaces.size()) != Option::tab_width->get()) {
			spaces.resize(Option::tab_width->get());
			for (int i = 0; i < Option::tab_width->get(); i++) {
				string t;
				for (int j = 0; j < Option::tab_width->get() - i; j++)
					t += "&nbsp;";
				spaces[i] = t;
			}
		}
		space_idx = column % Option::tab_width->get();
		if (DP())
			cout << "Tab; " << " column before:" << column << " space_idx: " << space_idx << endl;
		column += 8 - space_idx;
		return spaces[space_idx].c_str();
	case '\n':
		column = 0;
		return "<br>\n";
	case '\r':
		column = 0;
		return "";
	case '\f':
		column = 0;
		return "<br><hr><br>\n";
	default:
		column++;
		str[0] = c;
		return str;
	}
}

// HTML-encode the given string
string
html(const string &s)
{
	string r;

	for (string::const_iterator i = s.begin(); i != s.end(); i++)
		r += html(*i);
	return r;
}

// Output s as HTML in of
void
html_string(FILE *of, string s)
{
	for (string::const_iterator i = s.begin(); i != s.end(); i++)
		fputs(html(*i), of);
}


// Create a new HTML file with a given filename and title
// The heading, if not given, will be the same as the title
void
html_head(FILE *of, const string fname, const string title, const char *heading)
{
	swill_title(title.c_str());
	if (DP())
		cerr << "Write to " << fname << endl;
	fprintf(of,
		"<!doctype html public \"-//IETF//DTD HTML//EN\">\n"
		"<html>\n"
		"<head>\n"
		"<meta name=\"GENERATOR\" content=\"CScout %s - %s\">\n",
		Version::get_revision().c_str(),
		Version::get_date().c_str());
	fputs(
		"<meta http-equiv=\"Content-Style-Type\" content=\"text/css\">"
		"<style type=\"text/css\" >"
		"<!--\n", of);

	ifstream in;
	string css_fname;
	if (cscout_input_file("style.css", in, css_fname)) {
		int val;
		while ((val = in.get()) != EOF)
			putc(val, of);
	} else
		fputs(
		#include "css.cpp"
		, of);
	fputs(
		"-->"
		"</style>", of);
	fprintf(of,
		"<title>%s</title>\n"
		"</head>\n"
		"<body>\n"
		"<h1>%s</h1>\n",
		title.c_str(),
		heading ? heading : title.c_str());
}

// And an HTML file end
void
html_tail(FILE *of)
{
	extern Attributes::size_type current_project;

	if (current_project)
		fprintf(of, "<p> <b>Project %s is currently selected</b>\n", Project::get_projname(current_project).c_str());
	fprintf(of,
		"<p>"
		"<a href=\"index.html\">Main page</a>\n"
		" &mdash; Web: "
		"<a href=\"http://www.spinellis.gr/cscout\">Home</a>\n"
		"<a href=\"http://www.spinellis.gr/cscout/doc/index.html\">Manual</a>\n"
		"<br><hr><div class=\"footer\">CScout %s &mdash; %s",
		Version::get_revision().c_str(),
		Version::get_date().c_str());
	fprintf(of, " &mdash; Licensed under the GNU General Public License.");
	fprintf(of, "</div></body></html>\n");
}

// Return a function's label, based on the user's preferences
string
file_label(Fileid f, bool hyperlink)
{
	string result;
	char buff[256];

	if (hyperlink) {
		snprintf(buff, sizeof(buff), "<a href=\"file.html?id=%d\">", f.get_id());
		result = buff;
	}
	switch (Option::fgraph_show->get()) {
	case 'p':			// Show complete paths
		result += f.get_path() + "/";
		/* FALLTHROUGH */
	case 'n':			// Show only file names
		result += f.get_fname();
		break;
	case 'e':			// Show only edges
		result += " ";
		break;
	}
	if (hyperlink)
		result += "</a>";
	return (result);
}

// Return a function's label, based on the user's preferences
string
function_label(Call *f, bool hyperlink)
{
	string result;
	char buff[256];

	if (hyperlink) {
		snprintf(buff, sizeof(buff), "<a href=\"fun.html?f=%p\">", (void *)f);
		result = buff;
	}
	if (Option::show_function_type->get()) {
		if (f->is_file_scoped())
			result += "static:";
		else
			result += "public:";
	}
	if (Option::is_defined->get()) {
		if (f->is_defined())
			result += "1:";
		else
			result += "0:";
	}
	if (Option::line_num->get()) {
        Tokid t;
		if (f->is_defined()) {
            t = f->get_definition();
        } else {
            t = f->get_tokid();
        }
        int first = Filedetails::get_line_number(t.get_fileid(), t.get_streampos());
        int last = first + f->get_pre_cpp_metrics().get_metric(Metrics::em_nline);
        result += to_string(first) + ";" + to_string(last) + ":";
	}
	if (Option::cgraph_show->get() == 'f')		// Show files
		result += f->get_site().get_fileid().get_fname() + ":";
	else if (Option::cgraph_show->get() == 'p')	// Show paths
		result += f->get_site().get_fileid().get_path() + ":";
	if (Option::cgraph_show->get() != 'e')		// Empty labels
		result += f->get_name();
	if (hyperlink)
		result += "</a>";
	return (result);
}

// Display a system error on the HTML output.
void
html_perror(FILE *of, const string &user_msg, bool svg)
{
	string error_msg(user_msg + ": " + string(strerror(errno)) + "\n");
	fputs(error_msg.c_str(), stderr);
	if (svg)
		fprintf(of, "<?xml version=\"1.0\" ?>\n"
			"<svg>\n"
			"<text  x=\"20\" y=\"50\" >%s</text>\n"
			"</svg>\n", error_msg.c_str());
	else {
		fputs(error_msg.c_str(), of);
		fputs("</p><p>The operation you requested is incomplete.  "
			"Please correct the underlying cause, and (if possible) return to the "
			"CScout <a href=\"index.html\">main page</a> to retry the operation.</p>", of);
	}
}

// Display a non-system error on the HTML output.
void
html_error(FILE *of, const string &error_msg)
{
	fputs(error_msg.c_str(), stderr);
	fputs(error_msg.c_str(), of);
	fputs("</p><p>The operation you requested is incomplete.  "
		"Please correct the underlying cause, and (if possible) return to the "
		"CScout <a href=\"index.html\">main page</a> to retry the operation.</p>", of);
}


// Display an identifier hyperlink
void
html(FILE *of, const IdPropElem &i)
{
	fprintf(of, "<a href=\"id.html?id=%p\">", (void *)i.first);
	html_string(of, (i.second).get_id());
	fputs("</a>", of);
}

void
html(FILE *of, const Call &c)
{
	fprintf(of, "<a href=\"fun.html?f=%p\">", (void *)&c);
	html_string(of, c.get_name());
	fputs("</a>", of);
}

// Display a hyperlink based on a string and its starting tokid
void
html_string(FILE *of, const string &s, Tokid t)
{
	int len = s.length();
	for (int pos = 0; pos < len;) {
		Eclass *ec = t.get_ec();
		Identifier id(ec, s.substr(pos, ec->get_len()));
		const IdPropElem ip(ec, id);
		html(of, ip);
		pos += ec->get_len();
		t += ec->get_len();
		if (pos < len)
			fprintf(of, "][");
	}
}

// Display hyperlinks to a function's identifiers
void
html_string(FILE *of, const Call *f)
{
	int start = 0;
	for (dequeTpart::const_iterator i = f->get_token().get_parts_begin(); i != f->get_token().get_parts_end(); i++) {
		Tokid t = i->get_tokid();
		putc('[', of);
		html_string(of, f->get_name().substr(start, i->get_len()), t);
		putc(']', of);
		start += i->get_len();
	}
}



// Display the contents of a file in hypertext form
void
file_hypertext(FILE *of, Fileid fi, bool eval_query)
{
	istream *in;
	const string &fname = fi.get_path();
	bool at_bol = true;
	int line_number = 1;
	bool mark_unprocessed = !!swill_getvar("marku");

	/*
	 * In theory this could be handled by adding a class
	 * factory method to Query, and making eval virtual.
	 * In practice the IdQuery and FunQuery eval methods
	 * take incompatible arguments, and are difficult to
	 * reconcile.
	 */
	IdQuery idq;
	FunQuery funq;
	bool have_funq, have_idq;
	char *qtype = swill_getvar("qt");
	have_funq = have_idq = false;
	if (!qtype || strcmp(qtype, "id") == 0) {
		idq = IdQuery(of, Option::file_icase->get(), current_project, eval_query);
		have_idq = true;
	} else if (strcmp(qtype, "fun") == 0) {
		funq = FunQuery(of, Option::file_icase->get(), current_project, eval_query);
		have_funq = true;
	} else {
		fprintf(stderr, "Unknown query type (try adding &qt=id to the URL).\n");
		return;
	}

	if (DP())
		cout << "Write to " << fname << endl;
	if (Filedetails::is_hand_edited(fi)) {
		in = new istringstream(Filedetails::get_original_contents(fi));
		fputs("<p>This file has been edited by hand. The following code reflects the contents before the first CScout-invoked hand edit.</p>", of);
	} else {
		in = new ifstream(fname.c_str(), ios::binary);
		if (in->fail()) {
			html_perror(of, "Unable to open " + fname + " for reading");
			return;
		}
	}
	fputs("<hr><code>", of);
	(void)html('\n');	// Reset HTML tab handling
	// Go through the file character by character
	for (;;) {
		Tokid ti;
		int val;

		ti = Tokid(fi, in->tellg());
		if ((val = in->get()) == EOF)
			break;
		if (at_bol) {
			fprintf(of,"<a name=\"%d\"></a>", line_number);
			if (mark_unprocessed && !Filedetails::is_line_processed(fi, line_number))
				fprintf(of, "<span class=\"unused\">");
			if (Option::show_line_number->get()) {
				char buff[50];
				snprintf(buff, sizeof(buff), "%5d ", line_number);
				// Do not go via HTML string to keep tabs ok
				for (char *s = buff; *s; s++)
					if (*s == ' ')
						fputs("&nbsp;", of);
					else
						fputc(*s, of);
			}
			at_bol = false;
		}
		// Identifier we can mark
		Eclass *ec;
		if (have_idq && (ec = ti.check_ec()) && ec->is_identifier() && idq.need_eval()) {
			string s;
			s = (char)val;
			int len = ec->get_len();
			for (int j = 1; j < len; j++)
				s += (char)in->get();
			Identifier i(ec, s);
			const IdPropElem ip(ec, i);
			if (idq.eval(ip))
				html(of, ip);
			else
				html_string(of, s);
			continue;
		}
		// Function we can mark
		if (have_funq && funq.need_eval()) {
			pair <Call::const_fmap_iterator_type, Call::const_fmap_iterator_type> be(Call::get_calls(ti));
			Call::const_fmap_iterator_type ci;
			for (ci = be.first; ci != be.second; ci++)
				if (funq.eval(ci->second)) {
					string s;
					s = (char)val;
					int len = ci->second->get_name().length();
					for (int j = 1; j < len; j++)
						s += (char)in->get();
					html(of, *(ci->second));
					break;
				}
			if (ci != be.second)
				continue;
		}
		fprintf(of, "%s", html((char)val));
		if ((char)val == '\n') {
			at_bol = true;
			if (mark_unprocessed && !Filedetails::is_line_processed(fi, line_number))
				fprintf(of, "</span>");
			line_number++;
		}
	}
	delete in;
	fputs("<hr></code>", of);
}

void
change_prohibited(FILE *fo)
{
	html_head(fo, "nochange", "Change Prohibited");
	fputs("Identifier substitutions or function argument refactoring are not allowed "
		"to be performed together with and the hand-editing of files"
		"within the same CScout session.", fo);
	html_tail(fo);
}

void
nonbrowse_operation_prohibited(FILE *fo)
{
	html_head(fo, "nochange", "Non-browsing Operations Disabled");
	fputs("This is a multiuser browse-only CScout session."
		"Non-browsing operations are disabled.", fo);
	html_tail(fo);
}

// Call before the start of a file list
void
html_file_begin(FILE *of)
{
	if (Option::fname_in_context->get())
		fprintf(of, "<table class='dirlist'><tr><th>Directory</th><th>File</th>");
	else
		fprintf(of, "<table><tr><th></th><th></th>");
}

// Call before actually listing files (after printing additional headers)
void
html_file_set_begin(FILE *of)
{
	fprintf(of, "</tr>\n");
}

// Called after html_file (after printing additional columns)
void
html_file_record_end(FILE *of)
{
	fprintf(of, "</tr>\n");
}

// Called at the end
void
html_file_end(FILE *of)
{
	fprintf(of, "</table>\n");
}

// Display a filename of an html file
void
html_file(FILE *of, Fileid fi)
{
	if (!Option::fname_in_context->get()) {
		fprintf(of, "\n<tr><td></td><td><a href=\"file.html?id=%u\">%s</a></td>",
			fi.get_id(),
			fi.get_path().c_str());
		return;
	}

	// Split path into dir and fname
	string s(fi.get_path());
	string::size_type k = s.find_last_of("/\\");
	if (k == string::npos)
		k = 0;
	else
		k++;
	string dir(s, 0, k);
	string fname(s, k);

	fprintf(of, "<tr><td align=\"right\">%s\n</td>\n", dir.c_str());
	fprintf(of, "<td><a href=\"file.html?id=%u\">%s</a></td>",
		fi.get_id(),
		fname.c_str());
}