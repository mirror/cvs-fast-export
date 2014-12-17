# Test makefile for cvs-fast-export

# Set OPTS to pass options to the cvs-fast-export instances.

PATH := ${CURDIR}/..:${PATH}
SHELL = sh	# Do not introduce bashisms!
CVS = cvs
DIFF = diff -u -a
CVS_FAST_EXPORT = ../cvs-fast-export $(OPTS)

check: test

.SUFFIXES: .tst .repo .testrepo .checkout .dot .fi ,v

.tst.repo:
	python $<
.repo.checkout:
	$(CVS) -d :local:${CURDIR}/$*.repo -Q checkout module && mv module $*.checkout
.repo.dot:
	find $</module -name '*,v' | $(CVS_FAST_EXPORT) -g >$*.dot
.repo.fi:
	find $</module -name '*,v' | $(CVS_FAST_EXPORT) >$*.dot
.testrepo.checkout:
	$(CVS) -d :local:${CURDIR}/$*.testrepo -Q checkout module && mv module $*.checkout
.testrepo.dot:
	find $</module -name '*,v' | $(CVS_FAST_EXPORT) -g >$*.dot
.testrepo.fi:
	find $</module -name '*,v' | $(CVS_FAST_EXPORT) >$*.fi
,v.dot:
	$(CVS_FAST_EXPORT) -g $< >$*.dot

test: s_regress m_regress r_regress i_regress f_regress t_regress
	@echo "No diff output is good news."

rebuild: s_rebuild m_rebuild r_rebuild i_rebuild t_rebuild

testlist:
	@grep '^##' *.tst *.py

neutralize.map:
	@echo "$${USER} = foo <foo> -0500" >neutralize.map

TESTLOADS := $(shell ls -1 *.tst | sed '/.tst/s///')
TESTOPTS = -k kv -T -A neutralize.map --reposurgeon
s_rebuild: neutralize.map
	@-for file in $(TESTLOADS); do \
	    echo "Remaking $${file}.chk"; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/module -name '*,v' | $(CVS_FAST_EXPORT) $(TESTOPTS) >$${file}.chk 2>&1; \
	done;
s_regress: neutralize.map
	@echo "== Dump regressions =="
	@-for file in $(TESTLOADS); do \
	    echo -n "  $${file} "; grep '##' $${file}.tst  || echo ' ## (no description)'; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/module -name '*,v' | $(CVS_FAST_EXPORT) $(TESTOPTS) 2>&1 | $(DIFF) $${file}.chk -; \
	done

MASTERS := $(shell ls -1 *,v | sed '/,v/s///')
m_rebuild:
	@-for file in $(MASTERS); do \
	    echo "Remaking $${file}.chk"; \
	    $(CVS_FAST_EXPORT) $${file},v >$${file}.chk 2>&1; \
	done;
m_regress:
	@echo "== Master-parsing regressions =="
	@-for file in $(MASTERS); do \
	    echo -n "  $${file}: "; sed <$${file},v -n -e '/^comment	@# \(.*\)@;/s//\1/p'; \
	    $(CVS_FAST_EXPORT) $${file},v 2>&1 | $(DIFF) $${file}.chk -; \
	done

INCREMENTAL=twobranch
THRESHOLD=104000
i_rebuild: neutralize.map
	@-for file in $(INCREMENTAL); do \
	    echo "Remaking $${file}.inc-chk"; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/module -name '*,v' | $(CVS_FAST_EXPORT) -T -A neutralize.map -i $(THRESHOLD) >$${file}.inc-chk 2>&1; \
	done;
i_regress: neutralize.map
	@echo "== Incremental-dump regressions =="
	@-for file in $(INCREMENTAL); do \
	    echo -n "  $${file} "; grep '##' $${file}.tst  || echo ' ## (no description)'; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/module -name '*,v' | $(CVS_FAST_EXPORT) -T -i $(THRESHOLD) -A neutralize.map 2>&1 | $(DIFF) $${file}.inc-chk -; \
	done

# Alas, this produces false failures on branchy repos because of some
# wacky nondeterminism in the git tools.  Thus we can only test
# repeatably on linear ones.  There's also some wacky non-idempotency
# in how git handles timezones.
LINEAR = basic expand hack1 hack2 hack3 longrev twotag
f_regress: neutralize.map
	@echo "== Fast format compatibility =="
	@-for file in $(LINEAR); do \
	    echo -n "  $${file} "; grep '##' $${file}.tst  || echo ' ## (no description)'; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/module -name '*,v' | $(CVS_FAST_EXPORT) -C -T -A neutralize.map 2>&1 | ./gitwash >canonical$$; \
	    find $${file}.repo/module -name '*,v' | $(CVS_FAST_EXPORT) -F -T -A neutralize.map 2>&1 | ./gitwash >fast$$; \
	    $(DIFF) canonical$$ fast$$; \
	    rm canonical$$ fast$$; \
	done

REDUCED=oldhead
r_rebuild: neutralize.map
	@-for file in $(REDUCED); do \
	    echo "Remaking $${file}.chk"; \
	    find $${file}.testrepo/module -name '*,v' | $(CVS_FAST_EXPORT) $(TESTOPTS) >$${file}.chk 2>&1; \
	done;
r_regress: neutralize.map
	@echo "== Repo regressions =="
	@-for repo in $(REDUCED); do \
	    echo -n "  $${repo} "; grep '##' $${repo}.testrepo/README  || echo ' ## (no description)'; \
	    find $${repo}.testrepo/module -name '*,v' | $(CVS_FAST_EXPORT) $(TESTOPTS) 2>&1 | $(DIFF) $${repo}.chk -; \
	done

PYTESTS=t9601 t9602 t9603 t9604 t9605
PATHSTRIP = sed -e '/\/.*tests/s//tests/'
t_regress:
	@echo "== Pathological cases =="
	@for pytest in $(PYTESTS); do \
		echo -n "  $${pytest} "; grep '##' $${pytest}.py  || echo ' ## (no description)'; \
		python $${pytest}.py 2>&1 | $(PATHSTRIP) | $(DIFF) $${pytest}.err -; \
	done
t_rebuild:
	@for pytest in $(PYTESTS); do \
		echo "Remaking $${pytest}.err "; \
		python $${pytest}.py 2>&1 | $(PATHSTRIP) >$${pytest}.err; \
	done

clean:
	rm -fr neutralize.map *.checkout *.repo *.pyc *.dot *.git *.git.fi
