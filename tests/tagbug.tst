#!/usr/bin/env python
## Tricky tag corner case

import sys, testlifter

testlifter.verbose += sys.argv[1:].count("-v")
repo = testlifter.CVSRepository("tagbug.repo")
repo.init()
repo.module("module")
co = repo.checkout("module", "tagbug.checkout")

co.write("foo.c", "The quick brown fox jumped over the lazy dog.\n")
co.add("foo.c")
co.write("bar.c", "Not an obfuscated C contest entry.\n")
co.add("bar.c")
co.commit("First commit")

co.remove("bar.c")
co.commit("Second commit")

co.tag("tag")

repo.cleanup()

# end
