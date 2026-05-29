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
 * CLI and web-based interface for viewing and processing C code
 * Important functions: main(), file_analyze(), file_refactor()
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
#include <regex.h> // regex

#include <getopt.h>

#include "swill.h"

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
#include "type.h"
#include "stab.h"
#include "fdep.h"
#include "version.h"
#include "call.h"
#include "fcall.h"
#include "mcall.h"
#include "compiledre.h"
#include "option.h"
#include "query.h"
#include "mquery.h"
#include "idquery.h"
#include "funquery.h"
#include "filequery.h"
#include "logo.h"
#include "pager.h"
#include "html.h"
#include "dirbrowse.h"
#include "fileutils.h"
#include "globobj.h"
#include "fifstream.h"
#include "ctag.h"
#include "timer.h"
#include "dbtoken.h"
#include "macro_arg_processor.h"
#include "rest.h"
#include "options.h"

#ifdef PICO_QL
#include "pico_ql_search.h"
using namespace picoQL;
#endif

#include "sql.h"
#include "workdb.h"
#include "obfuscate.h"

#define ids Identifier::ids




static Fileid input_file_id;

// This uses many of the above, and is therefore included here
#include "gdisplay.h"


CompiledRE sfile_re;			// Saved files replacement location RE


vector <Fileid> files;






// Boundaries of a function argument
struct ArgBound {
	streampos start, end;
};

typedef map <Tokid, vector <ArgBound> > ArgBoundMap;
static ArgBoundMap argbounds_map;

// Keep track of the number of replacements made when saving the files
static int num_id_replacements = 0;
static int num_fun_call_refactorings = 0;


int index_page(FILE *of, void *data);




// Add identifiers of the file fi into ids
// Collect metrics for the file and its functions
// Populate the file's accociated files set
// Return true if the file contains unused identifiers
static bool
file_analyze(Fileid fi)
{
	using namespace std::rel_ops;

	fifstream in;
	bool has_unused = false;
	const string &fname = fi.get_path();
	int line_number = 0;


	FCallSet &fc = Filedetails::get_functions(fi);	// File's functions
	FCallSet::iterator fci = fc.begin();	// Iterator through them
	Call *cfun = NULL;			// Current function
	stack <Call *> fun_nesting;

	if (!CscoutOptions::quiet)
	    cerr << "Post-processing " << fname << endl;
	in.open(fname.c_str(), ios::binary);
	if (in.fail()) {
		perror(fname.c_str());
		exit(1);
	}

	MacroArgProcessor ma_proc;

	// Go through the file character by character
	for (;;) {
		Tokid ti;
		int val;

		ti = Tokid(fi, in.tellg());
		if ((val = in.get()) == EOF)
			break;

		// Update current_function
		if (cfun && ti > cfun->get_end().get_tokid()) {
			cfun->get_pre_cpp_metrics().summarize_identifiers();
			if (cfun->is_cfun())
				cfun->get_pre_cpp_metrics().adjust_cfun_metrics();
			if (fun_nesting.empty())
				cfun = NULL;
			else {
				cfun = fun_nesting.top();
				fun_nesting.pop();
			}
		}
		// See if entering a new function
		if (fci != fc.end() && ti >= (*fci)->get_begin().get_tokid()) {
			if (cfun)
				fun_nesting.push(cfun);
			cfun = *fci;
			fci++;
		}

		char c = (char)val;
		mapTokidEclass::iterator ei;
		enum e_cfile_state cstate = Filedetails::get_pre_cpp_metrics(fi).get_state();

		ma_proc.process_char(cstate, c);

		// Mark identifiers
		if (cstate != s_block_comment &&
		    cstate != s_string &&
		    cstate != s_cpp_comment &&
		    (isalnum(c) || c == '_') &&
		    (ei = ti.find_ec()) != ti.end_ec()) {
			Eclass *ec = (*ei).second;
			// Remove identifiers we are not supposed to CscoutOptions::monitor
			if (CscoutOptions::monitor.is_valid()) {
				IdPropElem ec_id(ec, Identifier());
				if (!CscoutOptions::monitor.eval(ec_id)) {
					ec->remove_from_tokid_map();
					delete ec;
					continue;
				}
			}

			string s(1, c);
			int len = ec->get_len();
			for (int j = 1; j < len; j++)
				s += (char)in.get();

			// Identifiers we can mark
			if (ec->is_identifier()) {
				// Update metrics
				id_msum.add_pre_cpp_id(ec);
				// Add to the map
				Filedetails::get_pre_cpp_metrics(fi).process_identifier(s, ec);
				if (cfun)
					cfun->get_pre_cpp_metrics().process_identifier(s, ec);
				/*
				 * ids[ec] = Identifier(ec, s);
				 * Efficiently add s to ids, if needed.
				 * See Meyers, effective STL, Item 24.
				 */
				IdProp::iterator idi = ids.lower_bound(ec);
				if (idi == ids.end() || idi->first != ec)
					ids.insert(idi, IdProp::value_type(ec, Identifier(ec, s)));
				if (ec->is_unused())
					has_unused = true;
				else {
					; // TODO fi.set_associated_files(ec);
				}
			} else {
				/*
				 * This equivalence class is not needed.
				 * (All potential identifier tokens,
				 * even reserved words get an EC. These are
				 * cleared here.)
				 */
				ec->remove_from_tokid_map();
				delete ec;
				ec = NULL;
			}
			ma_proc.process_ec(ec, s);
		}
		Filedetails::get_pre_cpp_metrics(fi).process_char((char)val);
		if (cfun)
			cfun->get_pre_cpp_metrics().process_char((char)val);
		if (c == '\n') {
			Filedetails::add_line_end(fi, ti.get_streampos());
			if (!Filedetails::is_line_processed(fi, ++line_number))
				Filedetails::get_pre_cpp_metrics(fi).add_unprocessed();
		}
	}
	if (cfun) {
		cfun->get_pre_cpp_metrics().summarize_identifiers();
		if (cfun->is_cfun())
			cfun->get_pre_cpp_metrics().adjust_cfun_metrics();
	}
	Filedetails::get_pre_cpp_metrics(fi).summarize_identifiers();
	Filedetails::get_pre_cpp_metrics(fi).set_ncopies(Filedetails::get_identical_files(fi).size());
	if (DP())
		cout << "nchar = " << Filedetails::get_pre_cpp_metrics(fi).get_metric(Metrics::em_nchar) << endl;
	in.close();
	return has_unused;
}


// Set the function argument boundaries for refactored
// function calls for the specified file
static void
establish_argument_boundaries(const string &fname)
{
	Pltoken::set_context(cpp_normal);
	Fchar::set_input(fname);

	for (;;) {
		Pltoken t;
again:
		t.getnext<Fchar>();
		if (t.get_code() == EOF)
			break;

		Tokid ti;
		Eclass *ec;
		RefFunCall::store_type::const_iterator rfc;
		if (t.get_code() == IDENTIFIER &&
		    (ti = t.get_parts_begin()->get_tokid(), ec = ti.check_ec()) &&
		    (rfc = RefFunCall::store.find(ec)) != RefFunCall::store.end() &&
		    rfc->second.is_active() &&
		    (int)t.get_val().length() == ec->get_len()) {
			Tokid call = t.get_parts_begin()->get_tokid();
			// Gather args
			FcharContext fc = Fchar::get_context();		// Save position to scan for another function
			// Move just before the first arg
			for (;;) {
				t.getnext<Fchar>();
				if (t.get_code() == EOF) {
					/*
					 * @error
					 * End of file encountered while scanning the opening
					 * bracket in a refactored function call
					 */
					Error::error(E_WARN, "Missing open bracket in refactored function call");
					break;
				}
				if (t.get_code() == '(')
					break;
				if (!isspace(t.get_code())) {
					Fchar::set_context(fc);		// Restore saved position
					goto again;
				}
			}
			// Gather the boundaries of all arguments of a single function
			vector <ArgBound> abv;
			for (;;) {
				ArgBound ab;
				// Set position of argument's first token part the delimiter read
				ab.start = t.get_delimiter_tokid().get_streampos();
				ab.start += 1;
				if (DP())
					cerr << "arg.start: " << ab.start << endl;
				t.getnext<Fchar>();	// We just read the delimiter; move past it
				int bracket = 0;
				// Scan to argument's end
				for (;;) {
					if (bracket == 0 && (t.get_code() == ',' || t.get_code() == ')')) {
						// End of arg
						ab.end = t.get_delimiter_tokid().get_streampos();
						abv.push_back(ab);
						if (DP())
							cerr << "arg.end: " << ab.end << endl;
						if (t.get_code() == ')') {
							// Done with this call
							argbounds_map.insert(pair<ArgBoundMap::key_type, ArgBoundMap::mapped_type>(call, abv));
							if (DP())
								cerr << "Finish function" << endl;
							Fchar::set_context(fc);		// Restore saved position
							goto again;			// Scan again from the token following the function's name
						}
						break;	// Next argument
					}
					switch (t.get_code()) {
					case '(':
						bracket++;
						break;
					case ')':
						bracket--;
						break;
					case EOF:
						/*
						 * @error
						 * The end of file was reached while
						 * gathering a refactored function call's arguments
						 */
						Error::error(E_WARN, "EOF while reading refactored function call arguments");
						Fchar::set_context(fc);
						goto again;
					}
					t.getnext<Fchar>();
				}
			}
			/* NOTREACHED */
		} // If refactored function call
	} // For all the file's tokens
}

// Trim whitespace at the left of the string
static string
ltrim(const string &s)
{
	string::const_iterator i;

	for (i = s.begin(); i != s.end(); i++)
		if (!isspace(*i))
			break;
	return string(i, s.end());
}

/*
 * Return (through the out params n and op) the value of
 * an @ template replacement operator and the corresponding modifier.
 * Update i to point to the first character after the @ sequence.
 * For conditional modifiers (+ and -) set b2 and e2 to the enclosed text,
 * otherwise set the two to point to end.
 * Return true if the operator's syntax is correct, false if not.
 * Set error to the corresponding error message.
 */
static bool
parse_function_call_replacement(string::const_iterator &i, string::const_iterator end, vector<string>::size_type &n,
    char &mod, string::const_iterator &b2, string::const_iterator &e2, const char **error)
{
	if (DP())
		cerr << "Scan replacement \"" << string(i, end) << '"' << endl;
	csassert(*i == '@');
	if (++i == end) {
		*error = "Missing argument to @";
		return false;
	}
	switch (*i) {
	case '.':
	case '+':
	case '-':
		mod = *i++;
		break;
	default:
		mod = '=';
		break;
	}
	int val, nchar;
	if (DP())
		cerr << "Scan number \"" << string(i, end) << '"' << endl;
	int nconv = sscanf(string(i, end).c_str(), "%d%n", &val, &nchar);
	if (nconv != 1 || val <= 0) {
		*error = "Invalid numerical value to @";
		return false;
	}
	i += nchar;
	n = (unsigned)val;
	if (mod == '+' || mod == '-') {
		// Set b2, e2 to the limits argument in braces and update i past them
		if (*i != '{') {
			*error = "Missing opening brace";
			return false;
		}
		b2 = i + 1;
		int nbrace = 0;
		for (; i != end; i++) {
			if (*i == '{') nbrace++;
			if (*i == '}') nbrace--;
			if (nbrace == 0)
				break;
		}
		if (i == end) {
			*error = "Non-terminated argument in braces (missing closing brace)";
			return false;
		} else
			e2 = i++;
		if (DP())
			cerr << "Enclosed range: \"" << string(b2, e2) << '"' << endl;
	} else
		b2 = e2 = end;
	if (DP())
		cerr << "nchar= " << nchar << " val=" << n << " remain: \"" << string(i, end) << '"' << endl;
	csassert(i <= end);
	*error = "No error";
	return true;
}


/*
 * Return true if a function call substitution string in the range is valid
 * Set error to the corresponding error value
 */
bool
is_function_call_replacement_valid(string::const_iterator begin, string::const_iterator end, const char **error)
{
	if (DP())
		cerr << "Call valid for \"" << string(begin, end) << '"' << endl;
	for (string::const_iterator i = begin; i != end;)
		if (*i == '@') {
			vector<string>::size_type n;
			char modifier;
			string::const_iterator b2, e2;
			if (!parse_function_call_replacement(i, end, n, modifier, b2, e2, error))
				return false;
			if ((b2 != e2) && !is_function_call_replacement_valid(b2, e2, error))
			    	return false;
		} else
			i++;
	return true;
}

/*
 * Set arguments in the new order as specified by the template begin..end
 * @N  : use argument N
 * @.N : use argument N and append a comma-separated list of all arguments following N
 * @+N{...} : if argument N exists, substitute text in braces
 * @-N{...} : if argument N doesn't exist, substitute text in braces
 */
string
function_argument_replace(string::const_iterator begin, string::const_iterator end, const vector <string> &args)
{
	string ret;

	for (string::const_iterator i = begin; i != end;)
		if (*i == '@') {
			vector<string>::size_type n;
			char modifier = '=';
			string::const_iterator b2, e2;
			const char *error;
			bool valid = parse_function_call_replacement(i, end, n, modifier, b2, e2, &error);
			csassert(valid);
			switch (modifier) {
			case '.':	// Append comma-separated varargs
				if (n <= args.size())
					ret += ltrim(args[n - 1]);
				for (vector<string>::size_type j = n; j < args.size(); j++) {
					ret += ", ";
					ret += ltrim(args[j]);
				}
				break;
			case '+':	// if argument N exists, substitute text in braces
				if (n <= args.size())
					ret += function_argument_replace(b2, e2, args);
				break;
			case '-':	// if argument N doesn't exist, substitute text in braces
				if (n > args.size())
					ret += function_argument_replace(b2, e2, args);
				break;
			case '=':	// Exact argument
				if (n <= args.size())
					ret += ltrim(args[n - 1]);
				break;
			}
		} else
			ret += *i++;
	return ret;
}

/*
 * Return the smallest part of the file that can be chunked
 * without renaming or having to reorder function arguments.
 * Otherwise, return the part suitably renamed and with the
 * function arguments reordered.
 */
static string
get_refactored_part(fifstream &in, Fileid fid)
{
	Tokid ti;
	int val;
	string ret;

	ti = Tokid(fid, in.tellg());
	if ((val = in.get()) == EOF)
		return ret;
	Eclass *ec;

	// Identifiers that should be replaced
	IdProp::const_iterator idi;
	if ((ec = ti.check_ec()) &&
	    ec->is_identifier() &&
	    (idi = ids.find(ec)) != ids.end() &&
	    idi->second.get_replaced() &&
	    idi->second.get_active()) {
		int len = ec->get_len();
		for (int j = 1; j < len; j++)
			(void)in.get();
		ret += (*idi).second.get_newid();
		num_id_replacements++;
	} else
		ret = (char)val;

	// Functions whose arguments need reordering
	RefFunCall::store_type::const_iterator rfc;
	ArgBoundMap::const_iterator abi;
	if ((ec = ti.check_ec()) &&
	    ec->is_identifier() &&
	    (rfc = RefFunCall::store.find(ec)) != RefFunCall::store.end() &&
	    rfc->second.is_active() &&
	    (abi = argbounds_map.find(ti)) != argbounds_map.end()) {
		const ArgBoundMap::mapped_type &argbounds = abi->second;
		csassert (in.tellg() < argbounds[0].start);
		// Gather material until first argument
		while (in.tellg() < argbounds[0].start)
			ret += get_refactored_part(in, fid);
		// Gather arguments
		vector<string> arg(argbounds.size());
		for (ArgBoundMap::mapped_type::size_type i = 0; i < argbounds.size(); i++) {
			while (in.tellg() < argbounds[i].end)
				arg[i] += get_refactored_part(in, fid);
			int endchar = in.get();
			if (DP())
			cerr << "arg[" << i << "] = \"" << arg[i] << "\" endchar: '" << (char)endchar << '\'' << endl;
			csassert ((i == argbounds.size() - 1 && endchar == ')') ||
			    (i < argbounds.size() - 1 && endchar == ','));
		}
		ret += function_argument_replace(rfc->second.get_replacement().begin(), rfc->second.get_replacement().end(), arg);
		ret += ')';
		num_fun_call_refactorings++;
	} // Replaced function call
	return ret;
}

// Go through the file doing any refactorings needed
static void
file_refactor(FILE *of, Fileid fid)
{
	string plain;
	fifstream in;
	ofstream out;

	if (!CscoutOptions::quiet)
	    cerr << "Processing file " << fid.get_path() << endl;

	if (RefFunCall::store.size())
		establish_argument_boundaries(fid.get_path());
	in.open(fid.get_path().c_str(), ios::binary);
	if (in.fail()) {
		html_perror(of, "Unable to open " + fid.get_path() + " for reading");
		return;
	}
	string ofname(fid.get_path() + ".repl");
	out.open(ofname.c_str(), ios::binary);
	if (out.fail()) {
		html_perror(of, "Unable to open " + ofname + " for writing");
		return;
	}

	while (!in.eof())
		out << get_refactored_part(in, fid);
	argbounds_map.clear();

	// Needed for Windows
	in.close();
	out.close();

	if (Option::sfile_re_string->get().length()) {
		regmatch_t be;
		if (sfile_re.exec(fid.get_path().c_str(), 1, &be, 0) == REG_NOMATCH ||
		    be.rm_so == -1 || be.rm_eo == -1)
			fprintf(of, "File %s does not match file replacement RE."
				"Replacements will be saved in %s.repl.<br>\n",
				ofname.c_str(), ofname.c_str());
		else {
			string newname(fid.get_path().c_str());
			newname.replace(be.rm_so, be.rm_eo - be.rm_so, Option::sfile_repl_string->get());
			string cmd("cscout_checkout " + newname);
			if (system(cmd.c_str()) != 0) {
				html_error(of, "Changes are saved in " + ofname + ", because executing the checkout command cscout_checkout failed");
				return;
			}
			if (unlink(newname) < 0) {
				html_perror(of, "Changes are saved in " + ofname + ", because deleting the target file " + newname + " failed");
				return;
			}
			if (rename(ofname.c_str(), newname.c_str()) < 0) {
				html_perror(of, "Changes are saved in " + ofname + ", because renaming the file " + ofname + " to " + newname + " failed");
				return;
			}
			string cmd2("cscout_checkin " + newname);
			if (system(cmd2.c_str()) != 0) {
				html_error(of, "Checking in the file " + newname + " failed");
				return;
			}
		}
	} else {
		string cmd("cscout_checkout " + fid.get_path());
		if (system(cmd.c_str()) != 0) {
			html_error(of, "Changes are saved in " + ofname + ", because checking out " + fid.get_path() + " failed");
			return;
		}
		if (unlink(fid.get_path()) < 0) {
			html_perror(of, "Changes are saved in " + ofname + ", because deleting the target file " + fid.get_path() + " failed");
			return;
		}
		if (rename(ofname.c_str(), fid.get_path().c_str()) < 0) {
			html_perror(of, "Changes are saved in " + ofname + ", because renaming the file " + ofname + " to " + fid.get_path() + " failed");
			return;
		}
		string cmd2("cscout_checkin " + fid.get_path());
		if (system(cmd2.c_str()) != 0) {
			html_error(of, "Checking in the file " + fid.get_path() + " failed");
			return;
		}
	}
	return;
}

int
write_quit_page(FILE *of, void *exit)
{
	prohibit_browsers(of);
	prohibit_remote_access(of);

	if (exit)
		html_head(of, "quit", "CScout exiting");
	else {
		if (Option::sfile_re_string->get().length() == 0) {
			html_head(of, "save", "Not Allowed");
			fputs("This in-place save and continue operation is not allowed, "
			"because it may corrupt CScout's idea of the source code.  "
			"Either set the filename substitution rule option, "
			"or select the save and exit operation.", of);
			html_tail(of);
			return 0;
		}
		html_head(of, "save", "Saving changes");
	}

	// Determine files we need to process
	IFSet process;
	if (!CscoutOptions::quiet)
	    cerr << "Examining identifiers for renaming" << endl;
	for (IdProp::iterator i = Identifier::ids.begin(); i != Identifier::ids.end(); i++) {
		progress(i, Identifier::ids);
		if (i->second.get_replaced() && i->second.get_active()) {
			Eclass *e = (*i).first;
			IFSet ifiles = e->sorted_files();
			process.insert(ifiles.begin(), ifiles.end());
		}
	}
	if (!CscoutOptions::quiet)
	    cerr << endl;

	// Check for identifier clashes
	Token::found_clashes = false;
	if (Option::refactor_check_clashes->get() && process.size()) {
		if (!CscoutOptions::quiet)
		    cerr << "Checking rename refactorings for name clashes." << endl;
		Token::check_clashes = true;
		// Reparse everything
		Fchar::set_input(input_file_id.get_path());
		Error::set_parsing(true);
		Pdtoken t;
		do
			t.getnext();
		while (t.get_code() != EOF);
		Error::set_parsing(false);
		Token::check_clashes = false;
	}
	if (Token::found_clashes) {
		fprintf(of, "Renamed identifier clashes detected. Errors reported on console output. No files were saved.");
		html_tail(of);
		return 0;
	}

	if (!CscoutOptions::quiet) 
	    cerr << "Examining function calls for refactoring" << endl;
	for (RefFunCall::store_type::iterator i = RefFunCall::store.begin(); i != RefFunCall::store.end(); i++) {
		progress(i, RefFunCall::store);
		if (!i->second.is_active())
			continue;
		Eclass *e = i->first;
		IFSet ifiles = e->sorted_files();
		process.insert(ifiles.begin(), ifiles.end());
	}
	if (!CscoutOptions::quiet)
	    cerr << endl;

	// Now do the replacements
	if (!CscoutOptions::quiet)
	    cerr << "Processing files" << endl;
	for (IFSet::const_iterator i = process.begin(); i != process.end(); i++)
		file_refactor(of, *i);
	fprintf(of, "A total of %d replacements and %d function call refactorings were made in %d files.",
	    num_id_replacements, num_fun_call_refactorings, (unsigned)(process.size()));
	if (exit) {
		fprintf(of, "<p>Bye...</body></html>");
		must_exit = true;
	} else
		html_tail(of);
	return 0;
}

// Load the CScout options.
static void
options_load()
{
	ifstream in;
	string fname;

	if (!cscout_input_file("options", in, fname)) {
		fprintf(stderr, "No options file found; will use default options.\n");
		return;
	}
	Option::load_all(in);
	if (Option::sfile_re_string->get().length()) {
		sfile_re = CompiledRE(Option::sfile_re_string->get().c_str(), REG_EXTENDED);
		if (!sfile_re.isCorrect()) {
			fprintf(stderr, "Filename regular expression error: %s", sfile_re.getError().c_str());
			Option::sfile_re_string->erase();
		}
	}
	in.close();
	fprintf(stderr, "Options loaded from %s\n", fname.c_str());
}


// Split a string by delimiter
vector<string> split_by_delimiter(string &s, char delim) {
	string buf;                 // Have a buffer string
	stringstream ss(s);       // Insert the string into a stream

	vector<string> tokens; // Create vector to hold our words

	while (getline(ss, buf, delim))
		tokens.push_back(buf);

	return tokens;
}

// Return version information
string
version_info(bool html)
{
	ostringstream v;

	string end = html ? "<br />" : "\n";
	string fold = html ? " " : "\n";

	v << "CScout version " <<
	Version::get_revision() << " - " <<
	Version::get_date() << end << end <<
	// 80 column terminal width---------------------------------------------------
	"(c) Copyright 2003-" << ((char *)__DATE__ + string(__DATE__).length() - 4) <<
				 // Current year
	" Diomidis Spinelllis." << end <<
	end <<
	// C grammar
	"Portions Copyright (c) 1989, 1990 James A. Roskind." << end <<
	// MD-5
	"Portions derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm." << end <<

	"Includes the SWILL (Simple Web Interface Link Library) library written by David" << fold <<
	"Beazley and Sotiria Lampoudi.  Copyright (c) 1998-2002 University of Chicago." << fold <<
	"SWILL is distributed under the terms of the GNU Lesser General Public License" << fold <<
	"version 2.1 available " <<
	(html ? "<a href=\"http://www.gnu.org/licenses/lgpl-2.1.html\">online</a>." : "online at http://www.gnu.org/licenses/lgpl-2.1.html.") << end <<

	end <<
	"CScout is distributed as open source software under the GNU" << fold <<
	"General Public License, available in the CScout documentation and ";
	if (html)
		v << "<a href=\"http://www.gnu.org/licenses/\">online</a>.";
	else
		v << "online at" << end <<
		"http://www.gnu.org/licenses/." << end;
	v << "Other licensing options and professional support are available"
		" on request." << end;
	return v.str();
}

// Produce call graphs with -R option
static void
produce_call_graphs(const vector <string> &call_graphs)
{
	char base_splitter = '?';
	char opts_splitter = '&';
	char opt_spltter = '=';
	GDArgsKeys gdargskeys;

	for (string url: call_graphs) {
		vector<string> split_base_and_opts = split_by_delimiter(url, base_splitter);
		if (split_base_and_opts.size() == 0) {
			cerr << url << "is not a valid url" << endl;
			continue;
		}

		FILE *target = fopen(split_base_and_opts[0].c_str() , "w+");
		if (target == NULL) {
			perror(split_base_and_opts[0].c_str());
			continue;
		}
		string base = split_base_and_opts[0];
		GDTxt gd(target);
		// Disable swill
		gd.uses_swill = false;
		vector<string> opts;

		if (split_base_and_opts.size() != 1) {

			opts = split_by_delimiter(split_base_and_opts[1], opts_splitter);

			// Parse opts
			for (string opt: opts) {
				vector<string> opt_tmp = split_by_delimiter(opt, opt_spltter);
				if (opt_tmp.size() < 2) continue;

				// Key-value pairs
				string key = opt_tmp[0];
				string val = opt_tmp[1];

				if (!key.compare(gdargskeys.ALL)) {
					gd.all = (bool) atoi(val.c_str());
				} else if (!key.compare(gdargskeys.ONLY_VISITED)) {
					gd.only_visited = (bool) atoi(val.c_str());
				} else if (!key.compare(gdargskeys.GTYPE)) {
					gd.gtype = val;
				} else if (!key.compare(gdargskeys.LTYPE)) {
					gd.ltype = val;
					Option::cgraph_show->set_hard(val.c_str());
				} else if (!key.compare("type")) {
					Option::show_function_type->set_hard((bool) atoi(val.c_str()));
				} else if (!key.compare("defined")) {
					Option::is_defined->set_hard((bool) atoi(val.c_str()));
				} else if (!key.compare("nline")) {
					Option::line_num->set_hard((bool) atoi(val.c_str()));
				} else if (!key.compare("nodes")) {
					Option::print_nodes->set_hard((bool) atoi(val.c_str()));
				}

			}

		}

		if (!base.compare(gdargskeys.CGRAPH)) {
			cgraph_page(&gd);
		}
		else if (!base.compare(gdargskeys.FGRAPH)) {
			fgraph_page(&gd);
		}

		fclose(target);
	}



}




// Parse the access control list acl.
static void
parse_acl()
{

	ifstream in;
	string ad, host;
	string fname;

	if (cscout_input_file("acl", in, fname)) {
		cerr << "Parsing ACL from " << fname << endl;
		for (;;) {
			in >> ad;
			if (in.eof())
				break;
			in >> host;
			if (ad == "A") {
				cerr << "Allow from IP address " << host << endl;
				swill_allow(host.c_str());
			} else if (ad == "D") {
				cerr << "Deny from IP address " << host << endl;
				swill_deny(host.c_str());
			} else
				cerr << "Bad ACL specification " << ad << ' ' << host << endl;
		}
		in.close();
	} else {
		cerr << "No ACL found.  Only localhost access will be allowed." << endl;
		swill_allow("127.0.0.1");
	}
}

// Included file site information
// See warning_report
class SiteInfo {
private:
	bool required;		// True if this site contains at least one required include file
	set <Fileid> files;	// Files included here
public:
	SiteInfo(bool r, Fileid f) : required(r) {
		files.insert(f);
	}
	void update(bool r, Fileid f) {
		required |= r;
		files.insert(f);
	}
	const set <Fileid> & get_files() const { return files; }
	bool is_required() const { return required; }
};

// Generate a warning report
static void
warning_report()
{
	struct {
		const char *message;
		const char *query;
	} reports[] = {
		{ "unused project scoped writable identifier",
		  "L:writable:unused:pscope" },
		{ "unused file scoped writable identifier",
		  "L:writable:unused:fscope" },
		{ "unused writable macro",
		  "L:writable:unused:macro" },
		{ "writable identifier should be made static",
		  "T:writable:obj:pscope" }, // xfile is implicitly 0
	};

	// Generate identifier warnings
	for (unsigned i = 0; i < sizeof(reports) / sizeof(reports[0]); i++) {
		IdQuery query(reports[i].query);

		csassert(query.is_valid());
		for (IdProp::iterator j = ids.begin(); j != ids.end(); j++) {
			if (!query.eval(*j))
				continue;
			const Tokid t = *((*j).first->get_members().begin());
			const string &id = (*j).second.get_id();
			cerr << t.get_path() << ':' <<
				Filedetails::get_line_number(t.get_fileid(), t.get_streampos()) << ": " <<
				id << ": " << reports[i].message << endl;
		}
	}

	/*
	 * Generate unneeded include file warnings
	 * The hard work has already been done by Fdep::mark_required()
	 * Here we do some additional processing, because
	 * a given include directive can include different files on different
	 * compilations (through different include paths or macros)
	 * Therefore maintain a map for include directive site information:
	 */

	typedef map <int, SiteInfo> Sites;
	Sites include_sites;

	for (vector <Fileid>::iterator i = files.begin(); i != files.end(); i++) {
		if (i->get_readonly() ||		// Don't report on RO files
		    !Filedetails::is_compilation_unit(*i) ||		// Algorithm only works for CUs
		    *i == input_file_id ||		// Don't report on main file
		    Filedetails::get_includers(*i).size() > 1)	// For files that are both CUs and included
							// by others all bets are off
			continue;
		const FileIncMap &m = Filedetails::get_includes(*i);
		// Find the status of our include sites
		include_sites.clear();
		for (FileIncMap::const_iterator j = m.begin(); j != m.end(); j++) {
			Fileid f2 = (*j).first;
			const IncDetails &id = (*j).second;
			if (!id.is_directly_included())
				continue;
			const set <int> &lines = id.include_line_numbers();
			for (set <int>::const_iterator k = lines.begin(); k != lines.end(); k++) {
				Sites::iterator si = include_sites.find(*k);
				if (si == include_sites.end())
					include_sites.insert(Sites::value_type(*k, SiteInfo(id.is_required(), f2)));
				else
					(*si).second.update(id.is_required(), f2);
			}
		}
		// And report those containing unused files
		Sites::const_iterator si;
		for (si = include_sites.begin(); si != include_sites.end(); si++)
			if (!(*si).second.is_required()) {
				const set <Fileid> &sf = (*si).second.get_files();
				int line = (*si).first;
				for (set <Fileid>::const_iterator fi = sf.begin(); fi != sf.end(); fi++)
					cerr << i->get_path() << ':' <<
						line << ": " <<
						"(" << Filedetails::get_pre_cpp_const_metrics(*i).get_int_metric(Metrics::em_nuline) << " unprocessed lines)"
						" unused included file " <<
						fi->get_path() <<
						endl;
			}
	}
}

// Report usage information and exit
static void
usage(char *fname)
{
	cerr << "usage: " << fname <<
		" ["
#ifndef WIN32
		"-b|"	// browse-only
#endif
		"-C|-c|-d D|-d H|-E RE|-o|-M files|"
		"-q|-R URL|-r|-S db|-s db|-v] "
		"[-l file] "

#ifdef PICO_QL
#define PICO_QL_OPTIONS "q"
		"-q|"
#else
#define PICO_QL_OPTIONS ""
#endif

		"[-P RE] [-p port] [-m spec] [-t table ...] file\n"
#ifndef WIN32
		"\t-b\tRun in multiuser browse-only mode\n"
#endif
		"\t-C\tCreate a ctags(1)-compatible tags file\n"
		"\t-c\tProcess the file and exit\n"
		"\t-R URL\tOutput the call graphs specified by the URLs exit\n"
		"\t-d D\tOutput the #defines being processed\n"
		"\t-d H\tOutput the names of included files being processed\n"
		"\t-E RE\tOutput preprocessed results and exit\n"
		"\t\t(Will process file(s) matched by the regular expression)\n"
		"\t-l file\tSpecify access log file\n"
		"\t-M files\tMerge specified EC files\n"
		"\t-m spec\tSpecify identifiers to CscoutOptions::monitor (unsound)\n"
		"\t-o\tCreate obfuscated versions of the processed files\n"
		"\t-P RE\tProcess only file(s) matched by the regular expression\n"
		"\t-p port\tSpecify TCP port for serving the CScout web pages\n"
		"\t\t(the port number must be in the range 1024-32767)\n"
#ifdef PICO_QL
		"\t-q\tProvide a PiCO_QL query interface\n"
#else
		"\t-q\tSuppress progress messages on standard error\n"
#endif
		"\t-r\tGenerate an identifier and include file warning report\n"
		"\t-S db\tGenerate the SQL schema for the specified RDBMS\n"
		"\t-s db\tGenerate SQL output for the specified RDBMS\n"
		"\t-t table\tEnable population of the specified RDBMS table\n"
		"\t\t(All enabled by default. Option can be provided multiple times)\n"
		"\t-v\tDisplay version and copyright information and exit\n"
		"\t-3\tEnable the handling of trigraph characters\n"
		;
	exit(1);
}


// Return a compiled RE for the string s, verifying its correctness
static CompiledRE
verified_compiled_re(const char *s)
{
	CompiledRE pre(s, REG_EXTENDED | REG_NOSUB);

	if (!pre.isCorrect()) {
		cerr << "Filename regular expression error:" <<
			pre.getError() << '\n';
		exit(1);
	}
	return pre;
}

/*
 * Read files with tokens classes and identifier attributes
 * and merge them together
 */
static void
merge_tokens(char **argv)
{
	// Skip over cscout -M
	argv += 2;

	// Files in the order they appear in argv
	enum arg_files {
		in_eclasses_attached,
		in_eclasses_original,
		in_ids,
		in_functionids_attached,
		in_functionids_original,
		in_idproj,
		new_eclasses,
		new_ids,
		new_functionids,
		new_idproj,
		new_functionid_to_global_map,
	};

	/*
	 * Example invocation:
	 * cscout -M \
	 *   eclasses-a-5.txt \
	 *   eclasses-o-5.txt \
	 *   ids-5.txt \
	 *   functionids-a-5.txt \
	 *   functionids-o-5.txt \
	 *   idproj-5.txt \
	 *   new-eclasses-5.csv \
	 *   new-ids-5.csv \
	 *   new-functionds-5.csv
	 *   new-idproj-5.csv
	 *   new-functionid-to-global-map.csv
	 */
	Dbtoken::add_eclasses_attached(argv[in_eclasses_attached]);
	Dbtoken::process_eclasses_original(argv[in_eclasses_original]);
	Dbtoken::read_write_functionids(
	    argv[in_functionids_attached],
	    argv[in_functionids_original],
	    argv[new_functionids],
	    argv[new_functionid_to_global_map]);
	Dbtoken::write_eclasses(argv[new_eclasses]);
	Dbtoken::read_ids(argv[in_ids]);
	Dbtoken::write_ids(argv[in_ids], argv[new_ids]);
	Dbtoken::read_write_idproj(argv[in_idproj], argv[new_idproj]);

	exit(0);
}


int
main(int argc, char *argv[])
{
	Pdtoken t;
	int c;
	CompiledRE pre;
#ifdef PICO_QL
	bool pico_ql = false;
#endif

	vector<string> call_graphs;
	Debug::db_read();

	while ((c = getopt(argc, argv, "3bCcd:rvE:P:p:Mm:l:oR:S:s:t:q" PICO_QL_OPTIONS)) != EOF)  //added q for CscoutOptions::quiet
		switch (c) {
		case '3':
			Fchar::enable_trigraphs();
			break;
		case 'E':
			if (!optarg || CscoutOptions::process_mode)
				usage(argv[0]);
			// Preprocess the specified file
			Pdtoken::set_preprocessed_output(verified_compiled_re(optarg));
			CscoutOptions::process_mode = pm_preprocess;
			break;
		case 'C':
			CTag::enable();
			break;
		#ifdef PICO_QL
		case 'q':
			pico_ql = true;
			/* FALLTHROUGH */
		#endif
		case 'c':
			if (CscoutOptions::process_mode)
				usage(argv[0]);
			CscoutOptions::process_mode = pm_compile;
			break;
		case 'd':
			if (!optarg)
				usage(argv[0]);
			switch (*optarg) {
			case 'D':	// Similar to gcc -dD
				Pdtoken::set_output_defines();
				break;
			case 'H':	// Similar to gcc -H
				Fchar::set_output_headers();
				break;
			default:
				usage(argv[0]);
			}
			break;
		case 'p':
			if (!optarg)
				usage(argv[0]);
			CscoutOptions::portno = atoi(optarg);
			if (CscoutOptions::portno < 1024 || CscoutOptions::portno > 32767)
				usage(argv[0]);
			break;
		case 'M':
			merge_tokens(argv);
			break;
		case 'm':
			if (!optarg)
				usage(argv[0]);
			CscoutOptions::monitor = IdQuery(optarg);
			break;
		case 'r':
			if (CscoutOptions::process_mode)
				usage(argv[0]);
			CscoutOptions::process_mode = pm_report;
			break;
		case 'v':
			cout << version_info(false);
			exit(0);
		case 'b':
			browse_only = true;
			break;
		case 'q':
			CscoutOptions::quiet = true;
			break;
		case 'l':
			if (!optarg)
				usage(argv[0]);
			FILE *logfile;
			if ((logfile = fopen(optarg, "a")) == NULL) {
				perror(optarg);
				exit(1);
			}
			swill_log(logfile);
			break;
		case 'o':
			if (CscoutOptions::process_mode)
				usage(argv[0]);
			CscoutOptions::process_mode = pm_obfuscation;
			break;
		case 'P':
			if (!optarg)
				usage(argv[0]);
			// Process the specified file(s)
			Pdtoken::set_processed_files(verified_compiled_re(optarg));
			break;
		case 'S':
			if (CscoutOptions::process_mode)
				usage(argv[0]);
			if (!optarg)
				usage(argv[0]);
			CscoutOptions::db_engine = optarg;
			if (!Sql::setEngine(optarg))
				return 1;
			workdb_schema(Sql::getInterface(), cout);
			exit(0);
		case 's':
			if (CscoutOptions::process_mode)
				usage(argv[0]);
			if (!optarg)
				usage(argv[0]);
			CscoutOptions::process_mode = pm_database;
			CscoutOptions::db_engine = optarg;
			break;
		case 't':
			if (!optarg)
				usage(argv[0]);
			table_enable(optarg);
			break;
		case 'R':
			if (!optarg)
				usage(argv[0]);
			CscoutOptions::process_mode = pm_call_graph;
			call_graphs.push_back(string(optarg));
			break;
		case '?':
			usage(argv[0]);
		}


	// We require exactly one argument
	if (argv[optind] == NULL || argv[optind + 1] != NULL)
		usage(argv[0]);

	if (CscoutOptions::process_mode != pm_compile
	    && CscoutOptions::process_mode != pm_database
	    && CscoutOptions::process_mode != pm_obfuscation
	    && CscoutOptions::process_mode != pm_preprocess) {
		if (!swill_init(CscoutOptions::portno)) {
			cerr << "Couldn't initialize our web server on port " << CscoutOptions::portno << endl;
			exit(1);
		}

		Option::initialize();
		options_load();
		parse_acl();
	}

	if (CscoutOptions::process_mode == pm_database) {
		if (!Sql::setEngine(CscoutOptions::db_engine))
			return 1;
		cout << Sql::getInterface()->begin_commands();
		workdb_schema(Sql::getInterface(), cout);
	}

	Project::set_current_project("unspecified");

	// Set the contents of the master file as immutable
	Fileid fi = Fileid(argv[optind]);
	fi.set_readonly(true);

	// Pass 1: process master file loop
	Fchar::set_input(argv[optind]);
	Error::set_parsing(true);
	do
		t.getnext();
	while (t.get_code() != EOF);
	Error::set_parsing(false);

	if (CscoutOptions::process_mode == pm_preprocess)
		return 0;

	input_file_id = Fileid(argv[optind]);

	Filedetails::unify_identical_files();

	if (CscoutOptions::process_mode == pm_obfuscation)
		return obfuscate();

	// Pass 2: Create web pages
	files = Fileid::files(true);



	if (CscoutOptions::process_mode != pm_compile) {
		swill_handle("sproject.html", select_project_page, 0);
		swill_handle("replacements.html", replacements_page, 0);
		swill_handle("xreplacements.html", xreplacements_page, NULL);
		swill_handle("funargrefs.html", funargrefs_page, 0);
		swill_handle("xfunargrefs.html", xfunargrefs_page, NULL);
		swill_handle("options.html", options_page, 0);
		swill_handle("soptions.html", set_options_page, 0);
		swill_handle("save_options.html", save_options_page, 0);
		swill_handle("sexit.html", write_quit_page, "exit");
		swill_handle("save.html", write_quit_page, 0);
		swill_handle("qexit.html", quit_page, 0);
	}

	/*
	 * Populate the EC identifier member and the directory tree.
	 * Set several file and function metrics.
	 */
	Call::populate_macro_map();
	for (vector <Fileid>::iterator i = files.begin(); i != files.end(); i++) {
		file_analyze(*i);
		dir_add_file(*i);
	}

	// Update file and function metrics
	file_msum.summarize_files();
	fun_msum.summarize_functions();

	// Set runtime file dependencies
	GlobObj::set_file_dependencies();

	// Set xfile and  metrics for each identifier
	if (!CscoutOptions::quiet)
	    cerr << "Processing identifiers" << endl;
	for (IdProp::iterator i = ids.begin(); i != ids.end(); i++) {
		progress(i, ids);
		Eclass *e = (*i).first;
		IFSet ifiles = e->sorted_files();
		(*i).second.set_xfile(ifiles.size() > 1);
		// Update metrics
		id_msum.add_unique_id(e);
	}
	if (!CscoutOptions::quiet)
	    cerr << endl;

	if (DP())
		cout << "Size " << file_msum.get_pre_cpp_total(Metrics::em_nchar) << endl;

	if (CscoutOptions::process_mode == pm_database) {
		workdb_rest(Sql::getInterface(), cout);
		Call::dumpSql(Sql::getInterface(), cout);
		cout << Sql::getInterface()->end_commands();
#ifdef LINUX_STAT_MONITOR
		char buff[100];
		snprintf(buff, sizeof(buff), "cat /proc/%u/stat >%u.stat", getpid(), getpid());
		if (system(buff) != 0) {
			fprintf(stderr, "Unable to run %s\n", buff);
			exit(1);
		}
#endif
		return 0;
	}

	if (CscoutOptions::process_mode != pm_compile) {
		swill_handle("src.html", source_page, NULL);
		swill_handle("qsrc.html", query_source_page, NULL);
		swill_handle("fedit.html", fedit_page, NULL);
		swill_handle("file.html", file_page, NULL);
		swill_handle("dir.html", dir_page, NULL);

		// Identifier query and execution
		swill_handle("iquery.html", iquery_page, NULL);
		swill_handle("xiquery.html", xiquery_page, NULL);
		// File query and execution
		swill_handle("filequery.html", filequery_page, NULL);
		swill_handle("xfilequery.html", xfilequery_page, NULL);
		swill_handle("qinc.html", query_include_page, NULL);

		// Function query and execution
		swill_handle("funquery.html", funquery_page, NULL);
		swill_handle("xfunquery.html", xfunquery_page, NULL);

		swill_handle("id.html", identifier_page, NULL);
		swill_handle("fun.html", function_page, NULL);
		swill_handle("funlist.html", funlist_page, NULL);
		swill_handle("funmetrics.html", function_metrics_page, NULL);
		swill_handle("filemetrics.html", file_metrics_page, NULL);
		swill_handle("idmetrics.html", id_metrics_page, NULL);

		graph_handle("cgraph", cgraph_page);
		graph_handle("fgraph", fgraph_page);
		graph_handle("cpath", cpath_page);

		swill_handle("about.html", about_page, NULL);
		swill_handle("setproj.html", set_project_page, NULL);
		swill_handle("logo.png", logo_page, NULL);
		swill_handle("index.html", index_page, 0);
		// REST/JSON API endpoints
		swill_handle("api/identifiers", api_identifiers, NULL);
		swill_handle("api/files", api_files, NULL);
		swill_handle("api/functions", api_functions, NULL);
		swill_handle("api/projects", api_projects, NULL);
		swill_handle("api/id", api_id, NULL);
		swill_handle("api/funcs", api_funcs, NULL);
		swill_handle("api/filemetrics", api_filemetrics, NULL);
		swill_handle("api/projectfiles", api_projectfiles, NULL);
		swill_handle("api/funmetrics", api_funmetrics, NULL);
		swill_handle("api/refactor", api_refactor, NULL);
	}


	if (file_msum.get_pre_cpp_writable(Metrics::em_nuline)) {
		ostringstream msg;
		msg << file_msum.get_pre_cpp_writable(Metrics::em_nuline) <<
		    " conditionally compiled writable lines" << endl <<
		    "(out of a total of " <<
		    (int)file_msum.get_pre_cpp_writable(Metrics::em_nline) <<
		    " writable lines) were not processed";
		Error::error(E_WARN, msg.str(), false);
	}

	CTag::save();
	if (CscoutOptions::process_mode == pm_report) {
		if (!must_exit)
			warning_report();
		return (0);
	}

#ifdef PICO_QL
	if (pico_ql) {
		pico_ql_register(&files, "files");
		pico_ql_register(&Identifier::ids, "ids");
		pico_ql_register(&Tokid::tm, "tm");
		pico_ql_register(&Call::functions(), "fun_map");
		while (pico_ql_serve(CscoutOptions::portno))
			;
		return (0);
	}
#endif

	if (CscoutOptions::process_mode == pm_call_graph) {
		cerr << "Producing call graphs for: ";
		for (string d : call_graphs) cerr << d << " ";
		cerr << endl;
		produce_call_graphs(call_graphs);

		return (0);
	}

	if (DP())
		cout  << "Tokid EC map size is " << Tokid::map_size() << endl;
	if (CscoutOptions::process_mode == pm_compile)
		return (0);
	// Serve web pages
	if (!must_exit)
		cerr << "CScout is now ready to serve you at http://localhost:" << CscoutOptions::portno << endl;
	if (browse_only)
		swill_setfork();
	while (!must_exit)
		swill_serve();

#ifdef NODE_USE_PROFILE
	cout << "Type node count = " << Type_node::get_count() << endl;
#endif
	return (0);
}


/*
 * Clear equivalence classes that do not satisfy the monitoring criteria.
 * Called after processing each input file, for that file.
 */
void
garbage_collect(Fileid root)
{
	vector <Fileid> files(Fileid::files(false));
	set <Fileid> touched_files;

	int count = 0;
	int sum = 0;

	Filedetails::set_compilation_unit(root, true);
	for (vector <Fileid>::iterator i = files.begin(); i != files.end(); i++) {
		Fileid fi = (*i);

		/*
		 * All files from which we input data during parsing
		 * are marked as in need for GC. Therefore all the files
		 * our parsing touched are marked as dirty
		 * (and will be marked clean again at the end of this loop)
		 */
		if (Filedetails::is_garbage_collected(fi))
			continue;

		Filedetails::set_required(fi, false);	// Mark the file as not being required
		touched_files.insert(fi);

		if (!CscoutOptions::monitor.is_valid()) {
			Filedetails::set_garbage_collected(fi, true);	// Mark the file as garbage collected
			continue;
		}

		const string &fname = fi.get_path();
		fifstream in;

		in.open(fname.c_str(), ios::binary);
		if (in.fail()) {
			perror(fname.c_str());
			exit(1);
		}
		// Go through the file character by character
		for (;;) {
			Tokid ti;
			int val;

			ti = Tokid(fi, in.tellg());
			if ((val = in.get()) == EOF)
				break;
			mapTokidEclass::iterator ei = ti.find_ec();
			if (ei != ti.end_ec()) {
				sum++;
				Eclass *ec = ei->second;
				IdPropElem ec_id(ec, Identifier());
				if (!CscoutOptions::monitor.eval(ec_id)) {
					count++;
					ec->remove_from_tokid_map();
					delete ec;
				}
			}
		}
		in.close();
		Filedetails::set_garbage_collected(fi, true);	// Mark the file as garbage collected
	}
	if (DP())
		cout << "Garbage collected " << count << " out of " << sum << " ECs" << endl;

	// Monitor dependencies
	set <Fileid> required_files;

	// Recursively mark all the files containing definitions for us
	Fdep::mark_required(root);
	// Store them in a set to calculate set difference
	for (set <Fileid>::const_iterator i = touched_files.begin(); i != touched_files.end(); i++)
		if (*i != root && *i != input_file_id)
			Filedetails::set_includes(root, *i, /* directly included (conservatively) */ false, Filedetails::is_required(*i));
	if (CscoutOptions::process_mode == pm_database)
		Fdep::dumpSql(Sql::getInterface(), cout, root);
	Fdep::reset();

	return;
}
