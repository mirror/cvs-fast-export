#!/usr/bin/env python
## Testing for correct timestamp handling in author maps.
import sys, testlifter, tempfile, os

uncorrected = """\
Rev 16 2006-10-29 07:00:01 +0000
Rev 15 2006-10-29 06:59:59 +0000
Rev 14 2006-04-02 08:00:01 +0000
Rev 13 2006-04-02 07:59:59 +0000
Rev 12 2005-12-01 00:00:00 +0000
Rev 11 2005-11-01 00:00:00 +0000
Rev 10 2005-10-01 00:00:00 +0000
Rev  9 2005-09-01 00:00:00 +0000
Rev  8 2005-08-01 00:00:00 +0000
Rev  7 2005-07-01 00:00:00 +0000
Rev  6 2005-06-01 00:00:00 +0000
Rev  5 2005-05-01 00:00:00 +0000
Rev  4 2005-04-01 00:00:00 +0000
Rev  3 2005-03-01 00:00:00 +0000
Rev  2 2005-02-01 00:00:00 +0000
Rev  1 2005-01-01 00:00:00 +0000
"""

cc = testlifter.ConvertComparison(stem="t9604", module="module")
cc.repo.retain = ("-k" in sys.argv[1:])
cc.compare_tree("branch", "master", True)
cc.command_returns("cd t9604-git >/dev/null; git log --format='%s %ai'", uncorrected)
cc.cleanup()

authormap = """\
user1=User One <user1@domain.org>
user2=User Two <user2@domain.org> CST6CDT
user3=User Three <user3@domain.org> EST5EDT
user4=User Four <user4@domain.org> MST7MDT
"""

corrected="""\
Rev 16 2006-10-29 01:00:01 -0600 User Two
Rev 15 2006-10-29 01:59:59 -0500 User Two
Rev 14 2006-04-02 03:00:01 -0500 User Two
Rev 13 2006-04-02 01:59:59 -0600 User Two
Rev 12 2005-11-30 17:00:00 -0700 User Four
Rev 11 2005-10-31 19:00:00 -0500 User Three
Rev 10 2005-09-30 19:00:00 -0500 User Two
Rev  9 2005-09-01 00:00:00 +0000 User One
Rev  8 2005-07-31 18:00:00 -0600 User Four
Rev  7 2005-06-30 20:00:00 -0400 User Three
Rev  6 2005-05-31 19:00:00 -0500 User Two
Rev  5 2005-05-01 00:00:00 +0000 User One
Rev  4 2005-03-31 17:00:00 -0700 User Four
Rev  3 2005-02-28 19:00:00 -0500 User Three
Rev  2 2005-01-31 18:00:00 -0600 User Two
Rev  1 2005-01-01 00:00:00 +0000 User One
"""

afp = open(tempfile.mktemp(), "w")
afp.write(authormap)
afp.flush()
cc = testlifter.ConvertComparison(stem="t9604", module="module",
                                  options="-A %s" % afp.name)
cc.repo.retain = ("-k" in sys.argv[1:])
cc.compare_tree("branch", "master", True)
cc.command_returns("cd t9604-git >/dev/null; git log --format='%s %ai %an'", corrected)
os.remove(afp.name)
afp.close()
cc.cleanup()

