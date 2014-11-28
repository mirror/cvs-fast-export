"""
Test framework for cvs-fast-export.
"""
import sys, os, shutil, subprocess, time, filecmp

DEBUG_STEPS    = 1
DEBUG_COMMANDS = 2
DEBUG_VCS      = 3
DEBUG_LIFTER   = 4

verbose = 0

os.putenv("PATH", os.getenv("PATH") + os.pathsep + "..") 

def noisy_run(dcmd, legend=""):
    "Either execute a command or raise a fatal exception."
    if legend:
        legend = " "  + legend
    caller = os.path.basename(sys.argv[0])
    if verbose >= DEBUG_COMMANDS:
        sys.stdout.write("%s: executing '%s'%s\n" % (caller, dcmd, legend))
    try:
        retcode = subprocess.call(dcmd, shell=True)
        if retcode < 0:
            sys.stderr.write("%s: %s was terminated by signal %d.\n" % (-caller, dcmd, retcode))
            sys.exit(1)
        elif retcode != 0:
            sys.stderr.write("%s: %s returned %d.\n" % (caller, dcmd, retcode))
            return False
    except (OSError, IOError) as e:
        sys.stderr.write("%s: execution of %s%s failed: %s\n" % (caller, dcmd, legend, e))
        return False
    return True

def capture_or_die(dcmd, legend=""):
    "Either execute a command and capture its output or die."
    if legend:
        legend = " "  + legend
    caller = os.path.basename(sys.argv[0])
    if verbose >= DEBUG_COMMANDS:
        sys.stdout.write("%s: executing '%s'%s\n" % (caller, dcmd, legend))
    try:
        return subprocess.Popen(dcmd, shell=True, stdout=subprocess.PIPE).communicate()[0]
    except subprocess.CalledProcessError as e:
        if e.returncode < 0:
            sys.stderr.write("%s: child was terminated by signal %d." % (caller, -e.returncode))
        elif e.returncode != 0:
            sys.stderr.write("%s: child returned %d." % (caller, e.returncode))
        sys.exit(1)
    
class directory_context:
    def __init__(self, target):
        self.target = target
        self.source = None
    def __enter__(self):
        if verbose >= DEBUG_COMMANDS:
            sys.stdout.write("In %s: " % os.path.relpath(self.target))
        self.source = os.getcwd()
        if os.path.isdir(self.target):
            os.chdir(self.target)
    def __exit__(self, extype, value_unused, traceback_unused):
        os.chdir(self.source)

class RCSRepository:
    "An RCS file collection."
    def __init__(self, name):
        self.name = name
        self.retain = False
        global verbose
        verbose += sys.argv[1:].count("-v")
        # For convenience, emulate the module structure of a CVS repository
        self.directory = os.path.join(os.getcwd(), self.name, "module")
        self.conversions = []
    def run_with_cleanup(self, cmd):
        if not noisy_run(cmd):
            if not self.retain:
                self.cleanup()
            sys.exit(1)
    def do(self, cmd, *args):
        "Execute a RCS command in context of this repo."
        if verbose < DEBUG_VCS:
            mute = '-q'
        else:
            mute = ""
        if not noisy_run("cd %s && %s %s %s" % (self.directory, cmd, mute, " ".join(args))) and not self.retain:
            self.cleanup()
            sys.exit(1)
    def init(self):
        "Initialize the repository."
        self.run_with_cleanup("rm -fr {0} && mkdir -p {0}".format(self.directory))
    def write(self, fn, content):
        "Create file content in the repository."
        if verbose >= DEBUG_COMMANDS:
            sys.stdout.write("%s <- %s" % (fn, content))
        with directory_context(self.directory):
            with open(fn, "w") as fp:
                fp.write(content)
    def add(self, filename):
        "Add a file to the version-controlled set."
        self.do("rcs", "-t-", "-i", filename)
    def tag(self, filename, name):
        "Create a tag on a specified file."
        self.do("rcs", "-n" + name + ":", filename)
    def checkout(self, filename):
        "Check out a writeable copy of the specified file."
        self.do("co", "-l", filename)
    def checkin(self, filename, message):
        "Check in changes to the specified file."
        self.do("ci", "-m'%s' %s" % (message, filename))
    def stream(self, module, gitdir, outfile, more_opts=''):
        vopt = "-v " * (verbose - DEBUG_LIFTER + 1)
        # The -L is necessary to handle proxied directories. 
        self.run_with_cleanup('find -L {0} -name "*,v" | cvs-fast-export {1} {2} >{3}'.format(self.directory, vopt, more_opts, outfile))
    def convert(self, module, gitdir, more_opts=''):
        "Convert the repo."
        streamfile = "%s.git.fi" % module
        self.stream(module, gitdir, streamfile, more_opts)
        self.run_with_cleanup("rm -fr {0} && mkdir {0} && git init --quiet {0}".format(gitdir))
        self.run_with_cleanup('cat {2} | (cd {1} >/dev/null; git fast-import --quiet --done && git checkout)'.format(self.directory, gitdir, streamfile))
        self.conversions.append(gitdir)
        if not self.retain:
            os.remove(streamfile)
    def cleanup(self):
        "Clean up the repository conversions."
        if not self.retain:
            if self.conversions:
                os.system("rm -fr %s" % " ".join(self.conversions))

class CVSRepository(RCSRepository):
    def __init__(self, name):
        RCSRepository.__init__(self, name)
        self.directory = os.path.join(os.getcwd(), self.name)
        self.checkouts = []
        self.conversions = []
    def do(self, *cmd):
        "Execute a CVS command in context of this repo."
        if verbose < DEBUG_VCS:
            mute = '-Q'
        else:
            mute = ""
        self.run_with_cleanup("cvs %s -d:local:%s %s" % (mute,
                                             self.directory,
                                             " ".join(cmd)))
    def init(self):
        RCSRepository.init(self)
        self.do("init")
    def module(self, mname):
        "Create an empty module with a specified name."
        module = os.path.join(self.directory, mname)
        if verbose >= DEBUG_COMMANDS:
            sys.stdout.write("Creating module %s\n" % module)
        os.mkdir(module)
    def checkout(self, module, checkout=None):
        "Create a checkout of this repo."
        self.checkouts.append(CVSCheckout(self, module, checkout))
        return self.checkouts[-1]
    def cleanup(self):
        "Clean up the repository checkout directories."
        if not self.retain:
            RCSRepository.cleanup(self)
            for checkout in self.checkouts:
                checkout.cleanup()

class CVSCheckout:
    PROXYSUFFIX = "-proxy"
    def __init__(self, repo, module, checkout=None):
        self.repo = repo
        self.module = module or "module"
        self.checkout = checkout or module
        # Hack to get around repositories that don't have a CVSROOT & module
        self.proxied = False
        if not os.path.exists(self.repo.directory + os.sep + "CVSROOT"):
            proxy = self.repo.name + CVSCheckout.PROXYSUFFIX
            try:
                shutil.rmtree(proxy)
            except OSError:
                pass
            os.mkdir(proxy)
            os.symlink(self.repo.directory, proxy + os.sep + self.module)
            os.mkdir(proxy + os.sep + "CVSROOT")
            self.repo.name += CVSCheckout.PROXYSUFFIX
            self.repo.directory += CVSCheckout.PROXYSUFFIX
            self.proxied = True
        self.repo.do("co", self.module)
        if checkout:
            if os.path.exists(checkout):
                shutil.rmtree(checkout)
            os.rename(self.module, checkout)
        self.directory = os.path.join(os.getcwd(), self.checkout)
    def do(self, cmd, *args):
        "Execute a command in the checkout directory."
        with directory_context(self.directory):
            apply(self.repo.do, [cmd] + list(args))
    def outdo(self, cmd):
        "Execute a command in the checkout directory."
        with directory_context(self.directory):
            self.repo.run_with_cleanup(cmd)
    def add(self, *filenames):
        "Add a file to the version-controlled set."
        apply(self.do, ["add"] + list(filenames))
    def remove(self, *files):
        "Remove a file from the version-controlled set."
        apply(self.do, ["remove", "-f"] + list(files))
    def branch(self, branchname):
        "Create a new branch."
        self.do("tag", branchname + "_root")
        self.do("tag", "-r", branchname + "_root", "-b", branchname)
        self.do("up", "-r", branchname)
    def switch(self, branch="HEAD"):
        "Switch to an existing branch."
        self.do("up", "-A")
        if branch != "HEAD":
            self.do("up", "-r", branch)
    def tag(self, name):
        "Create a tag."
        self.do("tag", name)
    def merge(self, branchname):
        "Merge a branch to trunk."
        # See https://kb.wisc.edu/middleware/page.php?id=4087
        self.do("tag", "merge_" + branchname)
        self.do("up", "-A")
        self.do("up", "-j", branchname)
    def commit(self, message):
        "Commit changes to the repository."
        # The CVS tools weren't designed to be called in rapid-fire
        # succession by scripts; they have race conditions.  This
        # presents misbehavior.
        time.sleep(2)
        apply(self.do, ["commit", "-m '%s'" % message])
    def write(self, fn, content):
        "Create file content in the repository."
        if verbose >= DEBUG_COMMANDS:
            sys.stdout.write("%s <- %s" % (fn, content))
        with directory_context(self.directory):
            with open(fn, "w") as fp:
                fp.write(content)
    def append(self, fn, content):
        "Append to file content in the repository."
        if verbose >= DEBUG_COMMANDS:
            sys.stdout.write("%s <-| %s" % (fn, content))
        with directory_context(self.directory):
            with open(fn, "a") as fp:
                fp.write(content)
    def update(self, rev):
        "Update the content to the specified revision or tag."
        if rev == 'master':
            rev = "HEAD"
        self.do("up", "-kb", "-A", "-r", rev)
    def cleanup(self):
        "Clean up the checkout directory."
        if self.proxied:
            shutil.rmtree(self.repo.directory + os.sep + "CVSROOT")
            os.unlink(self.repo.directory + os.sep + self.module)
            os.rmdir(self.repo.directory)
        if os.path.exists(self.directory):
            shutil.rmtree(self.directory)

def expect_same(a, b):
    "Complain if two files aren't identical"
    if not os.path.exists(a):
        sys.stderr.write("%s does not exist in CVS.\n" % a)
        return
    if not os.path.exists(b):
        sys.stderr.write("%s does not exist in the git conversion.\n" % b)
        return
    if not filecmp.cmp(a, b, shallow=False):
        sys.stderr.write("%s and %s are not the same.\n" % (a, b))
    
def expect_different(a, b):
    "Rejoice if two files are unexpectedly identical"
    if not os.path.exists(a):
        sys.stderr.write("%s does not exist in CVS.\n" % a)
        return
    if not os.path.exists(b):
        sys.stderr.write("%s does not exist in the git conversion.\n" % b)
        return
    if filecmp.cmp(a, b, shallow=False):
        sys.stderr.write("%s and %s are unexpectedly the same.\n" % (a, b))

def junkbranch(name):
    "Is this a synthetic branch generated by cvs-fast-export?"
    return  name.startswith("import-") or name.find("UNNAMED") != -1

class ConvertComparison:
    "Compare a CVS repository and its conversion for equality."
    # Needs to stay synchronized with reposurgeon's generic conversion makefile
    SUFFIX = "-git"
    def __init__(self, stem, repo=None, checkout=None, module=None, options="", showdiffs=False):
        self.stem = stem
        self.repo = CVSRepository(repo if repo else stem + ".testrepo")
        self.checkout = self.repo.checkout(module,
                                           checkout if checkout else stem + ".checkout")
        self.module = module or stem
        self.showdiffs = showdiffs
        self.repo.convert("module", stem + ConvertComparison.SUFFIX, more_opts=options)
        with directory_context(stem + ConvertComparison.SUFFIX):
            self.branches = [name for name in capture_or_die("git branch -l").split() if name != '*' and not junkbranch(name)]
            self.tags = [name for name in capture_or_die("git tag -l").split()]
        self.branches.sort()
        if "master" in self.branches:
            self.branches.remove("master")
            self.branches = ["master"] + self.branches
    def compare_tree(self, legend, ref, success_expected=True):
        "Test to see if a tag or branch checkout has the expected content."
        preamble = "%s %s %s: " % (self.stem, legend, ref)
        if ref not in self.tags and ref not in self.branches:
            if success_expected:
                sys.stderr.write(preamble + "%s unexpectedly missing.\n" % ref)
            return False
        def ftw(mydir, ignore):
            for root, dirs, files in os.walk(mydir):
                for file in files:
                    path = os.path.join(root, file)
                    if ignore not in path.split(os.sep) and not path.endswith(".cvsignore") and not path.endswith(".gitignore"):
                        yield path
        self.checkout.update(ref)
        with directory_context(self.stem + ConvertComparison.SUFFIX):
            if not noisy_run("git checkout --quiet %s" % ref):
                self.repo.cleanup()
                sys.exit(1)
        if 0:
            with directory_context(self.stem + ConvertComparison.SUFFIX):
                if not noisy_run("git log --format=%H -1"):
                    self.repo.cleanup()
                    sys.exit(1)
        cvspaths = list(ftw(self.stem + ".checkout", ignore="CVS"))
        cvsfiles = [fn[len(self.stem+".checkout")+1:] for fn in cvspaths]
        gitpaths = list(ftw(self.stem + ConvertComparison.SUFFIX, ignore=".git"))
        gitfiles = [fn[len(self.stem+ConvertComparison.SUFFIX)+1:] for fn in gitpaths]
        cvsfiles.sort()
        gitfiles.sort()
        success = True
        if cvsfiles != gitfiles:
            if success_expected:
                sys.stderr.write(preamble + "file manifests don't match.\n")
                if self.showdiffs:
                    sys.stderr.write(preamble + "common: %d\n" %
                                     len([f for f in gitfiles if f in cvsfiles]))
                    gitspace_only = {f for f in gitfiles if not f in cvsfiles}
                    if gitspace_only:
                        sys.stderr.write(preamble + "gitspace only: %s\n" %
                                         gitspace_only)
                    cvs_only = {f for f in cvsfiles if not f in gitfiles}
                    if cvs_only:
                        sys.stderr.write(preamble + "CVS only: %s\n" %
                                         cvs_only)
            success = False
        common = [(path, path.replace(".checkout/", ConvertComparison.SUFFIX + "/"))
                  for path in cvspaths if path.replace(".checkout/", ConvertComparison.SUFFIX + "/") in gitpaths]
        for (a, b) in common:
            if not filecmp.cmp(a, b, shallow=False):
                success = False
                if success_expected:
                    sys.stderr.write("%s %s %s: %s and %s are different.\n" % (self.stem, legend, ref, a, b))
                    if self.showdiffs:
                        os.system("diff -u %s %s" % (a, b))
        if success:
            if not success_expected:
                sys.stderr.write(preamble + "trees unexpectedly match\n")
            elif verbose >= DEBUG_STEPS:
                sys.stderr.write(preamble + "trees matched as expected\n")
        elif not success:
            if not success_expected and verbose >= DEBUG_STEPS:
                sys.stderr.write(preamble + "trees diverged as expected\n")
        return success
    def checkall(self):
        "Check all named references - branches and tags - expecting matches."
        for branch in cc.branches:
            if branch.endswith("UNNAMED-BRANCH"):
                if verbose > 0:
                    sys.stderr.write("%s: skipping %s\n" % (os.path.basename(sys.argv[0]), branch))
            else:
                cc.compare_tree("branch", branch)
        for tag in cc.tags:
            cc.compare_tree("tag", tag)
    def command_returns(self, cmd, expected):
        seen = capture_or_die(cmd)
        succeeded = (seen.strip() == expected.strip())
        if not succeeded:
            sys.stderr.write(cmd + " return was not as expected\n")
    def cleanup(self):
        self.checkout.cleanup()
        shutil.rmtree(self.stem+ConvertComparison.SUFFIX)

# End.
