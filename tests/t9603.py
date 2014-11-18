#!/usr/bin/env python
## Testing for correct patchset estimation

# Structure of the test cvs repository
#
# Message   File:Content         Commit Time
# Rev 1     a: 1.1               2009-02-21 19:11:43 +0100
# Rev 2     a: 1.2    b: 1.1     2009-02-21 19:11:14 +0100
# Rev 3               b: 1.2     2009-02-21 19:11:43 +0100
#
# As you can see the commit of Rev 3 has the same time as
# Rev 1 this leads to a broken import because of a cvsps
# bug.

import sys, testlifter

cc = testlifter.ConvertComparison(stem="t9603", module="module")
cc.repo.retain = ("-k" in sys.argv[1:])
cc.compare_tree("branch", "master", True)
cc.cleanup()
