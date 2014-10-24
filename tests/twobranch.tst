#!/usr/bin/env python
## Two-branch repo to test incremental dumping

import testlifter

repo = testlifter.CVSRepository("twobranch.repo")
repo.init()
repo.module("module")
co = repo.checkout("module", "twobranch.checkout")

co.write("README", "The quick brown fox jumped over the lazy dog.\n")
co.add("README")
co.commit("This is a sample commit")

co.branch("samplebranch")

co.write("README",
         "Now is the time for all good men to come to the aid of their country.\n")
co.commit("This is another sample commit")

co.switch("HEAD")

co.write("README",
         "And now for something completely different.\n")
co.commit("The obligatory Monty Python reference")

co.switch("samplebranch")

co.write("README", "This is random content for README.\n")
co.commit(r"We will put the dump theshold before this commit.")

co.switch("HEAD")

co.write("README", "I'm back in the saddle again.\n")
co.commit("This commit should alter the master branch.")

repo.cleanup()

# end
