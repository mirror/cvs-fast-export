#!/usr/bin/env python
## Test handling of executable bit

import testlifter

repo = testlifter.CVSRepository("exec.repo")
repo.init()
repo.module("module")
co = repo.checkout("module", "exec.checkout")

# Should have M 100755
co.write("exec",
	"Now is the time for all good shellscripts to come to the iid of their systems.\n")
co.add("exec")
co.outdo("chmod a+x exec")
co.commit("Committing executable file")

# Should have M 100644
co.write("nonexec",
         "The quick brown fox jumped over the lazy dog.\n")
co.add("nonexec")
co.commit("Committing nonexecutable file.")

repo.cleanup()
