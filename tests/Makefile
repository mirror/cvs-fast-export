all:       check

check:     test

clean:
	@rm -rf *.map *.checkout *.repo *.pyc *.git*
	@git checkout -- .||:

distclean: clean

test:
	@./test.sh
	git checkout -q -- .||:
