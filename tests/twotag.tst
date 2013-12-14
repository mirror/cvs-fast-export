#!/usr/bin/env python
## A repo with identical tags attached to different changesets

import testlifter

repo = testlifter.RCSRepository("twotag")
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

# These two file checkins should also form a clique
repo.checkout("tweedledum")
repo.checkout("tweedledee")
repo.write("tweedledum", "Portez ce vieux whisky au juge blond qui fume.\n")
repo.write("tweedledee", "Lynx c.q. vos prikt bh: dag zwemjuf!.\n")
repo.checkin("tweedledum", "An example second checkin") 
repo.checkin("tweedledee", "An example second checkin") 
repo.tag("tweedledee", "FUBAR")

# end
