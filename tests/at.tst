#!/usr/bin/env python
## Verify parsing of escaped at on final line

import sys, testlifter

testlifter.verbose += sys.argv[1:].count("-v")
repo = testlifter.CVSRepository("at.repo")
repo.init()
repo.module("module")
co = repo.checkout("module", "at.checkout")

co.write("README", "The quick brown fox jumped @t the lazy dog.")
co.add("README")
co.commit("This is a sample commit")

repo.cleanup()

# end
