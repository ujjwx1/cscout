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
 * HTML utility functions and handler declarations.
 *
 */

#ifndef HTML_
#define HTML_

#include <string>
#include <cstdio>

using namespace std;

#include "fileid.h"
#include "attr.h"
#include "compiledre.h"
#include "tokid.h"
#include "call.h"
#include "idquery.h"
class GraphDisplay;

/* Workspace modification state - tracks whether substitution or
 * hand-edit mode is active, controlling what the web UI permits. */
enum e_modification_state {
	ms_unmodified,		/* Unmodified; can be modified */
	ms_subst,		/* An identifier has been substituted */
	ms_hand_edit		/* A file has been hand-edited */
};

extern enum e_modification_state modification_state;
extern bool browse_only;
extern bool must_exit;
extern Attributes::size_type current_project;

/* Maximum graph elements allowed to browsing-only clients */
#define MAX_BROWSING_GRAPH_ELEMENTS 1000

/* HTML utility functions */
const char *html(char c);
string html(const string &s);
void html_string(FILE *of, string s);
void html_head(FILE *of, const string fname, const string title, const char *heading = NULL);
void html_tail(FILE *of);
void html_perror(FILE *of, const string &user_msg, bool svg = false);
void html_error(FILE *of, const string &user_msg);

string function_label(Call *f, bool hyperlink);
string file_label(Fileid f, bool hyperlink);

/* HTML handler functions - registered with swill_handle() in cscout.cpp */
int index_page(FILE *of, void *data);
int filequery_page(FILE *of, void *);
int xfilequery_page(FILE *of, void *);
int iquery_page(FILE *of, void *);
int funquery_page(FILE *of, void *);
int xiquery_page(FILE *of, void *);
int xfunquery_page(FILE *of, void *);
int identifier_page(FILE *fo, void *);
int function_page(FILE *fo, void *);
int funlist_page(FILE *fo, void *);
int cpath_page(GraphDisplay *gd);
int options_page(FILE *fo, void *);
int set_options_page(FILE *fo, void *p);
int save_options_page(FILE *fo, void *);
int file_metrics_page(FILE *fo, void *);
int function_metrics_page(FILE *fo, void *);
int id_metrics_page(FILE *fo, void *);
int cgraph_page(GraphDisplay *gd);
int fgraph_page(GraphDisplay *gd);
int graph_txt_page(FILE *fo, void (*graph_fun)(GraphDisplay *));
int graph_html_page(FILE *fo, void (*graph_fun)(GraphDisplay *));
int graph_dot_page(FILE *fo, void (*graph_fun)(GraphDisplay *));
int graph_svg_page(FILE *fo, void (*graph_fun)(GraphDisplay *));
int graph_gif_page(FILE *fo, void (*graph_fun)(GraphDisplay *));
int graph_png_page(FILE *fo, void (*graph_fun)(GraphDisplay *));
int graph_pdf_page(FILE *fo, void (*graph_fun)(GraphDisplay *));
int select_project_page(FILE *fo, void *);
int set_project_page(FILE *fo, void *p);
int about_page(FILE *fo, void *);
int file_page(FILE *of, void *);
int source_page(FILE *of, void *);
int fedit_page(FILE *of, void *);
int query_source_page(FILE *of, void *);
int query_include_page(FILE *of, void *);
int logo_page(FILE *fo, void *);
int replacements_page(FILE *of, void *);
int xreplacements_page(FILE *of, void *p);
int funargrefs_page(FILE *of, void *);
int xfunargrefs_page(FILE *of, void *p);
int write_quit_page(FILE *of, void *exit);
int quit_page(FILE *of, void *);
/* HTML rendering helpers - used by handlers in cscout.cpp and html.cpp */
void html(FILE *of, const IdPropElem &i);
void html(FILE *of, const Call &c);
void html_string(FILE *of, const string &s, Tokid t);
void html_string(FILE *of, const Call *f);
void file_hypertext(FILE *of, Fileid fi, bool eval_query);
void change_prohibited(FILE *fo);
void nonbrowse_operation_prohibited(FILE *fo);
void html_file_begin(FILE *of);
void html_file_set_begin(FILE *of);
void html_file_record_end(FILE *of);
void html_file_end(FILE *of);
void html_file(FILE *of, Fileid fi);

#endif /* HTML_ */