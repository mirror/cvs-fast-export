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
#include "gram.h"

/* lex.h should declare these, and does, in 2.5.39.  But didn't, in 2.5.35. */ 
int yyget_column (yyscan_t);
void yyset_column(int, yyscan_t);

static char *
parse_data(yyscan_t scanner);
static void
parse_text(cvs_text *text, yyscan_t scanner, cvs_file *);
static char *
parse_data_until_newline(yyscan_t scanner);
static void
fast_export_sanitize(yyscan_t scanner, cvs_file *cvs);

/*
 * A relative of export.c's optimization, we can use unlocked getc
 * in the body of the lexer, because the FILE*'s returned by yyget_in()
 * are all private to the invoking thread.
 */
#ifdef __GLIBC__
#undef  getc
#define getc	getc_unlocked
#endif /* __GLIBC__ */


/* FIXME: this is inefficient */
#define YY_INPUT(buf,result,max_size) { \
    int c = getc(yyget_in(yyscanner)); \
    result = (c == EOF) ? YY_NULL : (buf[0] = c, 1); \
}
    
YY_DECL;
%}
%option reentrant bison-bridge
%option warn nodefault
%option pointer
%option noyywrap noyyget_extra noyyget_leng noyyset_lineno
%option noyyget_out noyyset_out noyyget_lval noyyset_lval
%option noyyget_lloc noyyset_lloc noyyget_debug noyyset_debug

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
					parse_text(&yylval->text, yyscanner, cvs);
					BEGIN(INITIAL);
					return TEXT_DATA;
				}
<CONTENT>[-a-zA-Z_+%][-a-zA-Z_0-9+/%=.~^\\*?#!\[\]()<>]* {
					fast_export_sanitize(yyscanner, cvs);
					yylval->atom = atom(yytext);
					return TOKEN;
				}
<AUTHORSS>[-a-zA-Z_0-9+%][-a-zA-Z_0-9+/%=.~^\\*?]* {
					fast_export_sanitize(yyscanner, cvs);
					yylval->atom = atom(yytext);
					return TOKEN;
				}
<PERM>[0-9]+			{
					return IGNORED;
				}
<COMMIT>[0-9a-zA-Z]+		{
					yylval->atom = atom(yytext);
					return TOKEN;
				}
<REVISION>[0-9]+\.[0-9.]*			{
					yylval->number = lex_number(yytext);
					return NUMBER;
				}
<FNAME>[^;]*			{
					return IGNORED;
				}
[0-9]+\.[0-9.]*			{
					yylval->number = lex_number(yytext);
					return NUMBER;
				}
;				BEGIN(INITIAL); return SEMI;
:				return COLON;
<SKIPTOSEMI>[^;]*		{

#ifdef __UNUSED__
					/*
					 * If we ever need the data from the kopt
					 * or hardlinks clause,
					 * (1) Condition in this.
                                         * (2) Condition in the definition of
					 * parse_data_until_newline() below.
					 * (3) Change IGNORED to DATA
					 * (4) Make the corresponding change
					 * in the grammar file.
					 * Renember, parse_data_until_newline()
					 * return allocated storage.
					 */
					yylval->s = parse_data_until_newline(yyscanner);
					return DATA;
#else
					return IGNORED;
#endif /* __UNUSED__ */
				}
<INITIAL,CONTENT>@		{
					yylval->s = parse_data(yyscanner);
					return DATA;
				}
" " 				;
\t				;
\n				;
1				return BRAINDAMAGED_NUMBER;
.				{ 
					warn("%s: (%d) ignoring %c\n",
					    cvs->gen.master_name, yylineno,
					    yytext[0]);
				}
%%

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

static char *varbuf_dup(struct varbuf *buf, const char *legend) {
	char *dup = xmalloc(buf->cur, legend);
	memcpy(dup, buf->string, buf->cur);
	return dup;
}

static char *
parse_data(yyscan_t yyscanner)
{
    int c;
    char *ret;
    struct varbuf buf;

    varbuf_init(&buf);

    for(;;) {
	c = getc(yyget_in(yyscanner));
	if (c == '@') {
	    c = getc (yyget_in(yyscanner));
	    if (c != '@') 
		break;
	}
	varbuf_add(&buf, c);
    }
    ungetc(c, yyget_in(yyscanner));
    varbuf_add(&buf, '\0');
    ret = varbuf_dup(&buf, "parse_data");
    varbuf_free(&buf);
    return ret;
}

static void
parse_text(cvs_text *text, yyscan_t yyscanner, cvs_file *cvs)
{
    int c;
    size_t length;

    text->filename = cvs->gen.master_name;
    text->offset = ftell(yyget_in(yyscanner)) - 1;
    length = 1;

    while ((c = getc(yyget_in(yyscanner))) != EOF) {
	++length;
        if (c == '@') {
	    /* lookahead to see if we hit @@ */
	    c = getc(yyget_in(yyscanner));
	    if (c == '@') {
	        ++length;
	    } else {
		/* We consume only the closing single @,
		 * leaving it included in the length */
	        ungetc(c, yyget_in(yyscanner));
		break;
	    }
	}
    }
    text->length = length;
}

#ifdef __UNUSED__
static char *
parse_data_until_newline(yyscan_t yyscanner)
{
    int c;
    char *ret;
    struct varbuf buf;

    varbuf_init(&buf);
    for(;;) {
	c = getc(yyget_in(yyscanner));
	if (c == '\n') {
		break;
	}
	varbuf_add(&buf, c);
    }
    ungetc(c, yyget_in(yyscanner));
    varbuf_add(&buf, '\0');
    ret = varbuf_dup(&buf, "parse_data_until_newline");
    varbuf_free(&buf);
    return ret;
}
#endif /* __UNUSED__ */

cvs_number
lex_number(const char *s)
{
    cvs_number	n;
    const char	*next;

    n.c = 0;
    while (*s) {
	n.n[n.c] = (int)strtol(s, (char **)&next, 10);
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
lex_date(const cvs_number const *n, yyscan_t yyscanner, cvs_file *cvs)
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
			cvs->gen.master_name, yyget_lineno(yyscanner));
	for (i = 0; i < n->c; i++) {
	    if (i) fprintf(stderr, ".");
	    fprintf(stderr, "%d", n->n[i]);
	}
	fprintf(stderr, "\n");
    }
    if (d < RCS_EPOCH)
	fatal_error("%s: (%d) date before RCS epoch: ",
		    cvs->gen.master_name,
		    yyget_lineno(yyscanner));
    else if (d >= RCS_OMEGA)
	fatal_error("%s: (%d) date too far in future: ",
		    cvs->gen.master_name,
		    yyget_lineno(yyscanner));
    return d - RCS_EPOCH;
}

static void fast_export_sanitize(yyscan_t yyscanner, cvs_file *cvs)
{
    char *sp, *tp;

#define SUFFIX(a, s)	(strcmp(a + strlen(a) - strlen(s), s) == 0) 
#define BADCHARS	"~^\\*?"
    for (sp = tp = yyget_text(yyscanner); *sp; sp++) {
	if (isgraph((unsigned char)*sp) && strchr(BADCHARS, *sp) == NULL) {
	    *tp++ = *sp;
	    if (SUFFIX(yyget_text(yyscanner), "@{") || SUFFIX(yyget_text(yyscanner), "..")) {
		fatal_error("%s: (%d) tag or branch name %s is ill-formed.\n", 
			    cvs->gen.master_name, yyget_lineno(yyscanner), yyget_text(yyscanner));
	    }
	}
    }
    *tp = '\0';
    if (strlen(yyget_text(yyscanner)) == 0) {
	fatal_error("%s: (%d) tag or branch name was empty after sanitization.\n", 
		    cvs->gen.master_name, yyget_lineno(yyscanner));
    }
}
