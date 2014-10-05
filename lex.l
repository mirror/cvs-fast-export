%option reentrant bison-bridge
%{
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
#include "y.tab.h"
    
static char *
parse_data(yyscan_t scanner);
static void
parse_text(cvs_text *text, yyscan_t scanner, cvs_file *);
static char *
parse_data_until_newline(yyscan_t scanner);
static void
fast_export_sanitize(yyscan_t scanner, cvs_file *cvs);

#define YY_INPUT(buf,result,max_size) { \
    int c = getc(yyin); \
    result = (c == EOF) ? YY_NULL : (buf[0] = c, 1); \
}
    
extern cvs_file	*this_file;
%}
%s CONTENT SKIP COMMIT PERM REVISION FNAME SKIPTOSEMI AUTHORSS
%%
<INITIAL>head			BEGIN(CONTENT); return HEAD;
<INITIAL>branch			BEGIN(CONTENT); return BRANCH;
<INITIAL>access			BEGIN(CONTENT); return ACCESS;
<INITIAL>symbols		BEGIN(CONTENT); return SYMBOLS;
<INITIAL>locks			BEGIN(CONTENT); return LOCKS;
<INITIAL>comment		BEGIN(CONTENT); return COMMENT;
<INITIAL>expand			BEGIN(CONTENT); return EXPAND;
<INITIAL>date			BEGIN(CONTENT); return DATE;
<INITIAL>branches		BEGIN(CONTENT); return BRANCHES;
<INITIAL>next			BEGIN(CONTENT); return NEXT;
<INITIAL>commitid		BEGIN(COMMIT); return COMMITID;
<INITIAL>strict			BEGIN(CONTENT); return STRICT;
<INITIAL>author			BEGIN(AUTHORSS); return AUTHOR;
<INITIAL>state			BEGIN(CONTENT); return STATE;
<INITIAL>deltatype		BEGIN(CONTENT); return DELTATYPE;
<INITIAL>group			BEGIN(PERM); return GROUP;
<INITIAL>kopt			BEGIN(SKIPTOSEMI); return KOPT;
<INITIAL>owner			BEGIN(PERM); return OWNER;
<INITIAL>permissions		BEGIN(PERM); return PERMISSIONS;
<INITIAL>filename		BEGIN(FNAME); return FILENAME;
<INITIAL>mergepoint1		BEGIN(REVISION); return MERGEPOINT;
<INITIAL>hardlinks		BEGIN(SKIPTOSEMI); return HARDLINKS;
<INITIAL>desc			return DESC;
<INITIAL>log			return LOG;
<INITIAL>text			BEGIN(SKIP); return TEXT;
<SKIP>@				{
					parse_text(&yylval->text, yyg, this_file);
					BEGIN(INITIAL);
					return TEXT_DATA;
				}
<CONTENT>[-a-zA-Z_+%][-a-zA-Z_0-9+/%=.~^\\*?#!\[\]()<>]* {
					fast_export_sanitize(yyg, this_file);
					yylval->s = atom(yytext);
					return NAME;
				}
<AUTHORSS>[-a-zA-Z_0-9+%][-a-zA-Z_0-9+/%=.~^\\*?]* {
					fast_export_sanitize(yyg, this_file);
					yylval->s = atom(yytext);
					return NAME;
				}
<PERM>[0-9]+ {
					yylval->s = atom(yytext);
					return NAME;
				}
<COMMIT>[0-9a-zA-Z]+		{
					yylval->s = atom(yytext);
					return NAME;
				}
<REVISION>[0-9]+\.[0-9.]*			{
					yylval->number = lex_number(yytext);
					return NUMBER;
				}
<FNAME>[^;]* {
	yylval->s = atom(yytext);
	return NAME;
}
[0-9]+\.[0-9.]*			{
					yylval->number = lex_number(yytext);
					return NUMBER;
				}
;				BEGIN(INITIAL); return SEMI;
:				return COLON;
<SKIPTOSEMI>[^;]*		{
					yylval->s = parse_data_until_newline(yyg);
					return DATA;
				}
<INITIAL,CONTENT>@		{
					yylval->s = parse_data(yyg);
					return DATA;
				}
" " 				;
\t				;
\n				;
1				return BRAINDAMAGED_NUMBER;
.				{ 
				    fprintf(stderr, "%s: (%d) ignoring %c\n", 
					this_file->master_name, yylineno,
					yytext[0]);
				}
%%
int yywrap(yyscan_t scanner) { return 1; }

/*
 * A variable-length buffer, allocated on the stack first
 * but can grow to use the heap.
 */
struct varbuf {
	int max, cur;
	char *string;
	char buf[1024];
};

static void varbuf_init(struct varbuf *buf)
{
	buf->max = sizeof buf->buf;
	buf->cur = 0;
	buf->string = buf->buf;
}

static void varbuf_add(struct varbuf *buf, char c)
{
	if (buf->cur == buf->max) {
	    if (buf->string == buf->buf) {
		buf->max *= 2;
		buf->string = xmalloc(buf->max, __func__);
		memcpy(buf->string, buf->buf, buf->cur);
	    } else {
		buf->max *= 2;
		buf->string = xrealloc(buf->string, buf->max, __func__);
	    }
	}
	buf->string[buf->cur++] = c;
}

static void varbuf_free(struct varbuf *buf) {
	if (buf->string != buf->buf) {
	    free(buf->string);
	}
}

static char *
parse_data(yyscan_t scanner)
{
    int c;
    char *ret;
    struct varbuf buf;

    varbuf_init(&buf);

    for(;;) {
	c = getc(yyget_in(scanner));
	if (c == '@') {
	    c = getc (yyget_in(scanner));
	    if (c != '@') 
		break;
	}
	varbuf_add(&buf, c);
    }
    ungetc(c, yyget_in(scanner));
    varbuf_add(&buf, '\0');
    ret = atom(buf.string);
    varbuf_free(&buf);
    return ret;
}

static void
parse_text(cvs_text *text, yyscan_t scanner, cvs_file *cvs)
{
    int c;
    size_t length;

    text->filename = atom(cvs->master_name);
    text->offset = ftell(yyget_in(scanner)) - 1;
    length = 1;

    while ((c = getc(yyget_in(scanner))) != EOF) {
	++length;
        if (c == '@') {
	    /* lookahead to see if we hit @@ */
	    c = getc(yyget_in(scanner));
	    if (c == '@') {
	        ++length;
	    } else {
		/* We consume only the closing single @,
		 * leaving it included in the length */
	        ungetc(c, yyget_in(scanner));
		break;
	    }
	}
    }
    text->length = length;
}

static char *
parse_data_until_newline(yyscan_t scanner)
{
    int c;
    char *ret;
    struct varbuf buf;

    varbuf_init(&buf);
    for(;;) {
	c = getc(yyget_in(scanner));
	if (c == '\n') {
		break;
	}
	varbuf_add(&buf, c);
    }
    ungetc(c, yyget_in(scanner));
    varbuf_add(&buf, '\0');
    ret = atom(buf.string);
    varbuf_free(&buf);
    return ret;
}

cvs_number
lex_number(char *s)
{
    cvs_number	n;
    char	*next;

    n.c = 0;
    while (*s) {
	n.n[n.c] = (int)strtol(s, &next, 10);
	if (next == s)
	    break;
	if (*next == '.')
	    next++;
	s = next;
	if (n.c > CVS_MAX_DEPTH)
	    fatal_error("revision too long, increase CVS_MAX_DEPTH");
	n.c++;
    }
    return n;
}

cvstime_t
lex_date(cvs_number *n, yyscan_t scanner, cvs_file *cvs)
{
	struct tm	tm;
	time_t		d;
	
	tm.tm_year = n->n[0];
	if (tm.tm_year > 1900)
	   tm.tm_year -= 1900;
	tm.tm_mon = n->n[1] - 1;
	tm.tm_mday = n->n[2];
	tm.tm_hour = n->n[3];
	tm.tm_min = n->n[4];
	tm.tm_sec = n->n[5];
	tm.tm_isdst = 0;
	#ifndef __CYGWIN__
	tm.tm_zone = 0;
	#endif
	d = mktime(&tm);
	if (d == 0) {
	    int i;
	    fprintf(stderr, "%s: (%d) unparsable date: ", 
			    cvs->master_name, yyget_lineno(scanner));
	    for (i = 0; i < n->c; i++) {
		if (i) fprintf(stderr, ".");
		fprintf(stderr, "%d", n->n[i]);
	    }
	    fprintf(stderr, "\n");
	}
	if (d < RCS_EPOCH)
	    fatal_error("%s: (%d) date before RCS epoch: ",
			cvs->master_name,
			yyget_lineno(scanner));
	else if (d >= RCS_OMEGA)
	    fatal_error("%s: (%d) date too far in future: ",
			cvs->master_name,
			yyget_lineno(scanner));
	return d - RCS_EPOCH;
}

static void fast_export_sanitize(yyscan_t scanner, cvs_file *cvs)
{
    char *sp, *tp;

#define SUFFIX(a, s)	(strcmp(a + strlen(a) - strlen(s), s) == 0) 
#define BADCHARS	"~^\\*?"
    for (sp = tp = yyget_text(scanner); *sp; sp++) {
	if (isgraph((unsigned char)*sp) && strchr(BADCHARS, *sp) == NULL) {
	    *tp++ = *sp;
	    if (SUFFIX(yyget_text(scanner), "@{") || SUFFIX(yyget_text(scanner), "..")) {
		fatal_error("%s: (%d) tag or branch name %s is ill-formed.\n", 
			    cvs->master_name, yyget_lineno(scanner), yyget_text(scanner));
	    }
	}
    }
    *tp = '\0';
    if (strlen(yyget_text(scanner)) == 0) {
	fatal_error("%s: (%d) tag or branch name was empty after sanitization.\n", 
		    cvs->master_name, yyget_lineno(scanner));
    }
}
