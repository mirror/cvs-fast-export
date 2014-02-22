#!/usr/bin/env python=python2 python=pypy python=python sh#[bdy]ash|mksh!ksh
                  # 2014 G. Nixon. Public domain, or license of your choice.

alias python=$python # See env line.
[ -e "$(dirname "$0")/../cvs-fast-export" ]  &&
alias fefi="$(dirname "$0")/../cvs-fast-export" || alias fefi=cvs-fast-export

# `find foo | fefifo > fum;` ## I smell the blood of a revision control sys'm.
rcsfind(){ find "$file.repo/$file" ;}; fefifo(){ rcsfind | fefi -TA a.map  ;}

# Pretty/safe printing. Don't like it? Disable and printf. Just no echo -e|n.
print="command printf"; print(){ $print "$@\n"   ;}; printf(){ $print "$@" ;}
underline(){ $print "\033[4m$@\033[0m";}; red(){ $print "\033[33m$@\033[0m";}
italic(){ $print "\033[3m$@\033[0m" ;};  bold(){ $print "\033[1m$@\033[0m" ;}

git --version >/dev/null 2>&1 && [ -$NOCLEAN- = -- ] && rm -rf * &&
git checkout -- . ||  rm -rf *.map *.checkout *.repo *.pyc *.git */*.git
print "$(whoami) = foo <foo> -0500" > neutralize.map; ln neutralize.map a.map
limens=4000                 # c.f., "threshold", as "netralize.map" ...fancy?
suffixations=$(find * -depth 0 | sed 's%.*[\.,]%%' | sort -ru) # ugh, enough.
sfx=$(for s in tst inc-chk v py; do printf "$suffixations"|grep $s ||:; done)

difftool='git --no-pager diff'
# The issue with using `diff` to diff diffs on repository checkouts while
# within a repository checkout is that one can't tell if the difference in
# the diff is from the diff *of* the diffs, or the diff *being* diffed. If
# the diff reproduces errors or warnings, one doesn't know if the error seen
# is: the warning/error being reproduced, or the one that's occurring now.

# Consider:
#   - We're not producing patches with this output. The important part here
#     is conveying semantically meaningful divergence at any level of
#     detail, not strict reproducibility. Therefore use wrapped, colorized word
#     diffs, without "context" (which doesn't contextualize anything here).

#   - We're already in a version-controlled repository[1]. Therefore its not
#     necessary to have two mechanisms for diff production and testing.
#     Just use the same functionality one would for "rebuilding reference
#     diffs" to test: if the version control system reports a file change,
#     its an error. If not, pass. A "reference diff" is simply one that's
#     been committed.

#   - Realize that the escape sequences here don't "corrupt" the diff: they
#     are, in fact, e basically the only thing making the BRE's safe. I'm
#     not a super regex whiz, so presently there are probably errors, and
#     I'm sure it could be more efficient. But the technique shout be sound,
#     and display anomalies here will should never affect diff *production*,
#     that is, instantiate false positives where no errors exist.

#     Aside: I also don't know to what extent this is affected by terminfo;
#     nonetheless: I think this really should settle the question of whether
#     color in the shell is "necessary". Yes. At times, it is necessary.

# export TERM=xterm-256color # could help with display anomalies, possibly.

difflags='-U0 --histogram --relative --word-diff --color=always . | fold -s'
diffread='cat -v | sed -e "s|\^\[\[|\^\[|g" -e "s|^\[|\^\[|" | tr "^" "\033"'

# From this point onward, we're operating on the escaped, colorized diff.
notcruft='sed -e "s|^....index .*$||" -e "s|^....+++ .*$||" -e "s| b/.*||"'
bigerror='-e "s|diff --git a/|Error: difference in +-+-+-+-+-+-+-+-+-+-+-> |"'
noblanks="-e '/^\s*$/d' -e 's|^\(....\)--- a/\(.*\).*|\1\2:|'"
diffgood="$difftool $difflags | $diffread | $notcruft $bigerror $noblanks"

# [1] Yes, this effectly introduces a git dependency.

# However:
#  - It replaces an implicit system-specific shell and diff utility dependency
#  - Any VCS should work if you can't use git, and
#  - on releases without gitfiles, just init and commit before executing.
#  - Seriously, what are you doing executing the regression tests for a
#    utility with the single purpose of creating fast-import streams on a
#    system without git? Are you lost? If you *have* to do this,
#    (mayba on a buildbot), use checksums, not diffs:

#  sha(){ for sha1 in shasum sha1sum; do $sha1 /dev/null >/dev/null &&
#  $sha1 "$@" && break; done || return 1 ;}; difftool=sha           ## ...etc.

group(){ case $suffix in

    v)       group='Master-parsing regressions'                           ;;
    err)     group='Pathological cases'                                   ;;
    tst)     group='Dump regressions'                                     ;;
    inc-chk) group='Incremental-dump regressions'                         ;;

    '*') continue                                                         ;;

  esac && bold "\n  $group:\n" ||:
}

ass(){ essment=$1; file=$2; here=$2$dot$suffix;

  [ -"$file"- = -"$suffix"- ] || [ -$file- = -testloader- ] && return

  tag="$(grep -q '##' $here && grep '##' $here | sed 's|#||'            ||
         grep -q '@#' $here && grep '@#' $here | sed 's|.*@\(.*\)@;|\1|'||:)"

  case $essment in
    c) case $suffix in

      v)       fefi $here                                > $file.chk 2>&1 ;;
      py)      python $here 2>&1 | sed "s|$PWD/||g" 2>&1 > $file.err 2>&1 ;;
      tst)     python $here && fefifo                    > $file.chk 2>&1 ;;
      inc-chk) rcsfind | fefi -TA a.map -i $limens   > $file.inc-chk 2>&1 ;;

      '*') continue ;; esac                                               ;;

    tag) printf '    ' >&2; printf "$file\t" >&2; italic "$tag\n" >&2     ;;
    '*') continue                                                         ;;

  esac ||:
}

for suffix in $sfx; do [ -"$suffix"- = -"v"- ] && dot=',' || dot='.'; group
  files=$(find *$suffix -depth 0 ! -name test\* | sed 's%[\.,].*%%' | sort -u)
    for file in $files; do ass tag "$file" && ass c "$file" && wait ||:
  done
done

face(){ case "$1" in music) [ -"$(git status -s)"- = -""- ] && echo       &&
     italic "\n  All tests passed.\n\n" && exit || echo && eval $diffgood &&
     bold "\033[33m\nTest failure summary:\n\n\033[0m" && git status -s   &&
     echo &&                                                       exit 1 ;;
  esac
}

face music
