#!/usr/bin/env python
## A repo with identical tags attached to different changesets

import testlifter, time

repo = testlifter.RCSRepository("twotag.repo")
repo.init()

repo.add("tweedledum")
repo.add("tweedledee")

# These two file commits should form a clique
repo.checkout("tweedledum")
repo.checkout("tweedledee")
repo.write("tweedledum", "The quick brown fox jumped over the lazy dog.\n")
repo.write("tweedledee", "Alve bazige froulju wachtsje op dyn komst.\n")
repo.checkin("tweedledum", "An example first checkin") 
repo.checkin("tweedledee", "An example first checkin") 
repo.tag("tweedledum", "FUBAR")

# Without this, where the tag is finally assigned might be random,
# because the two commit cliques could have the same timestamp. In that
# case, when cvs-fast-export is using a threaded scheduler, the arrival
# order of the two commits qwill be random and so will the impputed
# order of the tags.
#
# Yes, this is a coward's way out.  It willhave to do until we invent a
# way to total-order the tags.
#
time.sleep(1)

# These two file checkins should also form a clique
repo.checkout("tweedledum")
repo.checkout("tweedledee")
repo.write("tweedledum", "Portez ce vieux whisky au juge blond qui fume.\n")
repo.write("tweedledee", "Lynx c.q. vos prikt bh: dag zwemjuf!.\n")
repo.checkin("tweedledum", "An example second checkin") 
repo.checkin("tweedledee", "An example second checkin") 
repo.tag("tweedledee", "FUBAR")

# end
