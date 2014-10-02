# Makefile for cvs-fast-export
#
# Build requirements: A C compiler, bison, flex, and asciidoc.
# For blob compression you will also need zlib.
# For documentation, you will need asciidoc, xsltproc and docbook stylesheets.
# The test suite requires Python 2.6, and you will see some meaningless failures
# with git 1.7.1 and older.

VERSION=1.18

prefix?=/usr/local
target=$(DESTDIR)$(prefix)

INSTALL = install
YACC = bison -y
LEX = flex

GCC_WARNINGS1=-Wall -Wpointer-arith -Wstrict-prototypes
GCC_WARNINGS2=-Wmissing-prototypes -Wmissing-declarations
GCC_WARNINGS3=-Wno-unused-function -Wno-unused-label -Wno-format-zero-length
GCC_WARNINGS=$(GCC_WARNINGS1) $(GCC_WARNINGS2) $(GCC_WARNINGS3)
CFLAGS=$(GCC_WARNINGS) -DVERSION=\"$(VERSION)\"

# To enable debugging of the Yacc grammar, uncomment the following line
#CFLAGS += -DYYDEBUG=1
# To enable debugging of blob export, uncomment the following line
#CFLAGS += -DFDEBUG=1
# To enable assertions of red black trees, uncomment the following line
#CFLAGS += -DRBDEBUG=1
# To enable debugging of CVS rev list generation, uncomment the following line
#CFLAGS += -DCVSDEBUG=1
# To enable debugging of order instability issues
#CFLAGS += -DORDERDEBUG=1

YFLAGS=-d -l
LFLAGS=-l

# To enable profiling, uncomment the following line
# Note: the profiler gets confused if you don't also turn off -O flags.
#CFLAGS += -pg
CFLAGS += -O3

# To enable blob compression, uncomment the following:
#CFLAGS += -DZLIB
#LDFLAGS += -lz

OBJS=gram.o lex.o rbtree.o main.o import.o dump.o cvsnumber.o \
	cvsutil.o revdir.o revlist.o atom.o revcvs.o generate.o export.o \
	nodehash.o tags.o authormap.o graph.o utils.o

cvs-fast-export: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@ 

$(OBJS): cvs.h

gram.c: gram.y
	@echo "Expect conflicts: 16 shift/reduce, 2 reduce/reduce"
	$(YACC) $(YFLAGS) gram.y 
	mv -f y.tab.c gram.c

lex.o: y.tab.h

lex.o: lex.c

y.tab.h: gram.c

.SUFFIXES: .html .asc .txt .1

# Requires asciidoc and xsltproc/docbook stylesheets.
.asc.1:
	a2x --doctype manpage --format manpage $*.asc
.asc.html:
	a2x --doctype manpage --format xhtml $*.asc
	rm -f docbook-xsl.css

man: cvssync.1 cvs-fast-export.1

html: cvssync.1 cvs-fast-export.1

clean:
	rm -f $(OBJS) y.tab.h gram.c lex.c cvs-fast-export docbook-xsl.css
	rm -f cvs-fast-export.1 cvs-fast-export.html
	rm -f cvssync.1 cvssync.html PROFILE gmon.out
	rm -f MANIFEST index.html *.tar.gz docbook-xsl.css

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
CSUPPRESSIONS = -U__UNUSED__ -UYYPARSE_PARAM -UYYTYPE_INT16 -UYYTYPE_INT8 \
	-UYYTYPE_UINT16 -UYYTYPE_UINT8 -UYY_USER_INIT \
	-Ushort -Usize_t -Uyytext_ptr -Uyyoverflow
cppcheck:
	cppcheck -I. --template gcc --enable=all $(CSUPPRESSIONS) --suppress=unusedStructMember --suppress=unusedFunction --suppress=unreadVariable --suppress=uselessAssignmentPtrArg --suppress=missingIncludeSystem *.[ch]

PYLINTOPTS = --rcfile=/dev/null --reports=n \
	--msg-template="{path}:{line}: [{msg_id}({symbol}), {obj}] {msg}" \
	--dummy-variables-rgx='^_'
PYSUPPRESSIONS = --disable="C0103,C0301"
pylint:
	@pylint $(PYLINTOPTS) $(PYSUPPRESSIONS) cvssync

SOURCES = Makefile *.[ch] *.[yl] cvssync
DOCS = README COPYING NEWS AUTHORS TODO control cvs-fast-export.asc cvssync.asc hacking.asc
ALL =  $(SOURCES) $(DOCS)
cvs-fast-export-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:cvs-fast-export-$(VERSION)/:' --show-transformed-names -cvzf cvs-fast-export-$(VERSION).tar.gz $(ALL)

dist: cvs-fast-export-$(VERSION).tar.gz

release: cvs-fast-export-$(VERSION).tar.gz cvs-fast-export.html cvssync.html
	shipper version=$(VERSION) | sh -e -x
