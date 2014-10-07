/*
 * Copyright � 2006 Sean Estabrooks <seanlkml@sympatico.ca>
 * Copyright � 2006 Keith Packard <keithp@keithp.com>
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
 *
 * Large portions of code contained in this file were obtained from
 * the original RCS application under GPLv2 or later license, it retains
 * the copyright of the original authors listed below:
 *
 * Copyright 1982, 1988, 1989 Walter Tichy
 *   Copyright 1990, 1991, 1992, 1993, 1994, 1995 Paul Eggert
 *      Distributed under license by the Free Software Foundation, Inc.
 */

/*
 * The entire aim of this module is the last function, which turns
 * the in-core revision history of a CVS/RCS master file and materializes
 * all of its revision levels through a specified export hook.
 */

#include <limits.h>
#include <stdarg.h>
#include "cvs.h"

typedef unsigned char uchar;
struct alloclist {
    void* alloc;
    struct alloclist *nextalloc;
};

struct diffcmd {
	long line1, nlines, adprev, dafter;
};

const int initial_out_buffer_size = 1024;
char const ciklog[] = "checked in with -k by ";

#define KEYLENGTH 8 /* max length of any of the above keywords */
#define KDELIM '$' /* keyword delimiter */
#define VDELIM ':' /* separates keywords from values */
#define SDELIM '@' /* string delimiter */
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

char const *const Keyword[] = {
	0, "Author", "Date", "Header", "Id", "Locker", "Log",
	"Name", "RCSfile", "Revision", "Source", "State"
};
enum markers { Nomatch, Author, Date, Header, Id, Locker, Log,
	Name, RCSfile, Revision, Source, State };
enum stringwork {ENTER, EDIT};

#define Gline(eb) eb->stack[eb->depth].line
#define Ggap(eb) eb->stack[eb->depth].gap
#define Ggapsize(eb) eb->stack[eb->depth].gapsize
#define Glinemax(eb) eb->stack[eb->depth].linemax
#define Gnode_text(eb) eb->stack[eb->depth].node_text

/* backup one position in the input buffer, unless at start of buffer
 *   return character at new position, or EOF if we could not back up
 */
static int in_buffer_ungetc(editbuffer_t *eb)
{
    int c;
    if (Ginbuf(eb)->read_count == 0)
	return EOF;
    --Ginbuf(eb)->read_count;
    --Ginbuf(eb)->ptr;
    c = *Ginbuf(eb)->ptr;
    if (c == SDELIM) {
	--Ginbuf(eb)->ptr;
	c = *Ginbuf(eb)->ptr;
    }
    return c;
}

static int in_buffer_getc(editbuffer_t *eb)
{
    int c;
    c = *(Ginbuf(eb)->ptr++);
    ++Ginbuf(eb)->read_count;
    if (c == SDELIM) {
	c = *(Ginbuf(eb)->ptr++);
	if (c != SDELIM) {
	    Ginbuf(eb)->ptr -= 2;
	    --Ginbuf(eb)->read_count;
	    return EOF;
	}
    }
    return c ;
}

static uchar *in_get_line(editbuffer_t *eb)
{
    int c;
    uchar *ptr = Ginbuf(eb)->ptr;
    c=in_buffer_getc(eb);
    if (c == EOF)
	return NULL;
    while (c != EOF && c != '\n')
	c = in_buffer_getc(eb);
    return ptr;
}

static uchar * in_buffer_loc(editbuffer_t *eb)
{
	return(Ginbuf(eb)->ptr);
}

static void in_buffer_init(editbuffer_t *eb, uchar *text, int bypass_initial)
{
    Ginbuf(eb)->ptr = Ginbuf(eb)->buffer = text;
    Ginbuf(eb)->read_count=0;
    if (bypass_initial && *Ginbuf(eb)->ptr++ != SDELIM)
	fatal_error("Illegal buffer, missing @ %s", text);
}

static void out_buffer_init(editbuffer_t *eb)
{
    char *t;
    eb->Goutbuf = xmalloc(sizeof(struct out_buffer_type), "out_buffer_init");
    memset(eb->Goutbuf, 0, sizeof(struct out_buffer_type));
    eb->Goutbuf->size = initial_out_buffer_size;
    t = xmalloc(eb->Goutbuf->size, "out+buffer_init");
    eb->Goutbuf->text = t;
    eb->Goutbuf->ptr = t;
    eb->Goutbuf->end_of_text = t + eb->Goutbuf->size;
}

static void out_buffer_enlarge(editbuffer_t *eb)
{
    int ptroffset = eb->Goutbuf->ptr - eb->Goutbuf->text;
    eb->Goutbuf->size *= 2;
    eb->Goutbuf->text = xrealloc(eb->Goutbuf->text, eb->Goutbuf->size, "out_buffer_enlarge");
    eb->Goutbuf->end_of_text = eb->Goutbuf->text + eb->Goutbuf->size;
    eb->Goutbuf->ptr = eb->Goutbuf->text + ptroffset;
}

static unsigned long  out_buffer_count(editbuffer_t *eb)
{
    return(unsigned long) (eb->Goutbuf->ptr - eb->Goutbuf->text);
}

static char *out_buffer_text(editbuffer_t *eb)
{
    return eb->Goutbuf->text;
}

static void out_buffer_cleanup(editbuffer_t *eb)
{
    free(eb->Goutbuf->text);
    free(eb->Goutbuf);
}

inline static void out_putc(editbuffer_t *eb, int c)
{
    *eb->Goutbuf->ptr++ = c;
    if (eb->Goutbuf->ptr >= eb->Goutbuf->end_of_text)
	out_buffer_enlarge(eb);
}

static void out_printf(editbuffer_t *eb, const char *fmt, ...)
{
    va_list ap;
    while (1) {
	int ret, room;
	room = eb->Goutbuf->end_of_text - eb->Goutbuf->ptr;
	va_start(ap, fmt);
	ret = vsnprintf(eb->Goutbuf->ptr, room, fmt, ap);
	va_end(ap);
	if (ret > -1 && ret < room) {
	    eb->Goutbuf->ptr += ret;
	    return;
	}
	out_buffer_enlarge(eb);
    }
}

static int out_fputs(editbuffer_t *eb, const char *s)
{
    while (*s)
	out_putc(eb, *s++);
    return 0;
}

static void out_awrite(editbuffer_t *eb, char const *s, size_t len)
{
    while (len--)
	out_putc(eb, *s++);
}

static bool latin1_alpha(int c)
{
    if (c >= 192 && c != 215 && c != 247 ) return true;
    if ((c >= 97 && c < 123) || (c >= 65 && c < 91)) return true;
    return false;
}

static int latin1_whitespace(uchar c)
{
    if (c == 32 || (c >= 8 && c <= 13 && c != 10)) return true;
    return false;
}

static enum expand_mode expand_override(char const *s)
{
    if (s != NULL)
    {
	char * const expand_names[] = {"kv", "kvl", "k", "v", "o", "b"};
	int i;
	for (i=0; i < 6; ++i)
	    if (strcmp(s,expand_names[i]) == 0)
		return(enum expand_mode) i;
    }
    return EXPANDKK;
}

static char const *basefilename(char const *p)
{
    char const *b = p, *q = p;
    for (;;)
	switch(*q++) {
	case '/': b = q; break;
	case 0: return b;
	}
}

/* Convert relative RCS filename to absolute path */
static char const * getfullRCSname(editbuffer_t *eb)
{
    char *wdbuf = NULL;
    int wdbuflen = 0;

    size_t dlen;

    char const *r;
    char* d;

    if (eb->Gfilename[0] == '/')
	return eb->Gfilename;

    /* If we've already calculated the absolute path, return it */
    if (eb->Gabspath)
	return eb->Gabspath;

    /* Get working directory and strip any trailing slashes */
    wdbuflen = _POSIX_PATH_MAX + 1;
    wdbuf = xmalloc(wdbuflen, "getcwd");
    while (!getcwd(wdbuf, wdbuflen)) {
	if (errno == ERANGE)
	    /* coverity[leaked_storage] */
	    xrealloc(wdbuf, wdbuflen<<1, "getcwd");
	else 
	    fatal_system_error("getcwd");
    }

    /* Trim off trailing slashes */
    dlen = strlen(wdbuf);
    while (dlen && wdbuf[dlen-1] == '/')
	--dlen;
    wdbuf[dlen] = 0;

    /* Ignore leading `./'s in Gfilename. */
    for (r = eb->Gfilename;  r[0]=='.' && r[1] == '/';  r += 2)
	while (r[2] == '/')
	    r++;

    /* Build full pathname.  */
    eb->Gabspath = d = xmalloc(dlen + strlen(r) + 2, "pathname building");
    memcpy(d, wdbuf, dlen);
    d += dlen;
    *d++ = '/';
    strcpy(d, r);
    free(wdbuf);

    return eb->Gabspath;
}

static enum markers trymatch(char const *string)
/* Check if string starts with a keyword followed by a KDELIM or VDELIM */
{
    int j;
    for (j = sizeof(Keyword)/sizeof(*Keyword);  (--j);  ) {
	char const *p, *s;
	p = Keyword[j];
	s = string;
	while (*p++ == *s++) {
	    if (!*p) {
		switch(*s) {
		case KDELIM:
		case VDELIM:
		    return(enum markers)j;
		default:
		    return Nomatch;
		}
	    }
	}
    }
    return(Nomatch);
}

static void insertline(editbuffer_t *eb, unsigned long n, uchar * l)
/* Before line N, insert line L.  N is 0-origin.  */
{
    if (n > Glinemax(eb) - Ggapsize(eb))
	fatal_error("edit script tried to insert beyond eof");
    if (!Ggapsize(eb)) {
	if (Glinemax(eb)) {
	    Ggap(eb) = Ggapsize(eb) = Glinemax(eb); Glinemax(eb) <<= 1;
	    Gline(eb) = xrealloc(Gline(eb), sizeof(uchar *) * Glinemax(eb), "insertline");
	} else {
	    Glinemax(eb) = Ggapsize(eb) = 1024;
	    Gline(eb) = xmalloc(sizeof(uchar *) *  Glinemax(eb), "insertline");
	}
    }
    if (n < Ggap(eb))
	memmove(Gline(eb)+n+Ggapsize(eb), Gline(eb)+n, (Ggap(eb)-n) * sizeof(uchar *));
    else if (Ggap(eb) < n)
	memmove(Gline(eb)+Ggap(eb), Gline(eb)+Ggap(eb)+Ggapsize(eb), (n-Ggap(eb)) * sizeof(uchar *));
    Gline(eb)[n] = l;
    Ggap(eb) = n + 1;
    Ggapsize(eb)--;
}

static void deletelines(editbuffer_t *eb, unsigned long n, unsigned long nlines)
/* Delete lines N through N+NLINES-1.  N is 0-origin.  */
{
    unsigned long l = n + nlines;
    if (Glinemax(eb)-Ggapsize(eb) < l  ||  l < n)
	fatal_error("edit script tried to delete beyond eof");
    if (l < Ggap(eb))
	memmove(Gline(eb)+l+Ggapsize(eb), Gline(eb)+l, (Ggap(eb)-l) * sizeof(uchar *));
    else if (Ggap(eb) < n)
	memmove(Gline(eb)+Ggap(eb), Gline(eb)+Ggap(eb)+Ggapsize(eb), (n-Ggap(eb)) * sizeof(uchar *));
    Ggap(eb) = n;
    Ggapsize(eb) += nlines;
}

static long parsenum(editbuffer_t *eb)
/* parse and return a decimal integer */
{
    int c;
    long ret = 0;
    for (c=in_buffer_getc(eb); isdigit(c); c=in_buffer_getc(eb))
	ret = (ret * 10) + (c - '0');
    in_buffer_ungetc(eb);
    return ret;
}

static int parse_next_delta_command(editbuffer_t *eb, struct diffcmd *dc)
{
    int cmd;
    long line1, nlines;

    cmd = in_buffer_getc(eb);
    if (cmd==EOF)
	return -1;

    line1 = parsenum(eb);

    while (in_buffer_getc(eb) == ' ')
	;
    in_buffer_ungetc(eb);

    nlines = parsenum(eb);

    while (in_buffer_getc(eb) != '\n')
	;

    if (!nlines || (cmd != 'a' && cmd != 'd') || line1+nlines < line1)
	fatal_error("Corrupt delta");

    if (cmd == 'a') {
	if (line1 < dc->adprev)
	    fatal_error("backward insertion in delta");
	dc->adprev = line1 + 1;
    } else if (cmd == 'd') {
	if (line1 < dc->adprev  ||  line1 < dc->dafter)
	    fatal_error("backward deletion in delta");
	dc->adprev = line1;
	dc->dafter = line1 + nlines;
    }

    dc->line1 = line1;
    dc->nlines = nlines;
    return cmd == 'a';
}

static void escape_string(editbuffer_t *eb, register char const *s)
{
    for (;;) {
	register char c;
	switch((c = *s++)) {
	case 0:		return;
	case '\t':	out_fputs(eb, "\\t"); break;
	case '\n':	out_fputs(eb, "\\n"); break;
	case ' ':	out_fputs(eb, "\\040"); break;
	case KDELIM:	out_fputs(eb, "\\044"); break;
	case '\\':	out_fputs(eb, "\\\\"); break;
	default:	out_putc(eb, c); break;
	}
    }
}

static void keyreplace(editbuffer_t *eb, enum markers marker)
/* output the appropriate keyword value(s) */
{
    char *leader = NULL;
    char date_string[25];
    enum expand_mode exp = eb->Gexpand;
    char const *kw = Keyword[(int)marker];
    time_t utime = RCS_EPOCH + eb->Gversion->date;

    strftime(date_string, 25, "%Y/%m/%d %H:%M:%S", localtime(&utime));

    out_printf(eb, "%c%s", KDELIM, kw);

    /* bug: Locker expansion is not implemented */
    if (exp != EXPANDKK) {
#ifdef __UNUSED__
	const char *target_lockedby = NULL;
#endif /* __UNUSED__ */

	if (exp != EXPANDKV)
	    out_printf(eb, "%c%c", VDELIM, ' ');

	switch(marker) {
	case Author:
	    out_fputs(eb, eb->Gversion->author);
	    break;
	case Date:
	    out_fputs(eb, date_string);
	    break;
	case Id:
	case Header:
	    if (marker == Id )
		escape_string(eb, basefilename(eb->Gfilename));
	    else
		escape_string(eb, getfullRCSname(eb));
	    out_printf(eb, " %s %s %s %s",
		       eb->Gversion_number, date_string,
		       eb->Gversion->author, eb->Gversion->state);
#ifdef __UNUSED__
	    if (target_lockedby && exp == EXPANDKKVL)
		out_printf(eb, " %s", target_lockedby);
#endif /* __UNUSED__ */
	    break;
	case Locker:
#ifdef __UNUSED__
	    if (target_lockedby && exp == EXPANDKKVL)
		out_fputs(eb, target_lockedby);
#endif /* __UNUSED__ */
	    break;
	case Log:
	case RCSfile:
	    escape_string(eb, basefilename(eb->Gfilename));
	    break;
	case Revision:
	    out_fputs(eb, eb->Gversion_number);
	    break;
	case Source:
	    escape_string(eb, getfullRCSname(eb));
	    break;
	case State:
	    out_fputs(eb, eb->Gversion->state);
	    break;
	default:
	    break;
	}

	if (exp != EXPANDKV)
	    out_putc(eb, ' ');
    }

#if 0
/* Closing delimiter is processed again in expandline */
    if (exp != EXPANDKV)
	out_putc(eb, KDELIM);
#endif

    if (marker == Log) {
	char const *xxp;
	uchar *kdelim_ptr = NULL;
	int c;
	size_t cs, cw, ls;
	/*
	 * "Closing delimiter is processed again in expandline"
	 * does not apply here, since we consume the input.
	 */
	if (exp != EXPANDKV)
	    out_putc(eb, KDELIM);

	kw = eb->Glog;
	ls = strlen(eb->Glog);
	if (sizeof(ciklog)-1<=ls && !memcmp(kw,ciklog,sizeof(ciklog)-1))
	    return;

	/* Back up to the start of the current input line */
	int num_kdelims = 0;
	for (;;) {
	    c = in_buffer_ungetc(eb);
	    if (c == EOF)
		break;
	    if (c == '\n') {
		in_buffer_getc(eb);
		break;
	    }
	    if (c == KDELIM) {
		num_kdelims++;
		/* It is possible to have multiple keywords
		   on one line. Make sure we don't backtrack
		   into some other keyword! */
		if (num_kdelims > 2) {
		    in_buffer_getc(eb);
		    break;
		}
		kdelim_ptr = in_buffer_loc(eb);
	    }
	}

	/* Copy characters before `$Log' into LEADER.  */
	xxp = leader = xmalloc(kdelim_ptr - in_buffer_loc(eb), "keyword expansion");
	for (cs = 0; ;  cs++) {
	    c = in_buffer_getc(eb);
	    if (c == KDELIM)
		break;
	    leader[cs] = c;
	}

	/* Convert traditional C or Pascal leader to ` *'.  */
	for (cw = 0;  cw < cs;  cw++)
	    if (!latin1_whitespace(xxp[cw]))
		break;
	if (cw+1 < cs &&  xxp[cw+1] == '*' &&
	    (xxp[cw] == '/'  ||  xxp[cw] == '(')) {
	    size_t i = cw+1;
	    for (;;) {
		if (++i == cs) {
		    leader[cw] = ' ';
		    break;
		} else if (!latin1_whitespace(xxp[i]))
		    break;
	    }
	}

	/* Skip `$Log ... $' string.  */
	do {
	    c = in_buffer_getc(eb);
	} while (c != KDELIM);

	out_putc(eb, '\n');
	out_awrite(eb, xxp, cs);
	out_printf(eb, "Revision %s  %s  %s",
		   eb->Gversion_number,
		   date_string,
		   eb->Gversion->author);

	/* Do not include state: it may change and is not updated.  */
	cw = cs;
	for (;  cw && (xxp[cw-1]==' ' || xxp[cw-1]=='\t');  --cw)
	    ;
	for (;;) {
	    out_putc(eb, '\n');
	    out_awrite(eb, xxp, cw);
	    if (!ls)
		break;
	    --ls;
	    c = *kw++;
	    if (c != '\n') {
		out_awrite(eb, xxp+cw, cs-cw);
		do {
		    out_putc(eb, c);
		    if (!ls)
			break;
		    --ls;
		    c = *kw++;
		} while (c != '\n');
	    }
	}
	free(leader);
    }
}

static int expandline(editbuffer_t *eb, bool enable_keyword_expansion)
{
    register int c = 0;
    char * tp;
    register int e, r;
    char const *tlim;
    enum markers matchresult;
    int orig_size;

    if (eb->Gkvlen < KEYLENGTH+3) {
	eb->Gkvlen = KEYLENGTH + 3;
	eb->Gkeyval = xrealloc(eb->Gkeyval, eb->Gkvlen, "expandline");
    }
    e = 0;
    r = -1;

    for (;;) {
	c = in_buffer_getc(eb);
	for (;;) {
	    switch(c) {
	    case EOF:
		goto uncache_exit;
	    case '\n':
		out_putc(eb, c);
		r = 2;
		goto uncache_exit;
	    case KDELIM:
		if (enable_keyword_expansion) {
		    r = 0;
		    /* check for keyword */
		    /* first, copy a long enough string into keystring */
		    tp = eb->Gkeyval;
		    *tp++ = KDELIM;
		    for (;;) {
			c = in_buffer_getc(eb);
			if (tp <= &eb->Gkeyval[KEYLENGTH] && latin1_alpha(c))
			    *tp++ = c;
			else
			    break;
		    }
		    *tp++ = c; *tp = '\0';
		    matchresult = trymatch(eb->Gkeyval+1);
		    if (matchresult==Nomatch) {
			tp[-1] = 0;
			out_fputs(eb, eb->Gkeyval);
			continue;   /* last c handled properly */
		    }

		    /* Now we have a keyword terminated with a K/VDELIM */
		    if (c==VDELIM) {
			/* try to find closing KDELIM, and replace value */
			tlim = eb->Gkeyval + eb->Gkvlen;
			for (;;) {
			    c = in_buffer_getc(eb);
			    if (c=='\n' || c==KDELIM)
				break;
			    *tp++ =c;
			    if (tlim <= tp) {
				orig_size = eb->Gkvlen;
				eb->Gkvlen *= 2;
				eb->Gkeyval = xrealloc(eb->Gkeyval, eb->Gkvlen, "expandline");
				tlim = eb->Gkeyval + eb->Gkvlen;
				tp = eb->Gkeyval + orig_size;

			    }
			    if (c==EOF)
				goto keystring_eof;
			}
			if (c!=KDELIM) {
			    /* couldn't find closing KDELIM -- give up */
			    *tp = 0;
			    out_fputs(eb, eb->Gkeyval);
			    continue;   /* last c handled properly */
			}
		    }
		    /*
		     * CVS will expand keywords that have
		     * overlapping delimiters, eg "$Name$Id$".  To
		     * support that(mis)feature, push the closing
		     * delimiter back on the input so that the
		     * loop will resume processing starting with
		     * it.
		     */
		    if (c == KDELIM)
			in_buffer_ungetc(eb);

		    /* now put out the new keyword value */
		    keyreplace(eb, matchresult);
		    e = 1;
		    break;
		}
		/* FALL THROUGH */
	    default:
		out_putc(eb, c);
		r = 0;
		break;
	    }
	    break;
	}
    }

keystring_eof:
    *tp = 0;
    out_fputs(eb, eb->Gkeyval);
uncache_exit:
    return r + e;
}

#define USE_MMAP 1

#if USE_MMAP

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#ifdef THREADS
#include <pthread.h>
#endif /* THREADS */

#ifdef THREADS
static pthread_mutex_t mmap_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* THREADS */

/* A recently used list of mmapped files */
#define NMAPS 4
static struct text_map {
    const char *filename;
    uchar *base;
    size_t size;
} text_maps[NMAPS];

static uchar *
load_text(const cvs_text *text)
{
    unsigned i;
    struct stat st;
    uchar *base;
    int fd;
    size_t size;
    size_t offset = (size_t)text->offset;

#ifdef THREADS
    pthread_mutex_lock(&mmap_mutex);
#endif /* THREADS */
    for (i = 0; i < NMAPS && text_maps[i].filename; ++i) {
        if (text_maps[i].filename == text->filename) {
	    base = text_maps[i].base;
	    if (i != 0) {
	        struct text_map t = text_maps[i];
		text_maps[i] = text_maps[i-1];
		text_maps[i-1] = t;
	    }
#ifdef THREADS
	    pthread_mutex_unlock(&mmap_mutex);
#endif /* THREADS */
	    return base + offset;
	}
    }

    if ((fd = open(text->filename, O_RDONLY)) == -1)
        fatal_system_error("open: %s", text->filename);
    if (fstat(fd, &st) == -1)
        fatal_system_error("fstat: %s", text->filename);
#if 0
    /* Coverity points out this always succeeds */
    if (st.st_size > SIZE_MAX)
        fatal_error("%s: too big", text->filename);
#endif
    size = st.st_size;
    base = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
        fatal_system_error("mmap: %s %zu", text->filename, size);
    close(fd);

    if (i == NMAPS) {
        --i;
	munmap(text_maps[i].base, text_maps[i].size);
    }
    memmove(text_maps + 1, text_maps, i * sizeof text_maps[0]);
    text_maps[0].filename = text->filename;
    text_maps[0].base = base;
    text_maps[0].size = size;
#ifdef THREADS
    pthread_mutex_unlock(&mmap_mutex);
#endif /* THREADS */

    return base + offset;
}

static void
unload_all_text(void)
{
    unsigned i;
#ifdef THREADS
    pthread_mutex_lock(&mmap_mutex);
#endif /* THREADS */
    for (i = 0; i <NMAPS; i++) {
        if (text_maps[i].filename) {
	    munmap(text_maps[i].base, text_maps[i].size);
	    text_maps[i].filename = NULL;
	}
    }
#ifdef THREADS
    pthread_mutex_unlock(&mmap_mutex);
#endif /* THREADS */
}

static void
unload_text(const cvs_text *text, uchar *data)
{
}
#else
static uchar *
load_text(const cvs_text *text)
{
    FILE *f = fopen(text->filename, "rb");
    uchar *data;

    if (!f)
	fatal_error("Cannot open %s", text->filename);
    if (fseek(f, text->offset, SEEK_SET) == -1)
        fatal_system_error("fseek %s", text->filename);
    data = xmalloc(text->length + 2, __func__);
    if (fread(data, 1, text->length, f) != text->length)
        fatal_system_error("short read %s", text->filename);
    if (data[0] != '@') fatal_error("doesn't start with '@'");
    if (data[text->length - 1] != '@') fatal_error("doesn't end with '@'");
    data[text->length] = ' ';
    data[text->length + 1] = '\0';
    fclose(f);
    return data;
}

static void
unload_text(const cvs_text *text, uchar *data)
{
    free(data);
}

static void
unload_all_text(void)
{
}
#endif /* !USE_MMAP */

static void process_delta(editbuffer_t *eb, node_t *node, enum stringwork func)
{
    long editline = 0, linecnt = 0, adjust = 0;
    int editor_command;
    struct diffcmd dc;
    uchar *ptr;

    eb->Glog = node->patch->log;
    in_buffer_init(eb, Gnode_text(eb), 1);
    eb->Gversion = node->version;
    cvs_number_string(&eb->Gversion->number, eb->Gversion_number, sizeof(eb->Gversion_number));

    switch(func) {
    case ENTER:
	while ( (ptr=in_get_line(eb)) )
	    insertline(eb, editline++, ptr);
	/* coverity[fallthrough] */
    case EDIT:
	dc.dafter = dc.adprev = 0;
	while ((editor_command = parse_next_delta_command(eb, &dc)) >= 0) {
	    if (editor_command) {
		editline = dc.line1 + adjust;
		linecnt = dc.nlines;
		while (linecnt--)
		    insertline(eb, editline++, in_get_line(eb));
		adjust += dc.nlines;
	    } else {
		deletelines(eb, dc.line1 - 1 + adjust, dc.nlines);
		adjust -= dc.nlines;
	    }
	}
	break;
    }
}

static void finishedit(editbuffer_t *eb, bool enable_keyword_expansion)
{
    uchar **p, **lim, **l = Gline(eb);
    for (p=l, lim=l+Ggap(eb);  p<lim;  ) {
	in_buffer_init(eb, *p++, 0);
	expandline(eb, enable_keyword_expansion);
    }
    for (p+=Ggapsize(eb), lim=l+Glinemax(eb);  p<lim;  ) {
	in_buffer_init(eb, *p++, 0);
	expandline(eb, enable_keyword_expansion);
    }
}

static void snapshotline(editbuffer_t *eb, register uchar * l)
{
    register int c;
    do {
	if ((c = *l++) == SDELIM  &&  *l++ != SDELIM)
	    return;
	out_putc(eb, c);
    } while (c != '\n');

}

static void snapshotedit(editbuffer_t *eb)
{
    uchar **p, **lim, **l=Gline(eb);
    for (p=l, lim=l+Ggap(eb);  p<lim;  )
	snapshotline(eb, *p++);
    for (p+=Ggapsize(eb), lim=l+Glinemax(eb);  p<lim;  )
	snapshotline(eb, *p++);
}

static void enter_branch(editbuffer_t *eb, node_t *node)
{
    uchar **p = xmalloc(sizeof(uchar *) * eb->stack[eb->depth].linemax, "enter branch");
	memcpy(p, eb->stack[eb->depth].line, sizeof(uchar *) * eb->stack[eb->depth].linemax);
	eb->stack[eb->depth + 1] = eb->stack[eb->depth];
	eb->stack[eb->depth + 1].next_branch = node->sib;
	eb->stack[eb->depth + 1].line = p;
	eb->depth++;
}

void generate_files(cvs_file *cvs,
		    bool enable_keyword_expansion,
		    void(*hook)(node_t *node, void *buf, size_t len))
/* export all the revision states of a CVS/RS master through a hook */
{
    /* this must become local to the CVS file */
    editbuffer_t *eb = &cvs->editbuffer;

    if (cvs->nodehash.head_node == NULL)
	return;

    eb->Gkeyval = NULL;
    eb->Gkvlen = 0;

    int expandflag = eb->Gexpand < EXPANDKO;
    node_t *node = cvs->nodehash.head_node;
    eb->depth = 0;
    eb->Gfilename = cvs->master_name;
    if (enable_keyword_expansion)
	eb->Gexpand = expand_override(cvs->expand);
    else
	eb->Gexpand = EXPANDKK;
    eb->Gabspath = NULL;
    Gline(eb) = NULL; Ggap(eb) = Ggapsize(eb) = Glinemax(eb) = 0;
    eb->stack[0].node = node;
    eb->stack[0].node_text = load_text(&node->patch->text);
    process_delta(eb, node, ENTER);
    while (1) {
	if (node->file) {
	    out_buffer_init(eb);
	    if (expandflag)
		finishedit(eb, enable_keyword_expansion);
	    else
		snapshotedit(eb);
	    hook(node, out_buffer_text(eb), out_buffer_count(eb));
	    out_buffer_cleanup(eb);
	}
	node = node->down;
	if (node) {
	    enter_branch(eb, node);
	    goto Next;
	}
	while ((node = eb->stack[eb->depth].node->to) == NULL) {
	    unload_text(&eb->stack[eb->depth].node->patch->text,
	                eb->stack[eb->depth].node_text);
	    free(eb->stack[eb->depth].line);
	    if (!eb->depth)
		goto Done;
	    node = eb->stack[eb->depth--].next_branch;
	    if (node) {
		enter_branch(eb, node);
		break;
	    }
	}
    Next:
	eb->stack[eb->depth].node = node;
	eb->stack[eb->depth].node_text = load_text(&node->patch->text);
	process_delta(eb, node, EDIT);
    }
Done:
    free(eb->Gkeyval);
    eb->Gkeyval = NULL;
    eb->Gkvlen = 0;
    free(eb->Gabspath);
    unload_all_text();
}

/* end */
