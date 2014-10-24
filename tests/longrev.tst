#!/usr/bin/env python
## A widely branched repo with long file revision strings.

import testlifter

repo = testlifter.CVSRepository("longrev.repo")
repo.init()
repo.module("module")
co = repo.checkout("module", "longrev.checkout")

co.write("README", "A test of multiple tags.\n")
co.add("README")
co.commit("Initial revision")

for i in range(10):
    branchname = ("branch%s" % (i+1))
    co.branch( branchname )
    co.switch( branchname )

    co.write("README", branchname)
    co.commit("Updated for " + branchname)

repo.cleanup()
