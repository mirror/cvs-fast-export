#!/usr/bin/env python
## A widely branched repo with long file revision strings.

import testlifter

repo = testlifter.CVSRepository("longrev.repo")
repo.init()
repo.module("longrev")
co = repo.checkout("longrev", "longrev.checkout")

co.write("README", "A test of multiple tags.\n")
co.add("README")
co.commit("Initial revision")

for i in range(16):
    branchname = ("branch%s" % (i+1))
    co.branch( branchname )
    co.switch( branchname )

    co.write("README", branchname)
    co.commit("Updated for " + branchname)

repo.cleanup()
