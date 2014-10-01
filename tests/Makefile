# Test makefile for cvs-fast-export

SHELL = sh	# Do not introduce bashisms!
srcdir = $(dir $(abspath $(firstword $(MAKEFILE_LIST))))
VPATH = $(srcdir)
CVS = cvs
DIFF = diff -u -a
export PYTHONPATH = $(srcdir)
export CVS_FAST_EXPORT = ../cvs-fast-export

%.repo/: %.tst
	python $<
%.checkout/: %.repo/
	$(CVS) -d :local:$(abspath $<) -Q checkout -d $@

test: s_regress m_regress i_regress t_regress
	@echo "No diff output is good news."

check: test

rebuild: s_rebuild m_rebuild i_rebuild t_rebuild

TESTLOADS := basic blankbranch expand hack1 hack2 hack3 
TESTLOADS += longrev postbranch twobranch twotag 

testlist: $(TESTLOADS:=.tst) $(PYTESTS:=.py)
	@grep '^##' $^ 

neutralize.map:
	echo "$$USER = foo <foo> -0500" >$@

.SECONDARY: $(TESTLOADS:=.repo)
# Test regressions
s_rebuild: $(TESTLOADS:=.schk)
%.schk: %.repo/ neutralize.map
	-find $*.repo/$* -name '*,v' | sort | \
	  $(CVS_FAST_EXPORT) -T -A neutralize.map >$@ 2>&1
	$(CP) $@ $(srcdir)$*.chk

s_regress: $(TESTLOADS:=.sreg)
%.sreg: %.tst %.repo/ neutralize.map
	@echo -n "  $* "
	@grep '##' $< || echo ' ## (no description)'
	find $*.repo/$* -name '*,v' | sort | \
	   $(CVS_FAST_EXPORT) -T -A neutralize.map 2>&1 | \
	   $(DIFF) $(srcdir)$*.chk -


# Master-parsing regressions
MASTERS := hardlinks hashsymbol
m_rebuild: $(MASTERS:=.mchk)
%.mchk: %,v
	-$(CVS_FAST_EXPORT) $< >$@ 2>&1
	$(CP) $@ $(srcdir)$*.chk
m_regress: $(MASTERS:=.mreg)
%.mreg: %,v
	@echo -n "  $*: "
	@sed <$< -n -e '/^comment	@# \(.*\)@;/s//\1/p'
	$(CVS_FAST_EXPORT) $< 2>&1 | \
	    $(DIFF) $(srcdir)$*.chk -


INCREMENTAL=twobranch
THRESHOLD=4000
.SECONDARY: $(INCREMENTAL:=.repo)
i_rebuild: $(INCREMENTAL:=.ichk)
%.ichk: %.repo/ neutralize.map
	-find $*.repo/$* -name '*,v' | sort | \
	 $(CVS_FAST_EXPORT) -T -A neutralize.map \
	     -i $(THRESHOLD) >$@ 2>&1
	$(CP) $@ $(srcdir)$*.inc-chk

i_regress: $(INCREMENTAL:=.ireg)
%.ireg: %.tst %.repo/ neutralize.map
	@echo -n "  $* "
	@grep '##' $< || echo ' ## (no description)'
	find $*.repo/$* -name '*,v' | sort | \
	  $(CVS_FAST_EXPORT) -T -A neutralize.map \
	      -i $(THRESHOLD) 2>&1 | \
	      $(DIFF) $(srcdir)$*.inc-chk -

PYTESTS=t9601 t9602 t9603 t9604 t9605
PATHSTRIP := sed -e 's!$(shell pwd)/!tests/!g;s!$(srcdir)!tests/!g'
t_rebuild: $(PYTESTS:=.pchk)
%.pchk: %.py
	-python $^ -s$(srcdir) 2>&1 | $(PATHSTRIP) >$@
	$(CP) $@ $(srcdir)$*.err
t_regress:  $(PYTESTS:=.preg)
%.preg: %.py
	@echo -n "  $* "
	@grep '##' $< || echo ' ## (no description)'
	python $^ -s$(srcdir) 2>&1 | \
	    $(PATHSTRIP) | $(DIFF) $(srcdir)$*.err -

clean:
	$(RM) neutralize.map *.pyc
	$(RM) -r *.checkout *.repo
realclean: clean
	$(RM) *.pchk *.ichk *.schk *.mchk
