# Makefile for cvs-fast-export
#
# Build requirements: A C compiler, bison, flex, and asciidoc.
# For blob compression you will also need zlib.
# For documentation, you will need asciidoc, xsltproc and docbook stylesheets.
# The test suite requires Python 2.6, and you will see some meaningless failures
# with git 1.7.1 and older.

VERSION=1.22

prefix?=/usr/local
target=$(DESTDIR)$(prefix)
srcdir=$(dir $(abspath $(firstword $(MAKEFILE_LIST))))
VPATH=$(srcdir)

INSTALL = install

GCC_WARNINGS1=-Wall -Wpointer-arith -Wstrict-prototypes
GCC_WARNINGS2=-Wmissing-prototypes -Wmissing-declarations
GCC_WARNINGS3=-Wno-unused-function -Wno-unused-label -Wno-format-zero-length
GCC_WARNINGS=$(GCC_WARNINGS1) $(GCC_WARNINGS2) $(GCC_WARNINGS3)
CFLAGS=$(GCC_WARNINGS)
CPPFLAGS += -I. -I$(srcdir)
LIBS=-lrt
CPPFLAGS += -DVERSION=\"$(VERSION)\"

# Enable this for multithreading.
CFLAGS += -pthread
CPPFLAGS += -DTHREADS

# Optimizing for speed. Comment this out for distribution builds
CFLAGS += -march=native

# To enable debugging of the Yacc grammar, uncomment the following line
#CPPFLAGS += -DYYDEBUG=1
# To enable debugging of blob export, uncomment the following line
#CPPFLAGS += -DFDEBUG=1
# To enable assertions of red black trees, uncomment the following line
#CPPFLAGS += -DRBDEBUG=1
# To enable debugging of order instability issues
#CPPFLAGS += -DORDERDEBUG=1

# Condition in various optimization hacks.  You almost certainly
# don't want to turn any of these off; the condition symbols are
# present more as documentation of the program structure than
# anything else
CPPFLAGS += -DREDBLACK # Use red-black trees for faster symbol lookup
CPPFLAGS += -DUSE_MMAP # Use mmap for reading CVS masters
CPPFLAGS += -DMEMOSORT # Memoized fast path sorting
CPPFLAGS += -DBLOOMSET # Use Bloom lookup to speed up export

# First line works for GNU C.  
# Replace with the next if your compiler doesn't support C99 restrict qualifier
CPPFLAGS+=-Drestrict=__restrict__
#CPPFLAGS+=-Drestrict=""

# To enable profiling, uncomment the following line
# Note: the profiler gets confused if you don't also turn off -O flags.
# CFLAGS += -pg
# CFLAGS += -O3
CFLAGS += -g
CFLAGS += $(EXTRA_CFLAGS)

YFLAGS=
LFLAGS=

OBJS=gram.o lex.o rbtree.o main.o import.o dump.o cvsnumber.o \
	cvsutil.o revdir.o revlist.o atom.o revcvs.o generate.o export.o \
	nodehash.o tags.o authormap.o graph.o utils.o merge.o memosort.o

cvs-fast-export: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

$(OBJS): cvs.h
revcvs.o cvsutils.o rbtree.o: rbtree.h
memosort.o: uthash.h
export.o: uthash.h

BISON ?= bison

gram.h gram.c: gram.y
	$(BISON) --defines=gram.h --output-file=gram.c $(YFLAGS) $<
lex.h lex.c: lex.l
	flex $(LFLAGS) --header-file=lex.h --outfile=lex.c $<

gram.o: gram.c lex.h gram.h
import.o: import.c lex.h gram.h
lex.o: lex.c gram.h

.SUFFIXES: .html .asc .txt .1

# Requires asciidoc and xsltproc/docbook stylesheets.
.asc.1:
	a2x --doctype manpage --format manpage -D . $<
.asc.html:
	a2x --doctype manpage --format xhtml -D . $<
	rm -f docbook-xsl.css

man: cvssync.1 cvs-fast-export.1

html: cvssync.1 cvs-fast-export.1

clean:
	rm -f $(OBJS) gram.h gram.c lex.h lex.c cvs-fast-export docbook-xsl.css
	rm -f cvs-fast-export.1 cvs-fast-export.html
	rm -f cvssync.1 cvssync.html PROFILE gmon.out
	rm -f MANIFEST index.html *.tar.gz docbook-xsl.css

check: cvs-fast-export
	@[ -d tests ] || mkdir tests
	$(MAKE) -C tests -s -f $(srcdir)tests/Makefile

install: install-bin install-man
install-bin: cvs-fast-export cvssync
	$(INSTALL) -d "$(target)/bin"
	$(INSTALL) $^ "$(target)/bin"
install-man: man
	$(INSTALL) -d "$(target)/share/man/man1"
	$(INSTALL) -m 644 cvs-fast-export.1 "$(target)/share/man/man1"
	$(INSTALL) -m 644 cvssync.1 "$(target)/share/man/man1"

PROFILE_REPO = ~/software/groff-conversion/groff-mirror/groff
gmon.out: cvs-fast-export
	find $(PROFILE_REPO) -name '*,v' | cvs-fast-export -k -p >/dev/null
PROFILE: gmon.out
	gprof cvs-fast-export >PROFILE


# Weird suppressions are required because of strange tricks in Bison and Flex.
CSUPPRESSIONS = -U__UNUSED__ -UYYPARSE_PARAM -UYYTYPE_INT16 -UYYTYPE_INT8 \
	-UYYTYPE_UINT16 -UYYTYPE_UINT8 -UYY_USER_INIT -UYY_READ_BUF_SIZE \
	-UYY_NO_INPUT -UECHO -UYY_START_STACK_INCR -UYY_FATAL_ERROR \
	-Ushort -Usize_t -Uyytext_ptr -Uyyoverflow -U__cplusplus
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
ALL =  $(SOURCES) $(DOCS) tests
cvs-fast-export-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:cvs-fast-export-$(VERSION)/:' --show-transformed-names -cvzf cvs-fast-export-$(VERSION).tar.gz $(ALL)

dist: cvs-fast-export-$(VERSION).tar.gz

release: cvs-fast-export-$(VERSION).tar.gz cvs-fast-export.html cvssync.html
	shipper version=$(VERSION) | sh -e -x
