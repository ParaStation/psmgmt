
import curl
import re
import sys
import os
import shutil
import subprocess
import socket
import hashlib
import time
import datetime


GIT  = "/usr/bin/git"
TAR  = "/usr/bin/tar"
URL  = "https://github.com/schedmd/slurm.git"

#
# A copy of the haskell fst function
def fst(tpl):
	return tpl[0]

#
# A copy of the haskell snd function
def snd(tpl):
	return tpl[1]


#
# Create a process and return the handle
def createProcess(argv):
	p = subprocess.Popen(argv,
			     stdout = subprocess.PIPE,
			     stderr = subprocess.PIPE)

	return p

#
# Wait for termination of a process.
def waitForProcess(p):
	o, e = p.communicate()
	x = p.wait()

	return (x, o, e)

#
# Create a subprocess and wait for it to exit. Return the exit
# code and standard output/exit as a triplet.
def createProcessAndWait(argv):
	return waitForProcess(createProcess(argv))

#
# Variant of createProcessAndWait which does not return the exit code but
# raises an exception if the command failed.
def createProcessAndWait_(argv):
	x, o, e = createProcessAndWait(argv)
	if x:
		raise Exception("Command '%s' failed with exit code %d." % (argv, x))

	return o, e

def generateUniqueId():
	x = hashlib.md5()
	x.update("%s-%0d-%d" % (socket.gethostname(), os.getpid(), time.time()))

	return x.hexdigest()

# #
# # Get the short form of the HEAD commit.
# def gitHeadCommit():
# 	return fst(createProcessAndWait_([GIT, "rev-parse", "--verify", "--short", "HEAD"])).strip()
#
# def retrieveVersion():
# 	return re.compile(r'.*AC_INIT\(\[.*\], \[([0-9\.]+)\..*\]\).*', re.DOTALL).match(open("configure.ac", "r").read()).group(1)
#
# def package():
# 	date    = datetime.datetime.fromtimestamp(time.time()).strftime("%Y%m%d")
# 	version = retrieveVersion()
# 	commit  = gitHeadCommit()
# 	files   = os.listdir(".")
#
# 	# Naming scheme following the Fedora naming guidelines:
# 	# https://fedoraproject.org/wiki/Packaging:NamingGuidelines?rd=Packaging/NamingGuidelines#NonNumericRelease
# 	D = "%s-%s-%sgit%s" % (NAME, version, date, commit)
#
# 	os.mkdir(D)
# 	for f in files:
# 		cp = None
#
# 		if ".git" == f:
# 			continue
#
# 		if os.path.isdir(f):
# 			cp = shutil.copytree
# 		else:
# 			cp = shutil.copy
#
# 		cp(f, D + "/" + f)
#
# 	createProcessAndWait_([TAR, "-czf", D + ".tar.gz", D])
#
# 	shutil.rmtree(D)
#
# 	return (D + ".tar.gz")

if "__main__" == __name__:
	startDir = os.getcwd()
	tmpDir   = "tmp-%s" % generateUniqueId()

	version = None
	if len(sys.argv) > 1:
		version = sys.argv[1]
	else:
		version, _ = createProcessAndWait_(["/usr/bin/scontrol", "--version"])
		version = version.split()[1]

	gitVersion, _ = createProcessAndWait_([GIT, "--version"])
	gitVersion = int("".join(["%03d" % int(x) for x in gitVersion.split()[-1].split(".")]))

	if gitVersion < 2000000:
		createProcessAndWait_([GIT, "clone", URL, tmpDir])
		os.chdir(tmpDir)

		tags, _ = createProcessAndWait_([GIT, "tag"])
		tags = [x for x in tags.split("\n") if x and re.match(r'^slurm-%s$' % version.replace(".","-"), x)]
	else:
		refs, _ = createProcessAndWait_([GIT, "ls-remote", "--tags", URL])
		tags = [snd(x.split("\t")).split("/")[-1] for x in refs.split("\n") if x and re.match(r'.*refs/tags/slurm-%s$' % version.replace(".","-"), x)]

	if len(tags) > 1:
		sys.stderr.write("Error: Multiple tags match the specified version: [%s]\n" % (", ".join(tags)))
		sys.exit(1)
	if len(tags) < 1:
		sys.stderr.write("Error: No tag matches the specified version.\n")
		sys.exit(1)

	if gitVersion < 2000000:
		createProcessAndWait_([GIT, "checkout", tags[0]])
	else:
		createProcessAndWait_([GIT, "clone", "--branch", tags[0], "--single-branch", "--depth", "1", URL, tmpDir])
		os.chdir(tmpDir)

	for patch in sorted([p for p in os.listdir(startDir + "/upstream//patches") if re.match(r'[0-9].*\.patch', p)]):
		createProcessAndWait_([GIT, "am", startDir + "/upstream/patches/" + patch])

	os.chdir(startDir)

	shutil.move(tmpDir + "/testsuite/expect", startDir + "/expect")

	shutil.rmtree(tmpDir)

