# Makefile for cvs-fast-export
#
# Build requirements: A C compiler, yacc, lex, zlib, and asciidoc.

INSTALL = install
prefix?=/usr/local
target=$(DESTDIR)$(prefix)
LEX=/usr/bin/flex

VERSION=1.2

GCC_WARNINGS1=-Wall -Wpointer-arith -Wstrict-prototypes
GCC_WARNINGS2=-Wmissing-prototypes -Wmissing-declarations
GCC_WARNINGS3=-Wno-unused-function -Wno-unused-label -Wno-format-zero-length
GCC_WARNINGS=$(GCC_WARNINGS1) $(GCC_WARNINGS2) $(GCC_WARNINGS3)
CFLAGS=$(GCC_WARNINGS) -DVERSION=\"$(VERSION)\"

# To enable debugging of the Yacc grammar, uncomment the following line
#CFLAGS += -DYYDEBUG=1

YFLAGS=-d -l
LFLAGS=-l

# To enable profiling, uncomment the following line
# Note: the profiler gets confused if you don't also turn off -O flags.
#CFLAGS += -pg
CFLAGS += -O

# To enable blob compression, uncomment the following:
#CFLAGS += -DZLIB
#LDFLAGS += -lz

OBJS=gram.o lex.o main.o cvsutil.o revdir.o \
	revlist.o atom.o revcvs.o generate.o export.o \
	nodehash.o tags.o authormap.o graph.o utils.o

cvs-fast-export: $(OBJS)
	cc $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): cvs.h

gram.c: gram.y
	@echo "Expect conflicts: 10 shift/reduce, 2 reduce/reduce"
	yacc $(YFLAGS) gram.y 
	mv -f y.tab.c gram.c

lex.o: y.tab.h

lex.o: lex.c

y.tab.h: gram.c

.SUFFIXES: .html .asc .txt .1

# Requires asciidoc
.asc.1:
	a2x --doctype manpage --format manpage $*.asc
.asc.html:
	a2x --doctype manpage --format xhtml $*.asc

man: cvssync.1 cvs-fast-export.1

clean:
	rm -f $(OBJS) y.tab.h gram.c lex.c cvs-fast-export docbook-xsl.css
	rm -f cvs-fast-export.1 cvs-fast-export.html
	rm -f cvssync.1 cvssync.html PROFILE gmon.out
	rm -f MANIFEST index.html *.tar.gz

check: cvs-fast-export
	@(cd tests >/dev/null; make -s)

install: cvs-fast-export man
	$(INSTALL) -d "$(target)/bin"
	$(INSTALL) -d "$(target)/share/man/man1"
	$(INSTALL) cvs-fast-export "$(target)/bin"
	$(INSTALL) cvssync "$(target)/bin"
	$(INSTALL) -m 644 cvs-fast-export.1 "$(target)/share/man/man1"
	$(INSTALL) -m 644 cvssync.1 "$(target)/share/man/man1"

PROFILE_REPO = ~/software/groff-conversion/groff-mirror/groff
gmon.out: cvs-fast-export
	find $(PROFILE_REPO) -name '*,v' | cvs-fast-export -k -p >/dev/null
PROFILE: gmon.out
	gprof cvs-fast-export >PROFILE


# Weird suppressions are required because of strange tricks in Bison.
SUPPRESSIONS = -U__UNUSED__ -UYYPARSE_PARAM -UYYTYPE_INT16 -UYYTYPE_INT8 \
	-UYYTYPE_UINT16 -UYYTYPE_UINT8 -UYY_USER_INIT \
	-Ushort -Usize_t -Uyytext_ptr -Uyyoverflow
cppcheck:
	cppcheck -I. --template gcc --enable=all $(SUPPRESSIONS) --suppress=unusedStructMember --suppress=unusedFunction --suppress=unreadVariable --suppress=uselessAssignmentPtrArg --suppress=missingIncludeSystem *.[ch]

SOURCES = Makefile *.[ch] *.[yl] cvssync
DOCS = README COPYING NEWS AUTHORS TODO control cvs-fast-export.asc cvssync.asc
ALL =  $(SOURCES) $(DOCS)
cvs-fast-export-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:cvs-fast-export-$(VERSION)/:' --show-transformed-names -cvzf cvs-fast-export-$(VERSION).tar.gz $(ALL)

dist: cvs-fast-export-$(VERSION).tar.gz

release: cvs-fast-export-$(VERSION).tar.gz cvs-fast-export.html cvssync.html
	shipper version=$(VERSION) | sh -e -x
