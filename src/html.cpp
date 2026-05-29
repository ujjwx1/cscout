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
#include "pager.h"
#include "timer.h"

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


/*
 * Display the sorted identifiers or functions, taking into account the reverse sort property
 * for properly aligning the output.
 */
template <typename container>
void
display_sorted(FILE *of, const Query &query, const container &sorted_ids)
{
	if (Option::sort_rev->get())
		fputs("<table><tr><td width=\"50%\" align=\"right\">\n", of);
	else
		fputs("<p>\n", of);

	Pager pager(of, Option::entries_per_page->get(), query.base_url() + "&qi=1", query.bookmarkable());
	typename container::const_iterator i;
	for (i = sorted_ids.begin(); i != sorted_ids.end(); i++) {
		if (pager.show_next()) {
			html(of, **i);
			fputs("<br>\n", of);
		}
	}

	if (Option::sort_rev->get())
		fputs("</td> <td width=\"50%\"> </td></tr></table>\n", of);
	else
		fputs("</p>\n", of);
	pager.end();
}

/*
 * Display the sorted functions with their metrics,
 * taking into account the reverse sort property
 * for properly aligning the output.
 */
void
display_sorted_function_metrics(FILE *of, const FunQuery &query, const Sfuns &sorted_ids)
{
	fprintf(of, "<table class=\"metrics\"><tr>"
	    "<th width='50%%' align='left'>Name</th>"
	    "<th width='50%%' align='right'>%s</th>\n",
	    Metrics::get_name<FunMetrics>(query.get_sort_order()).c_str());

	Pager pager(of, Option::entries_per_page->get(), query.base_url() + "&qi=1", query.bookmarkable());
	for (Sfuns::const_iterator i = sorted_ids.begin(); i != sorted_ids.end(); i++) {
		if (pager.show_next()) {
			fputs("<tr><td witdh='50%'>", of);
			html(of, **i);
			fprintf(of, "</td><td witdh='50%%' align='right'>%g</td></tr>\n",
			    (*i)->get_pre_cpp_const_metrics().get_metric(query.get_sort_order()));
		}
	}
	fputs("</table>\n", of);
	pager.end();
}


// Identifier query page
int
iquery_page(FILE *of,  void *)
{
	html_head(of, "iquery", "Identifier Query");
	fputs("<FORM ACTION=\"xiquery.html\" METHOD=\"GET\">\n"
	"<input type=\"checkbox\" name=\"writable\" value=\"1\">Writable<br>\n", of);
	for (int i = attr_begin; i < attr_end; i++)
		fprintf(of, "<input type=\"checkbox\" name=\"a%d\" value=\"1\">%s<br>\n", i,
			Attributes::name(i).c_str());
	fputs(
	"<input type=\"checkbox\" name=\"xfile\" value=\"1\">Crosses file boundary<br>\n"
	"<input type=\"checkbox\" name=\"unused\" value=\"1\">Unused<br>\n"
	"<p>\n"
	"<input type=\"radio\" name=\"match\" value=\"Y\" CHECKED>Match any marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"L\">Match all marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"E\">Exclude marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"T\" >Exact match\n"
	"<br><hr>\n"
	"<table>\n"
	"<tr><td>\n"
	"Identifier names should "
	"(<input type=\"checkbox\" name=\"xire\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"ire\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"
	"<tr><td>\n"
	"Select identifiers from filenames "
	"(<input type=\"checkbox\" name=\"xfre\" value=\"1\"> not) \n"
	" matching RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"
	"</table>\n"
	"<hr>\n"
	"<p>Query title <INPUT TYPE=\"text\" NAME=\"n\" SIZE=60 MAXLENGTH=256>\n"
	"&nbsp;&nbsp;<INPUT TYPE=\"submit\" NAME=\"qi\" VALUE=\"Show identifiers\">\n"
	"<INPUT TYPE=\"submit\" NAME=\"qf\" VALUE=\"Show files\">\n"
	"<INPUT TYPE=\"submit\" NAME=\"qfun\" VALUE=\"Show functions\">\n"
	"</FORM>\n"
	, of);
	html_tail(of);
	return 0;
}

// Function query page
int
funquery_page(FILE *of,  void *)
{
	html_head(of, "funquery", "Function Query");
	fputs("<FORM ACTION=\"xfunquery.html\" METHOD=\"GET\">\n"
	"<input type=\"checkbox\" name=\"cfun\" value=\"1\">C function<br>\n"
	"<input type=\"checkbox\" name=\"macro\" value=\"1\">Function-like macro<br>\n"
	"<input type=\"checkbox\" name=\"writable\" value=\"1\">Writable declaration<br>\n"
	"<input type=\"checkbox\" name=\"ro\" value=\"1\">Read-only declaration<br>\n"
	"<input type=\"checkbox\" name=\"pscope\" value=\"1\">Project scope<br>\n"
	"<input type=\"checkbox\" name=\"fscope\" value=\"1\">File scope<br>\n"
	"<input type=\"checkbox\" name=\"defined\" value=\"1\">Defined<br>\n", of);
	MQuery<FunMetrics, Call &>::metrics_query_form(of);
	fputs("<p>"
	"<input type=\"radio\" name=\"match\" value=\"Y\" CHECKED>Match any marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"L\">Match all marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"E\">Exclude marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"T\" >Exact match\n"
	"<br><hr>\n"
	"<table>\n"

	"<tr><td>\n"
	"Number of direct callers\n"
	"<select name=\"ncallerop\" value=\"1\">\n",
	of);
	Query::equality_selection(of);
	fputs(
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"ncallers\" SIZE=5 MAXLENGTH=10>\n"
	"</td><td>\n"

	"<tr><td>\n"
	"Function names should "
	"(<input type=\"checkbox\" name=\"xfnre\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fnre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"

	"<tr><td>\n"
	"Names of calling functions should "
	"(<input type=\"checkbox\" name=\"xfure\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fure\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"

	"<tr><td>\n"
	"Names of called functions should "
	"(<input type=\"checkbox\" name=\"xfdre\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fdre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"

	"<tr><td>\n"
	"Select functions from filenames "
	"(<input type=\"checkbox\" name=\"xfre\" value=\"1\"> not) \n"
	" matching RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"
	"</table>\n"
	"<hr>\n"
	"<p>Query title <INPUT TYPE=\"text\" NAME=\"n\" SIZE=60 MAXLENGTH=256>\n"
	"&nbsp;&nbsp;<INPUT TYPE=\"submit\" NAME=\"qi\" VALUE=\"Show functions\">\n"
	"<INPUT TYPE=\"submit\" NAME=\"qf\" VALUE=\"Show files\">\n"
	"</FORM>\n"
	, of);
	html_tail(of);
	return 0;
}

void
display_files(FILE *of, const Query &query, const IFSet &sorted_files)
{
	const string query_url(query.param_url());

	fputs("<h2>Matching Files</h2>\n", of);
	html_file_begin(of);
	html_file_set_begin(of);
	Pager pager(of, Option::entries_per_page->get(), query.base_url() + "&qf=1", query.bookmarkable());
	for (IFSet::iterator i = sorted_files.begin(); i != sorted_files.end(); i++) {
		Fileid f = *i;
		if (current_project && !Filedetails::get_attribute(f, current_project))
			continue;
		if (pager.show_next()) {
			html_file(of, *i);
			fprintf(of, "<td><a href=\"qsrc.html?id=%u&%s\">marked source</a></td>",
				f.get_id(),
				query_url.c_str());
			if (modification_state != ms_subst && !browse_only)
				fprintf(of, "<td><a href=\"fedit.html?id=%u\">edit</a></td>",
				f.get_id());
			html_file_record_end(of);
		}
	}
	html_file_end(of);
	pager.end();
}

// Process an identifier query
int
xiquery_page(FILE *of,  void *)
{
	Timer timer;
	prohibit_remote_access(of);

	Sids sorted_ids;
	IFSet sorted_files;
	set <Call *> funs;
	bool q_id = !!swill_getvar("qi");	// Show matching identifiers
	bool q_file = !!swill_getvar("qf");	// Show matching files
	bool q_fun = !!swill_getvar("qfun");	// Show matching functions
	char *qname = swill_getvar("n");
	IdQuery query(of, Option::file_icase->get(), current_project);

	if (!query.is_valid()) {
		html_tail(of);
		return 0;
	}

	html_head(of, "xiquery", (qname && *qname) ? qname : "Identifier Query Results");
	if (!CscoutOptions::quiet)
	    cerr << "Evaluating identifier query" << endl;
	for (IdProp::iterator i = Identifier::ids.begin(); i != Identifier::ids.end(); i++) {
		progress(i, Identifier::ids);
		if (!query.eval(*i))
			continue;
		if (q_id)
			sorted_ids.insert(&*i);
		else if (q_file) {
			IFSet f = i->first->sorted_files();
			sorted_files.insert(f.begin(), f.end());
		} else if (q_fun) {
			set <Call *> ecfuns(i->first->functions());
			funs.insert(ecfuns.begin(), ecfuns.end());
		}
	}
	if (!CscoutOptions::quiet)
	    cerr << endl;
	if (q_id) {
		fputs("<h2>Matching Identifiers</h2>\n", of);
		display_sorted(of, query, sorted_ids);
	}
	if (q_file)
		display_files(of, query, sorted_files);
	if (q_fun) {
		fputs("<h2>Matching Functions</h2>\n", of);
		Sfuns sorted_funs([](const Call *a, const Call *b) {
			return Query::string_bi_compare(a->get_name(), b->get_name());
		});
		sorted_funs.insert(funs.begin(), funs.end());
		display_sorted(of, query, sorted_funs);
	}

	timer.print_elapsed(of);
	html_tail(of);
	return 0;
}

// Process a function query
int
xfunquery_page(FILE *of,  void *)
{
	prohibit_remote_access(of);
	Timer timer;

	IFSet sorted_files;
	bool q_id = !!swill_getvar("qi");	// Show matching identifiers
	bool q_file = !!swill_getvar("qf");	// Show matching files
	char *qname = swill_getvar("n");
	FunQuery query(of, Option::file_icase->get(), current_project);
	Sfuns sorted_funs(query.get_comparator());

	if (!query.is_valid())
		return 0;

	html_head(of, "xfunquery", (qname && *qname) ? qname : "Function Query Results");
	if (!CscoutOptions::quiet)
	    cerr << "Evaluating function query" << endl;
	for (Call::const_fmap_iterator_type i = Call::fbegin(); i != Call::fend(); i++) {
		progress(i, Call::functions());
		if (!query.eval(i->second))
			continue;
		if (q_id)
			sorted_funs.insert(i->second);
		if (q_file)
			sorted_files.insert(i->second->get_fileid());
	}
	if (!CscoutOptions::quiet)
	    cerr << endl;
	if (q_id) {
		fputs("<h2>Matching Functions</h2>\n", of);
		if (query.get_sort_order() != -1)
			display_sorted_function_metrics(of, query, sorted_funs);
		else
			display_sorted(of, query, sorted_funs);
	}
	if (q_file)
		display_files(of, query, sorted_files);
	timer.print_elapsed(of);
	html_tail(of);
	return 0;
}

// Display an identifier property
void
show_id_prop(FILE *fo, const string &name, bool val)
{
	if (!Option::show_true->get() || val)
		fprintf(fo, ("<li>" + name + ": %s</li>\n").c_str(), val ? "Yes" : "No");
}

// Display whether a macro can be replaced by a C constant
void
show_c_const(FILE *fo, Eclass *e)
{
	bool val = !e->get_attribute(is_fun_macro)
		&& !e->get_attribute(is_cpp_const)
		&& !e->get_attribute(is_cpp_str_val)
		&& ((e->get_attribute(is_def_c_const)
			    && !e->get_attribute(is_def_not_c_const))
		    || (e->get_attribute(is_exp_c_const)
			    && !e->get_attribute(is_exp_not_c_const))
		   );
	fprintf(fo, "<li>Can be replaced by C constant: %s\n", val ? "Yes" : "No");
	fprintf(fo, "<ul>\n");
	for (int i = is_fun_macro; i <= is_exp_not_c_const; i++)
		show_id_prop(fo, Attributes::name(i), e->get_attribute(i));
	fprintf(fo, "</ul></li>\n");
}


/*
 * Visit all functions associated with a call/caller relationship with f
 * Call_path is an HTML string to print next to each function that
 * leads to a page showing the function's call path.  A single %p
 * in the string is used as a placeholder to fill the function's address.
 * The methods to obtain the relationship containers are passed through
 * the fbegin and fend method pointers.
 * If recurse is true the list will also contain all correspondingly
 * associated children functions.
 * If show is true, then a function hyperlink is printed, otherwise
 * only the visited flag is set to visit_id.
 */
void
visit_functions(FILE *fo, const char *call_path, Call *f,
	Call::const_fiterator_type (Call::*fbegin)() const,
	Call::const_fiterator_type (Call::*fend)() const,
	bool recurse, bool show, int level, int visit_id)
{
	if (level == 0)
		return;

	Call::const_fiterator_type i;

	f->set_visited(visit_id);
	for (i = (f->*fbegin)(); i != (f->*fend)(); i++) {
		if (show && (!(*i)->is_visited(visit_id) || *i == f)) {
			fprintf(fo, "<li> ");
			html(fo, **i);
			if (recurse && call_path)
				fprintf(fo, call_path, *i);
		}
		if (recurse && !(*i)->is_visited(visit_id))
			visit_functions(fo, call_path, *i, fbegin, fend, recurse, show, level - 1, visit_id);
	}
}

/*
 * Visit all files associated with a includes/included relationship with f
 * The method to obtain the relationship container is passed through
 * the get_map function pointer.
 * The method to check if a file should be included in the visit is passed through the
 * the is_ok method pointer.
 * Set the visited flag for all nodes visited.
 */
void
visit_include_files(Fileid f, const FileIncMap & (*get_map)(Fileid fi),
    bool (IncDetails::*is_ok)() const, int level)
{
	if (level == 0)
		return;

	if (DP())
		cout << "Visiting " << f.get_fname() << endl;
	Filedetails::set_visited(f);
	const FileIncMap &m = get_map(f);
	for (FileIncMap::const_iterator i = m.begin(); i != m.end(); i++) {
		if (!Filedetails::is_visited(i->first) && (i->second.*is_ok)())
			visit_include_files(i->first, get_map, is_ok, level - 1);
	}
}

/*
 * Visit all files associated with a global variable def/ref relationship with f
 * The method to obtain the relationship container is passed through
 * the get_fileid_set method pointer.
 * Set the visited flag for all nodes visited.
 */
void
visit_globobj_files(Fileid f, const Fileidset & (*get_fileid_set)(Fileid fi),
		int level)
{
	if (level == 0)
		return;

	if (DP())
		cout << "Visiting " << f.get_fname() << endl;
	Filedetails::set_visited(f);
	const Fileidset &s = get_fileid_set(f);
	for (Fileidset::const_iterator i = s.begin(); i != s.end(); i++) {
		if (!Filedetails::is_visited(*i))
			visit_globobj_files(*i, get_fileid_set, level - 1);
	}
}

/*
 * Visit all files associated with a function call relationship with f
 * (a control dependency).
 * The methods to obtain the relationship iterators are passed through
 * the abegin and aend method pointers.
 * Set the visited flag for all nodes visited and the edges matrix for
 * the corresponding edges.
 */
void
visit_fcall_files(Fileid f, Call::const_fiterator_type (Call::*abegin)() const, Call::const_fiterator_type (Call::*aend)() const, int level, EdgeMatrix &edges)
{
	if (level == 0)
		return;

	if (DP())
		cout << "Visiting " << f.get_fname() << endl;
	Filedetails::set_visited(f);
	/*
	 * For every function in this file:
	 * for every function associated with this function
	 * set the edge and visit the corresponding files.
	 */
	for (FCallSet::const_iterator filefun = Filedetails::get_functions(f).begin(); filefun != Filedetails::get_functions(f).end(); filefun++) {
		if (!(*filefun)->is_cfun())
			continue;
		for (Call::const_fiterator_type afun = ((*filefun)->*abegin)(); afun != ((*filefun)->*aend)(); afun++)
			if ((*afun)->is_defined() && (*afun)->is_cfun()) {
				Fileid f2((*afun)->get_definition().get_fileid());
				edges[f.get_id()][f2.get_id()] = true;
				if (!Filedetails::is_visited(f2))
					visit_fcall_files(f2, abegin, aend, level - 1, edges);
			}
	}
}


extern "C" { const char *swill_getquerystring(void); }

/*
 * Print a list of callers or called functions for the given function,
 * recursively expanding functions that the user has specified.
 */
void
explore_functions(FILE *fo, Call *f,
	Call::const_fiterator_type (Call::*fbegin)() const,
	Call::const_fiterator_type (Call::*fend)() const,
	int level)
{
	Call::const_fiterator_type i;

	for (i = (f->*fbegin)(); i != (f->*fend)(); i++) {
		fprintf(fo, "<div style=\"margin-left: %dem\">", level * 2);
		if (((*i)->*fbegin)() != ((*i)->*fend)()) {
			/* Functions below; create +/- hyperlink. */
			char param[1024];
			snprintf(param, sizeof(param), "f%02d%p", level, (void *)&(**i));
			char *pval = swill_getvar(param);

			if (pval) {
				// Colapse hyperlink
				string nquery(swill_getquerystring());
				string::size_type start = nquery.find(param);
				if (start != string::npos && start > 0)
					// Erase &param=1 (i.e. param + 3 chars)
					nquery.erase(start - 1, strlen(param) + 3);
				fprintf(fo, "<table class=\"box\"> <tr><th><a class=\"plain\" href=\"%s?%s\">&ndash;</a></th><td>",
				    swill_getvar("__uri__"), nquery.c_str());
			} else
				// Expand hyperlink
				fprintf(fo, "<table class=\"box\"> <tr><th><a class=\"plain\" href=\"%s?%s&%s=1\">+</a></th><td>",
				    swill_getvar("__uri__"),
				    swill_getquerystring(), param);
			html(fo, **i);
			fputs("</td></tr></table></div>\n", fo);
			if (pval && *pval == '1')
				explore_functions(fo, *i, fbegin, fend, level + 1);
		} else {
			/* No functions below. Just display the function. */
			fputs("<table class=\"unbox\"> <tr><th></th><td>", fo);
			html(fo, **i);
			fputs("</td></tr></table></div>\n", fo);
		}
	}
}

// List of functions associated with a given one
int
funlist_page(FILE *fo, void *)
{
	Call *f;
	char buff[256];

	char *ltype = swill_getvar("n");
	if (!swill_getargs("p(f)", &f) || !ltype) {
		fprintf(fo, "Missing value");
		return 0;
	}
	html_head(fo, "funlist", "Function List");
	fprintf(fo, "<h2>Function ");
	html(fo, *f);
	fprintf(fo, "</h2>");
	const char *calltype;
	bool recurse;
	switch (*ltype) {
	case 'u': case 'd':
		calltype = "directly";
		recurse = false;
		break;
	case 'U': case 'D':
		calltype = "all";
		recurse = true;
		break;
	default:
		fprintf(fo, "Illegal value");
		return 0;
	}
	// Pointers to the ...begin and ...end methods
	Call::const_fiterator_type (Call::*fbegin)() const;
	Call::const_fiterator_type (Call::*fend)() const;
	switch (*ltype) {
	default:
	case 'u':
	case 'U':
		fbegin = &Call::caller_begin;
		fend = &Call::caller_end;
		fprintf(fo, "List of %s calling functions\n", calltype);
		snprintf(buff, sizeof(buff), " &mdash; <a href=\"cpath%s?from=%%p&to=%p\">call path from function</a>", graph_suffix(), (void *)f);
		break;
	case 'd':
	case 'D':
		fbegin = &Call::call_begin;
		fend = &Call::call_end;
		fprintf(fo, "List of %s called functions\n", calltype);
		snprintf(buff, sizeof(buff), " &mdash; <a href=\"cpath%s?from=%p&to=%%p\">call path to function</a>", graph_suffix(), (void *)f);
		break;
	}
	if (swill_getvar("e")) {
		fprintf(fo, "<br />\n");
		explore_functions(fo, f, fbegin, fend, 0);
	} else {
		fprintf(fo, "<ul>\n");
		Call::clear_visit_flags();
		visit_functions(fo, buff, f, fbegin, fend, recurse, true, Option::cgraph_depth->get());
		fprintf(fo, "</ul>\n");
	}
	html_tail(fo);
	return 0;
}

// Return the page suffix for the select call graph type
const char *
graph_suffix()
{
	switch (Option::cgraph_type->get()) {
	case 't': return ".txt";
	case 'h': return ".html";
	case 'd': return "_dot.txt";
	case 's': return ".svg";
	case 'g': return ".gif";
	case 'p': return ".png";
	case 'f': return ".pdf";
	}
	return "";
}