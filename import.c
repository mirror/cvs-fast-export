/*
 *  Copyright © 2006 Keith Packard <keithp@keithp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */
#include "cvs.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * CVS master analysis.  Grinds out a revlist structure represnting
 * the entire CVS history of a collection.
 */

cvs_file	*this_file;	/* gram.y sets this, no other module uses it */

/* these are only shared with gram.y and lex.l */
extern FILE *yyin;
extern int yylineno;
char *yyfilename;

static int load_current_file;
static int err;

static rev_list *
rev_list_file(char *name, const bool generate)
{
    rev_list	*rl;
    struct stat	buf;

    yyin = fopen(name, "r");
    if (!yyin) {
	perror(name);
	++err;
	return NULL;
    }
    yyfilename = name;
    yylineno = 0;
    this_file = xcalloc(1, sizeof(cvs_file), __func__);
    this_file->name = name;
    if (yyin)
	assert(fstat(fileno(yyin), &buf) == 0);
    this_file->mode = buf.st_mode;
    yyparse();
    fclose(yyin);
    yyfilename = 0;
    rl = rev_list_cvs(this_file);
    if (generate)
	generate_files(this_file, export_blob);
   
    cvs_file_free(this_file);
    return rl;
}

#ifdef __UNUSED__
static int
strcommon(char *a, char *b)
/* return the length of the common prefix of strings a and b */
{
    int	c = 0;
    
    while (*a == *b) {
	if (!*a)
	    break;
	a++;
	b++;
	c++;
    }
    return c;
}
#endif /* __UNUSED__ */

static int
strcommonendingwith(char *a, char *b, char endc)
/* return the length of the common prefix of strings a and b ending with endc */
{
    int c = 0;
    int d = 0;
    while (*a == *b) {
	if (!*a) {
	    break;
 	}
	a++;
	b++;
	c++;
	if (*a == endc) {
	    d = c + 1;
	}
    }
    return d;
}

typedef struct _rev_filename {
    struct _rev_filename	*next;
    char		*file;
} rev_filename;

#define PROGRESS_LEN	20

static void load_status(char *name, int load_total_files)
{
    int	spot = load_current_file * PROGRESS_LEN / load_total_files;
    int	    s;
    int	    l;

    l = strlen(name);
    if (l > 35) name += l - 35;

    fprintf(STATUS, "\rLoad: %35.35s ", name);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc(s == spot ? '*' : '.', STATUS);
    fprintf(STATUS, " %5d of %5d ", load_current_file, load_total_files);
    fflush(STATUS);
}

static void load_status_next(void)
{
    fprintf(STATUS, "\n");
    fflush(STATUS);
}

rev_list *analyze_masters(int argc, char *argv[], 
			  bool generate, time_t fromtime, 
			  bool verbose, int *filecount, int *err)
/* main entry point; collect and parse CVS masters */
{
    rev_filename    *fn_head, **fn_tail = &fn_head, *fn;
    rev_list	    *head, **tail = &head, *rl;
    char	    name[10240], *last = NULL;
    char	    *file;
    int		    nfile = 0;
    off_t	    textsize = 0;
    int		    j = 1;
    int		    c;

    progress_begin("Reading list of files...", NO_MAX);
    for (;;)
    {
	struct stat stb;

	if (argc < 2) {
	    int l;
	    /* coverity[tainted_data] Safe, never handed to exec */
	    if (fgets(name, sizeof(name) - 1, stdin) == NULL)
		break;
	    l = strlen(name);
	    if (name[l-1] == '\n')
		name[l-1] = '\0';
	    file = name;
	} else {
	    file = argv[j++];
	    if (!file)
		break;
	}

	if (stat(file, &stb) != 0)
	    continue;
	else if (S_ISDIR(stb.st_mode) != 0)
	    continue;
	else
	    textsize += stb.st_size;

	fn = xcalloc(1, sizeof(rev_filename), "filename gathering");
	fn->file = atom(file);
	*fn_tail = fn;
	fn_tail = &fn->next;
	if (striplen > 0 && last != NULL) {
	    c = strcommonendingwith(fn->file, last, '/');
	    if (c < striplen)
		striplen = c;
	} else if (striplen < 0) {
	    size_t i;

	    striplen = 0;
	    for (i = 0; i < strlen(fn->file); i++)
		if (fn->file[i] == '/')
		    striplen = i + 1;
	}
	last = fn->file;
	nfile++;
	if (progress && nfile % 100 == 0)
	    progress_jump(nfile);
    }
    progress_end("done, %ldKB in %d files", (long)(textsize/1024), nfile);
    if (generate)
	export_init();
    *filecount = nfile;
    load_current_file = 0;
    /* analyze the files for CVS revision structure */
    while (fn_head) {
	
	fn = fn_head;
	fn_head = fn_head->next;
	++load_current_file;
	if (verbose)
	    announce("processing %s\n", fn->file);
	if (progress)
	    load_status(fn->file + striplen, *filecount);
	rl = rev_list_file(fn->file, generate);
	*tail = rl;
	tail = &rl->next;

	free(fn);
    }
    if (progress)
	load_status_next();
    return head;
}

/* end */
