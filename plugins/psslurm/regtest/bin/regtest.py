#!/usr/bin/env python

import sys
import os
import subprocess
import json
import re
import time
import threading
# To guarantee that we can run on systems
# with an older software stack we use optparse
# rather than the newer argparse module.
import optparse
import select
import termios
import hashlib


#
# Pad a string with whitespaces
def whitespace_pad(x, n):
	return x + " " * (n - len(x))


#
# Get the version of the slurm installation as a string.
def query_slurm_version():
	p       = subprocess.Popen(["squeue", "-V"], stdout = subprocess.PIPE)
	version = p.communicate()[0].split()[1]
	if 1 != p.wait():
		raise Exception("Failed to retrieve version")

	return version

#
# Generic worker thread class that executes a specified function
# with some arguments.
# Might look strange given that the thread module underlying threading
# provides exactly this functionality but threading also gives us
# a join function!
class WorkerThread(threading.Thread):
	def __init__(self, fct, args, name = None):
		threading.Thread.__init__(self)

		self.fct  = fct
		self.args = args
		self.ret  = None
		self.name = name

	def run(self):
		self.ret = self.fct(*self.args)

#
# Parse a single line of "scontrol -o show job" output.
def parse_scontrol_output_line(line):
	stats = {}

	while len(line) > 0:
		x = re.search(r'([^ ]+)=(.*?)( ([^ ]+)=|$)', line)
		stats[x.group(1)] = x.group(2)

		line = line.replace(x.group(1) + "=" + x.group(2), "", 1).strip()

	return stats

#
# Parse the output of "scontrol -o show job". The function
# returns an array of dictionaries of key-value pairs. For
# normal jobs the array will have length one. For job arrays
# there will be one dictionary per array task.
def parse_scontrol_output(text):
	return [parse_scontrol_output_line(x) for x in text.split("\n") if len(x) > 0]

#
# Query the status of a job using scontrol. The argument jobid must
# be a string.
def query_scontrol(jobid):
	p = subprocess.Popen(["scontrol", "-o", "show", "job", jobid], \
	                     stdout = subprocess.PIPE, \
	                     stderr = subprocess.PIPE)

	out, err = p.communicate()
	ret = p.wait()

	stats = None
	if 0 == ret:
		stats = parse_scontrol_output(out)

	return stats

#
# Prepare the submission command. This function can be used for
# both sbatch and srun.
def prepare_submit_cmd(part, reserv, cmd, key, wdir, odir, flags):
	tmp = [cmd[0], "-J", key, "-p", part]
	if len(reserv) > 0:
		tmp += ["--reservation", reserv]

	if len([x for x in flags if "SUBMIT_NO_O_OPTION" == x]) < 1 and \
	   len([x for x in cmd[1:] if "-o" == x]) < 1:
			tmp += ["-o", odir + "/slurm-%j.out"]

	if len([x for x in flags if "SUBMIT_NO_E_OPTION" == x]) < 1 and \
	   len([x for x in cmd[1:] if "-e" == x]) < 1:
			tmp += ["-e", odir + "/slurm-%j.err"]

	return tmp + cmd[1:]


#
# Submit a job to partition part and return the jobid using sbatch.
# We know that sbatch is non-blocking so we can directly wait here
# for the process to finish.
def submit_via_sbatch(part, reserv, cmd, key, wdir, odir, flags):
	cmd = prepare_submit_cmd(part, reserv, cmd, key, wdir, odir, flags)

	p = subprocess.Popen(cmd, \
	                     stdout = subprocess.PIPE, \
	                     stderr = subprocess.PIPE, \
	                     cwd = wdir)

	out = p.stdout.readline()

	return p, re.search(r'Submitted batch job ([0-9]+)', out).group(1)

#
# Submit a job to partition part and return the jobid using srun.
def submit_via_srun(part, reserv, cmd, key, wdir, odir, flags):
	cmd = prepare_submit_cmd(part, reserv, cmd, key, wdir, odir, flags)

	p = subprocess.Popen(cmd, \
	                     stdout = subprocess.PIPE, \
	                     stderr = subprocess.PIPE, \
	                     cwd = wdir)

	err = p.stderr.readline()

	# srun (in contrast to sbatch) writes the status information
	# to stderr.

	#
	# TODO Handle messages like "srun: Required node not available (down or drained)"
	#
	return p, re.search('srun: job ([0-9]+) queued and waiting for resources', err).group(1)

#
# Submit a job to partition part and return the jobid.
#
# The current version of the code cannot handle srun since srun blocks.
# Moreover, when using srun we want to check that Ctrl-C and friends are
# properly handled.
def submit(part, reserv, cmd, key, wdir, odir, flags):
	return {"sbatch": submit_via_sbatch, \
	        "srun"  : submit_via_srun}[cmd[0].strip()](part, reserv, cmd, key, \
	                                                   wdir, odir, flags)


#
# Interpret state as either done or not-done.
def state_means_done(state):
	# Job state codes (from the squeue man page):
	# PENDING, RUNNING, SUSPENDED, CANCELLED,
	# COMPLETING, COMPLETED, CONFIGURING, FAILED, TIMEOUT,
	# PREEMPTED, NODE_FAIL and SPECIAL_EXIT
	return state in ["COMPLETED", \
	                 "FAILED", "TIMEOUT", "CANCELLED", "NODE_FAIL"]

#
# Check if a job is done. An array of jobs is only considered to be
# completely done when all array tasks have finished.
def job_is_done(stats):
	tmp = [state_means_done(x["JobState"]) for x in stats]
	return (len(stats) == len([x for x in tmp if x]))

#
# Execute a batch job. The function waits until the job and the accompanying
# frontend process (if one) are terminated.
#
# TODO Implement a timeout mechanism.
#
def exec_test_batch(test, idx):
	assert("batch" == test["type"])

	part   = test["partitions"][idx]
	reserv = test["reservations"][idx]

	q     = None
	jobid = None
	# "submit" can be null in the input JSON file. In this case
	# we only run the frontend process which interacts with the
	# batch system directly.
	if test["submit"]:
		q, jobid = submit(part, reserv, test["submit"], \
		                  test["key"], test["root"], \
		                  test["outdir"], test["flags"])

	# Use partition instead of jobid here since jobid may be None!
	fproc_out = test["outdir"] + "/fproc-%s.out" % part
	fproc_err = test["outdir"] + "/fproc-%s.err" % part

	p = None
	if "fproc" in test.keys() and test["fproc"]:
		# Prepare the environment for the front-end process
		env = os.environ.copy()
		env["PSTEST_PARTITION"]   = "%s" % part
		env["PSTEST_RESERVATION"] = "%s" % reserv
		if jobid:
			env["PSTEST_JOB_ID"] = jobid

		cmd = test["fproc"]
		cmd = [test["root"] + "/" + cmd[0]] + cmd[1:]

		p = subprocess.Popen(cmd, \
		                     stdout = open(fproc_out, "w"), \
		                     stderr = open(fproc_err, "w"), \
		                     env = env, \
		                     cwd = test["root"])

	stdout = ""
	stderr = ""

	delay = 1.0/test["monitor_hz"]

	stats = {"scontrol": None, \
	         "fproc"   : None, \
	         "submit"  : None}
	while 1:
		done = 1

		if q:
			ready, _, _ = select.select([q.stdout, q.stderr], [], [], 0)
			if len(ready) > 0:
				for x in ready:
					if q.stdout == x:
						stdout += x.read()
					if q.stderr == x:
						stderr += x.read()

			ret = q.poll()
			if None != ret:
				stdout += q.stdout.read()
				stderr += q.stderr.read()

				stats["submit"] = { "ExitCode": ret}
				q = None
			else:
				done = 0

		if p:
			ret = p.poll()
			if None != ret:
				# Use CamelCase for the keys here to so that we can handle
				# the scontrol and fproc stats in the same fasion.
				stats["fproc"] = {"StdOut": fproc_out, \
				                  "StdErr": fproc_err, \
				                  "ExitCode": ret}
				p = None
			else:
				done = 0

		if jobid:
			tmp = query_scontrol(jobid)

			if tmp and len(tmp) > 0:
				stats["scontrol"] = tmp

				if not job_is_done(tmp):
					done = 0

		if done:
			break

		# TODO Actually we should measure the time of the previous
		#      code and subtract it from delay. If the result is negative
		#      we need to add the smallest multiple of delay such that
		#      the sum is positive. In this way we guarantee that
		#      loop time is a multiple of the requested period
		time.sleep(delay)

	# Special cases for srun.
	# srun does not support job arrays and sbatch requires the output to be
	# written to a file.
	if stats["scontrol"] and 1 == len(stats["scontrol"]):
		tmp = stats["scontrol"]

		# To simplify writing evaluation scripts we always add StdOut and StdErr
		# to the scontrol output.
		# FIXME That would be incorrect if the user specifies some -o options?
		if not "StdOut" in tmp[0].keys():
			tmp[0]["StdOut"] = test["outdir"] + "/slurm-%s.out" % jobid
		if not "StdErr" in tmp[0].keys():
			tmp[0]["StdErr"] = test["outdir"] + "/slurm-%s.err" % jobid

		# Slurm seems to have a bug in that it does not properly resolve format
		# string for StdErr (even though it writes to the correct file. This is
		# a workaround for this bug
		tmp[0]["StdErr"] = re.sub(r'%j', jobid, tmp[0]["StdErr"])

		# If output file is not existing we assume that all output went to stdout
		# and stderr.
		# stderr will be truncated since we already read one line!
		if not os.path.isfile(tmp[0]["StdOut"]):
			open(tmp[0]["StdOut"], "w").write(stdout)
		if not os.path.isfile(tmp[0]["StdErr"]):
			open(tmp[0]["StdErr"], "w").write(stderr)

	# Return the latest captured stats
	return stats

#
# Execute an interactive job.
def exec_test_interactive(test, idx):
	assert("interactive" == test["type"])

	part   = test["partitions"][idx]
	reserv = test["reservations"][idx]

	stdout = ""
	stderr = ""

	q      = None
	jobid  = None
	if test["submit"]:
		master, slave = os.openpty()

		# Disable the echo. Note that we still get an echo from srun as demonstrated
		# by the following trace of a bash session where I disabled the echo, executed
		# srun -N 1 --pty /bin/bash and typed hostname and exit afterwards:
		#
		# > -bash-4.1$ stty -echo
		# > -bash-4.1$ srun: job 4881 queued and waiting for resources
		# > srun: job 4881 has been allocated resources
		# > hostname
		# > exit
		# > bash-4.1$ hostname
		# > j3c004
		# > bash-4.1$ exit
		# > exit
		#
		attr = termios.tcgetattr(master)
		attr[3] = attr[3] & (~termios.ECHO)
		termios.tcsetattr(master, termios.TCSADRAIN, attr)

		cmd = test["submit"]
		tmp = [cmd[0], "-p", part]
		if len(reserv) > 0:
			tmp += ["--reservation", reserv]
		cmd = tmp + cmd[1:]

		q = subprocess.Popen(cmd, \
		                     stdin  = slave, \
		                     stdout = slave, \
		                     stderr = slave, \
		                     cwd = test["root"])

		# In contrast to batch jobs we do not start the frontend process before we
		# have been allocated resources. This still does not guarantee that the
		# remote side is ready to receive input, though.

		jobid  = None
		while 1:
			ret = q.poll()
			if None != ret:
				sys.stderr.write("Submission failed with exit code %d.\n" % ret)
				return None

			# Add a timeout here to make sure we do not wait indefinitely
			# if the job dies for some reason.
			ready, _, _ = select.select([master], [], [], 1)

			if 0 == len(ready):
				continue

			for x in ready:
				stdout += os.read(master, 1028)

			done = 0

			for line in stdout.split("\n"):
				if not jobid:
					x = re.search(r'srun: job ([0-9]+) queued and waiting for resources', line)
					if x:
						jobid = x.group(1)
				else:
					if re.match(r'.* job %s .* allocated .*' % jobid, line):
						done = 1
						break

			if done:
				break

		if not jobid:
			raise Exception("Submission failure.")

	# For an interactive job we need someone to interact with the spawned processes
	# so we are sure that "fproc" in test.keys() - This is checked elsewhere.

	# FIXME Environment

	cmd = test["fproc"]
	cmd = [test["root"] + "/" + cmd[0]] + cmd[1:]
	p = subprocess.Popen(cmd, \
	                     stdin  = subprocess.PIPE, \
	                     stdout = subprocess.PIPE, \
	                     cwd = test["root"])


	delay = 1.0/test["monitor_hz"]

	stats = {"scontrol": None, \
	         "fproc"   : None, \
	         "submit"  : None}
	while 1:
		done = 1

		if q:
			ready, _, _ = select.select([master], [], [], 0)

			if len(ready) > 0:
				for x in ready:
					stdout += os.read(master, 1024)

			ret = q.poll()
			if None != ret:
				stats["submit"] = { "ExitCode": ret}
				q = None
			else:
				done = 0

		if p:
			ready, _, _ = select.select([p.stdout], [], [], 0)
			if len(ready) > 0:
				os.write(master, p.stdout.read())

			ret = p.poll()
			if None != ret:
				stats["fproc"] = {"ExitCode": ret}
				p = None
			else:
				done = 0

		if jobid:
			tmp = query_scontrol(jobid)

			if tmp and len(tmp) > 0:
				stats["scontrol"] = tmp

				if not job_is_done(tmp):
					done = 0

		if done:
			break

		# TODO Actually we should measure the time of the previous
		#      code and subtract it from delay. If the result is negative
		#      we need to add the smallest multiple of delay such that
		#      the sum is positive. In this way we guarantee that
		#      loop time is a multiple of the requested period.
		time.sleep(delay)

	stats["scontrol"][0]["StdOut"] = test["outdir"] + "/slurm-%s.out" % jobid
	open(stats["scontrol"][0]["StdOut"], "w").write(stdout)

	return stats

#
# Convert a CamelCase string to a CAMEL_CASE type string
def camel_to_upper(string):
	LOWERCASE = "abcdefghijklmnopqrstuvwxyz"
	UPPERCASE = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	new = string[0]
	for i in range(1, len(string)):
		if string[i] in UPPERCASE and string[i-1] in LOWERCASE:
			new = new + "_" + string[i]
		else:
			new = new       + string[i]

	return new.upper()

#
# Sanitize a string such that it can be used as the name of
# an environment variable.
def sanitize(string):
	string = string.replace("/", "_SLASH_")
	string = string.replace(":", "_COLON_")

	return string

#
# Export the key-value pairs from a dictionary to the environment.
def export_dictionary_to_env(dictionary, prefix, env):
	for k, v in dictionary.iteritems():
		env[prefix + sanitize(camel_to_upper(k))] = str(v)

#
# Export the information gathered via scontrol to the environment.
def export_scontrol_output_to_env(stats, part, env):
	if 1 == len(stats):
		prefix = "PSTEST_SCONTROL_" + part.upper() + "_"
		export_dictionary_to_env(stats[0], prefix, env)
	else:
		for i, x in enumerate(stats):
			prefix = "PSTEST_SCONTROL_" + part.upper() + "_%d_" % i
			export_dictionary_to_env(x, prefix, env)

#
# Export the variables related to the frontend process to
# the environment.
def export_fproc_variables_to_env(stats, part, env):
	prefix = "PSTEST_FPROC_" + part.upper() + "_"
	export_dictionary_to_env(stats, prefix, env)

#
# Export the variables related to the srun/batch process to
# the environment.
def export_submit_variables_to_env(stats, part, env):
	prefix = "PSTEST_SUBMIT_" + part.upper() + "_"
	export_dictionary_to_env(stats, prefix, env)

#
# Execute an evaluation command. The scontrol information about the job are
# passed via the environment. Specifically, for the job submitted to the partition
# "batch", several environment variables with the prefix PSTEST_SCONTROL_SBATCH_ are
# available corresponding to the scontrol output.
def exec_eval_command(test, stats):
	env = os.environ.copy()

	env["PSTEST_PARTITIONS"] = " ".join(test["partitions"])
	env["PSTEST_TESTKEY"] = test["key"]
	env["PSTEST_OUTDIR"]  = test["outdir"]

	for i, part in enumerate(test["partitions"]):
		if stats[i]["scontrol"]:
			export_scontrol_output_to_env(stats[i]["scontrol"], \
			                              part, env)

		if stats[i]["fproc"]:
			export_fproc_variables_to_env(stats[i]["fproc"], \
			                              part, env)

		if stats[i]["submit"]:
			export_submit_variables_to_env(stats[i]["submit"], \
			                               part, env)

	cmd = test["eval"]
	cmd = [test["root"] + "/" + cmd[0]] + cmd[1:]

	p = subprocess.Popen(cmd, \
		             stdout = open(test["outdir"] + "/eval.out", "w"), \
		             stderr = open(test["outdir"] + "/eval.err", "w"), \
	                     env = env, \
	                     cwd = test["root"])

	ret = p.wait()

	return ret

#
# Execute the evaluation processes. Usually this is done using a
# user-specified application that checks the output. If no evaluation
# program is specified in test description all we can do is check
# the exit code of the submissions.
def eval_test_outcome(test, stats):
	fail = 0

	for x in stats:
		if not x:
			fail = 1

	if not fail:
		if "eval" in test.keys() and test["eval"]:
			fail = exec_eval_command(test, stats)
		else:
			tmp = []
			for x in stats:
				for z in x["scontrol"]:
					tmp.append(z["ExitCode"])
			# FIXME What happens if all tests across all partitions
			#       fail?
			if min(tmp) != max(tmp):
				fail = 1

	global BL
	BL.acquire()

	try:
		tmp1 = whitespace_pad(test["name"],30)
		tmp2 = whitespace_pad(test["key"], 49)

		sys.stdout.flush()
		# TODO Take terminal width into account?
		if fail:
			print(" %s%s [\033[0;31mFAIL\033[0m] " % (tmp1, tmp2))
		else:
			print(" %s%s [\033[0;32mOK\033[0m] "   % (tmp1, tmp2))
		sys.stdout.flush()
	finally:
		BL.release()

#
# Create an empty output folder for the test. 
def create_output_dir(test):
	os.mkdir(test["outdir"])

#
# Replace matches to regexp by repl in all entries of array
# and return the new array
def fixup_test_description_placeholder(array, regexp, repl):
	return [re.sub(regexp, repl, x) for x in array]

#
# Pre-processing of the test description.
# Replaces @D in strings by the root directory of the test.
# We use @ here instead of the more common %D in order to ensure
# that Slurm format strings are not altered.
# Replaces @O in strings by the output directory of the test.
def fixup_test_description(test, opts):
	for z in ["submit", "eval", "fproc"]:
		if not test[z]:
			continue
		x = test[z]

		if isinstance(x, basestring):
			x = x.split()

		x = fixup_test_description_placeholder(x, r'@D', test["root"])
		x = fixup_test_description_placeholder(x, r'@O', test["outdir"])

		test[z] = x


	for x in opts.ignorep:
		for i, z in enumerate(test["partitions"]):
			if x == z:
				del test["partitions"][i]
				del test["reservations"][i]
				break

	# Specifying flags is optional
	if not "flags" in test.keys():
		test["flags"] = []

#
# Check that the test description is okay.
def check_test_description(test):
	KEYS = ["type", "partitions", "submit", "eval", "fproc", "monitor_hz"]

	for k in KEYS:
		if k not in test.keys():
			raise Exception("Missing key '%s' in input file. Try adding" \
			                "\"%s\": null to the description." % (k, k))

	if not test["type"] in ["batch", "interactive"]:
		raise Exception("Invalid test type '%s'" % test["type"])

	if "interactive" == test["type"]:
		if not test["submit"][0] in ["srun", "salloc"]:
			raise Exception("Interactive jobs need to be submitted " \
			                "via 'srun' or 'salloc'.")
		if not test["fproc"]:
			raise Exception("Interactive jobs need a frontend process " \
			                "that handles the interaction.")

#
# Run a single test. For each specified partition the function will submit one
# job, potentially spawn an accompanying frontend process that can interact with
# the batch system (e.g., to test job canceling) and then runs the evaluation.
def perform_test(testdir, testkey, opts):

	# For convenience we allow Python-style comments in the JSON files. These
	# are removed before presenting the string to the json.loads function.
	descr = " ".join(map(lambda x: re.sub(r'#.*$', r'', x), \
	                     open(testdir + "/descr.json", "r").readlines()))
	try:
		test = json.loads(descr)
	except ValueError as e:
		sys.stderr.write(" Error: exception thrown while parsing "
		                  "descr.json: '%s'\n" % str(e))
		exit(1)

	test["name"]   = os.path.basename(testdir)
	test["root"]   = testdir
	test["key"]    = testkey
	test["outdir"] = test["root"] + "/output-%s" % testkey

	fixup_test_description(test, opts)
	check_test_description(test)

	create_output_dir(test)

	if not test["type"] in ["batch", "interactive"]:
		raise Exception("Unknown test type '%s'" % test["type"])

	n = len(test["partitions"])

	if n != len(test["reservations"]):
		raise Exception("The \"partitions\" and \"reservations\" list must "
		                "be of equal size.")

	thr = []
	for i in range(n):
		fct = {"batch"      : exec_test_batch, \
		       "interactive": exec_test_interactive}[test["type"]]
		thr.append(WorkerThread(fct, [test, i]))

	[x.start() for x in thr]
	[x.join()  for x in thr]

	stats = [x.ret for x in thr]

	eval_test_outcome(test, stats)

	return

#
# Construct the list of tests to be performed taking into account exclude/include
# lists.
def get_test_list(argv, opts):
	tests = []
	if "" != opts.tests:
		tests = [x.strip() for x in opts.tests.split(",")]
	else:
		tests = os.listdir(opts.testsdir)

	if "" != opts.excludes:
		tmp = [x.strip() for x in opts.excludes.split(",")]
		done = 0
		while not done:
			done = 1
			for i in range(len(tests)):
				if tests[i] in tmp:
					del tests[i]
					done = 0
					break

	return tests

#
# Create a unique key for the test based on name, number and date
def test_key(test, testnum):
	tmp = hashlib.md5()
	tmp.update(test + "-%08d-%d" % (testnum, time.time()))
	return tmp.hexdigest()

def main(argv):
	parser = optparse.OptionParser(usage = "usage: %prog [options]")
	parser.add_option("-d", "--testsdir", action = "store", type = "string", \
	                  dest = "testsdir", \
	                  default = "/".join(os.path.abspath(sys.argv[0]).split("/")[:-2]) + "/tests", \
	                  help = "Path to the tests directory.")
	parser.add_option("-p", "--maxpar", action = "store", type = "int", \
	                  dest = "maxpar", default = 16, \
	                  help = "Maximal number of tests processed in parallel.")
	parser.add_option("-l", "--list", action = "store_true", \
	                  dest = "do_list", \
	                  help = "Print tests that would be run but do not actually run them.")
	parser.add_option("-t", "--tests", action = "store", type = "string", \
	                  dest = "tests", default = "", \
	                  help = "Comma-separated list of tests that should be executed.")
	parser.add_option("-x", "--excludes", action = "store", type = "string", \
	                  dest = "excludes", default = "", \
	                  help = "Comma-separated list of excluded tests that should not be executed.")
	parser.add_option("-i", "--ignorep", action = "store", type = "string", \
	                  dest = "ignorep", default = "", \
	                  help = "Comma-separated list of partitions that should be ignored.")
	parser.add_option("-r", "--repetitions", action = "store", type = "int", \
	                  dest = "repetitions", default = 1, \
	                  help = "Number of repetitions for each test.")

	(opts, args) = parser.parse_args()

	if not os.path.isdir(opts.testsdir):
		parser.error("Invalid tests directory '%s'." % opts.testsdir)

	opts.ignorep = [x.strip() for x in opts.ignorep.split(",")]

	tests = get_test_list(argv, opts)

	if opts.do_list:
		for testdir in tests:
			print(" " + whitespace_pad(testdir, 29) + \
			      " (%s)" % (opts.testsdir + "/" + testdir))
	else:
		if opts.repetitions > 1:
			tests = [x for z in tests for x in [z]*opts.repetitions]

		testnum = 0

		testthr = []
		for testdir in tests:
			testkey = test_key(testdir, testnum)
			testnum += 1

			testthr.append(WorkerThread(perform_test, \
			               [opts.testsdir + "/" + testdir, \
			               testkey, opts]))
			testthr[-1].start()

			testthr = [x for x in testthr if x.is_alive()]

			# Block until there is room for more threads.
			while len(testthr) >= opts.maxpar:
				time.sleep(0.1)
				testthr = [x for x in testthr if x.is_alive()]

		[x.join() for x in testthr]

# The big lock
BL = threading.Lock()

main(sys.argv)

