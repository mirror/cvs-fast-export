# Test makefile for cvs-fast-export

PATH := ${PWD}/..:${PATH}


.SUFFIXES: .tst .repo .checkout

.tst.repo:
	python $<
.repo.checkout:
	cvs -d :local:${PWD}/$*.repo -Q checkout $* && mv $* $*.checkout

test: s_regress i_regress t_regress
	@echo "No diff output is good news."

check: test

rebuild: s_rebuild i_rebuild t_rebuild

testlist:
	@grep '^##' *.tst *.py

neutralize.map:
	@echo "$${USER} = foo <foo> -0500" >neutralize.map

TESTLOADS := $(shell ls -1 *.tst | sed '/.tst/s///')
s_rebuild: neutralize.map
	@-for file in $(TESTLOADS); do \
	    echo "Remaking $${file}.chk"; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/$${file} -name '*,v' | cvs-fast-export -T -A neutralize.map >$${file}.chk 2>&1; \
	done;
s_regress: neutralize.map
	@echo "== Dump regressions =="
	@-for file in $(TESTLOADS); do \
	    echo -n "  $${file} "; grep '##' $${file}.tst  || echo ' ## (no description)'; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/$${file} -name '*,v' | cvs-fast-export -T -A neutralize.map 2>&1 | diff -u $${file}.chk -; \
	done

INCREMENTAL=twobranch
THRESHOLD=4000
i_rebuild: neutralize.map
	@-for file in $(INCREMENTAL); do \
	    echo "Remaking $${file}.inc-chk"; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/$${file} -name '*,v' | cvs-fast-export -T -A neutralize.map -i $(THRESHOLD) >$${file}.inc-chk 2>&1; \
	done;
i_regress: neutralize.map
	@echo "== Incremental-dump regressions =="
	@-for file in $(INCREMENTAL); do \
	    echo -n "  $${file} "; grep '##' $${file}.tst  || echo ' ## (no description)'; \
	    make --quiet $${file}.repo; \
	    find $${file}.repo/$${file} -name '*,v' | cvs-fast-export -T -i $(THRESHOLD) -A neutralize.map 2>&1 | diff -u $${file}.inc-chk -; \
	done


PYTESTS=t9601 t9602 t9603 t9605
t_regress:
	@echo "== Pathological cases =="
	@for pytest in $(PYTESTS); do \
		echo -n "  $${pytest} "; grep '##' $${pytest}.py  || echo ' ## (no description)'; \
		python $${pytest}.py 2>&1 | diff $${pytest}.err -; \
	done
t_rebuild:
	@for pytest in $(PYTESTS); do \
		echo "Remaking $${pytest}.err "; \
		python $${pytest}.py >$${pytest}.err 2>&1; \
	done

clean:
	rm -fr neutralize.map *.checkout *.repo *.pyc *.git.fi
