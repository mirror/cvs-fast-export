#!/usr/bin/env python
## Test keyword expansion

import testlifter

repo = testlifter.CVSRepository("expand.repo")
repo.init()
repo.module("expand")
co = repo.checkout("expand", "expand.checkout")

co.write("README", "$Revision$ should expand to 1.1.\n")
co.add("README")
co.commit("This is a sample commit")

co.do("admin", "-kkv", "README")

co.write("README", "$Revision$ should expand to 1.2.\n")
co.commit("This is another sample commit")

co.write("README", "$Revision$ should expand to 1.3.\n")
co.commit("Yet another sample commit")

#repo.cleanup()

# end
