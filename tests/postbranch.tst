#!/usr/bin/env python
## Ilya Basin's test, failed by cvsps-3.x
"""
Date: Sat, 20 Apr 2013 14:38:55 +0400
From: Ilya Basin <basinilya@gmail.com>
Message-ID: <1769337906.20130420143855@gmail.com>
Subject: cvsps --fast-export: a branch has no common ancestor with trunk

Hi Eric.
I think I found a bug: with option '--fast-export' cvsps produces a
stream in which the branch has no common ancestor with trunk. When
importing this with 'git fast-import', 2 branches are unrelated.

Please see the CVS repo here: https://github.com/basinilya/testcvsps
(cvs repo is inside the git repo)
Here I did:

1) create file "f"
2) commit
3) create branch "br":
  # name the branch's point of attachment
  cvs tag br_0
  # actually create the branch
  cvs tag -r br_0 -b br

4) modify f, commit in trunk
5)
  # switch to new branch
  cvs update -r br -kk

6) modify f, commit in br
7)
  # switch back to trunk
  cvs -q up -CAd -kk
8) modify f, commit in trunk

(To have both cvsps versions, I created: /usr/local/bin/cvsps -> /usr/bin/cvsps2)

The correct graph, imported with: git cvsimport -C proj2 -r cvs -k proj

    * commit 6351661c6711ebb6295c473cb1110fbb9d0513ab
    | Author: il <il>
    | Date:   Sat Apr 20 09:23:01 2013 +0000
    | 
    |     second commit in trunk
    |    
    | * commit d047058d60d57f90d404129321c809e7c91253d5
    |/  Author: il <il>
    |   Date:   Sat Apr 20 09:16:37 2013 +0000
    |   
    |       commit in br
    |  
    * commit e56d41b400a1a8cd50d7880d0278b42e960fb451
    | Author: il <il>
    | Date:   Sat Apr 20 09:15:21 2013 +0000
    | 
    |     commit in trunk
    |  
    * commit 4374f4dcb6125dae25cc630a7b81149595621d6d
      Author: il <il>
      Date:   Sat Apr 20 09:11:34 2013 +0000
      
          root

Wrong graph, imported with: /usr/bin/cvsps --fast-export proj | git fast-import

    * commit 6351661c6711ebb6295c473cb1110fbb9d0513ab
    | Author: il <il>
    | Date:   Sat Apr 20 09:23:01 2013 +0000
    | 
    |     second commit in trunk
    |  
    * commit e56d41b400a1a8cd50d7880d0278b42e960fb451
    | Author: il <il>
    | Date:   Sat Apr 20 09:15:21 2013 +0000
    | 
    |     commit in trunk
    |  
    * commit 4374f4dcb6125dae25cc630a7b81149595621d6d
      Author: il <il>
      Date:   Sat Apr 20 09:11:34 2013 +0000
      
          root
      
    * commit edb5d8ac84296d376aada841f4c45e05ba8db1d8
      Author: il <il>
      Date:   Sat Apr 20 09:16:37 2013 +0000
      
          commit in br
"""

import testlifter

repo = testlifter.CVSRepository("postbranch.repo")
repo.init()
repo.module("module")
co = repo.checkout("module", "postbranch.checkout")

# 1) create file "f"
co.write("f", "random content")
co.add("f")

# 2) commit
co.commit("root")

# 3) create branch "br":
# name the branch's point of attachment
co.outdo("cvs -Q tag br_0")
# actually create the branch
co.outdo("cvs -Q tag -r br_0 -b br")

# 4) modify f, commit in trunk
co.write("f", "different random content")
co.commit("commit in trunk")

# 5) switch to new branch
co.outdo("cvs -Q update -r br -kk")

# 6) modify f, commit in br
co.write("f", "even more random content")
co.commit("commit in br")

# 7) switch back to trunk
co.outdo("cvs -Q -q up -CAd -kk")

# 8) modify f, commit in trunk
co.write("f", "even more different random content")
co.commit("second commit in trunk")

repo.cleanup()

# end
