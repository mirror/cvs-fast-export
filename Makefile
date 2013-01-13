# Makefile for cvs-fast-export
#
# Build requirements: A C compiler, yacc, lex, and asciidoc.

VERSION=0.2

GCC_WARNINGS1=-Wall -Wpointer-arith -Wstrict-prototypes
GCC_WARNINGS2=-Wmissing-prototypes -Wmissing-declarations
GCC_WARNINGS3=-Wno-unused-function -Wno-unused-label
GCC_WARNINGS=$(GCC_WARNINGS1) $(GCC_WARNINGS2) $(GCC_WARNINGS3)
CFLAGS=-O2 -g $(GCC_WARNINGS) -DVERSION=\"$(VERSION)\"

# To enable debugging of the Yacc grammar, uncomment the following line
#CFLAGS += -DYYDEBUG=1

YFLAGS=-d -l
LFLAGS=-l

OBJS=gram.o lex.o main.o cvsutil.o revdir.o \
	revlist.o atom.o revcvs.o generate.o export.o \
	nodehash.o tags.o authormap.o graph.o

cvs-fast-export: $(OBJS)
	cc $(CFLAGS) -o $@ $(OBJS)

$(OBJS): cvs.h
lex.o: y.tab.h

lex.o: lex.c

y.tab.h: gram.c

.SUFFIXES: .html .asc .txt .1

# Requires asciidoc
.asc.1:
	a2x --doctype manpage --format manpage $*.asc
.asc.html:
	a2x --doctype manpage --format xhtml $*.asc

clean:
	rm -f $(OBJS) y.tab.h gram.c lex.c cvs-fast-export docbook-xsl.css
install:
	cp cvs-fast-export ${HOME}/bin

cppcheck:
	cppcheck --template gcc --enable=all -UUNUSED --suppress=unusedStructMember *.[ch]

SOURCES = Makefile *.[ch]
DOCS = README COPYING NEWS AUTHORS cvs-fast-export.asc
ALL =  $(SOURCES) $(DOCS)
cvs-fast-export-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:cvs-fast-export-$(VERSION)/:' --show-transformed-names -cvzf cvs-fast-export-$(VERSION).tar.gz $(ALL)

dist: cvs-fast-export-$(VERSION).tar.gz

release: cvs-fast-export-$(VERSION).tar.gz cvs-fast-export.html
	shipper -u -m -t; make clean; rm SHIPPER.FREECODE
