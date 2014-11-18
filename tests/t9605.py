#!/usr/bin/env python
## Testing for correct patchset estimation

# Structure of the test cvs repository
#
#Import of a trivial CVS repository fails due to a cvsps bug.  Given the
#following series of commits:
#
#    timestamp             a    b    c   message
#    -------------------  ---  ---  ---  -------
#    2012/12/12 21:09:39  1.1            changes are done
#    2012/12/12 21:09:44            1.1  changes
#    2012/12/12 21:09:46            1.2  changes
#    2012/12/12 21:09:50       1.1  1.3  changes are done
#
#cvsps mangles the commit ordering (edited for brevity):
#
#    ---------------------
#    PatchSet 1
#    Date: 2012/12/12 15:09:39
#    Log:
#    changes are done
#
#    Members:
#        a:INITIAL->1.1
#        b:INITIAL->1.1
#        c:1.2->1.3
#
#    ---------------------
#    PatchSet 2
#    Date: 2012/12/12 15:09:44
#    Log:
#    changes
#
#    Members:
#        c:INITIAL->1.1
#
#    ---------------------
#    PatchSet 3
#    Date: 2012/12/12 15:09:46
#    Log:
#    changes
#
#    Members:
#        c:1.1->1.2
#
#This is seen in cvsps versions 2.x and up through at least 3.7.
#
# Reported by Chris Rorvick.

import sys, testlifter

cc = testlifter.ConvertComparison(stem="t9605", module="module")
cc.repo.retain = ("-k" in sys.argv[1:])
cc.compare_tree("branch", "master", True)
cc.cleanup()
