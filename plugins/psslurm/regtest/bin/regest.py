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
	def __init__(self, fct, args):
		threading.Thread.__init__(self)

		self.fct  = fct
		self.args = args
		self.ret  = None

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
# Submit a job to partition part and return the jobid.
#
# The current version of the code cannot handle srun since srun blocks.
# Moreover, when using srun we want to check that Ctrl-C and friends are
# properly handled.
def submit(part, cmd):
	if "sbatch" != cmd[0].strip():
		raise Exception("The code currently only supports " \
		                "sbatch (not '%s')" % cmd[0])

	cmd = [cmd[0], "-p", part, "-D", "output" ] + cmd[1:]

	p = subprocess.Popen(cmd, \
	                     stdout = subprocess.PIPE, \
	                     stderr = subprocess.PIPE)

	out, err = p.communicate()
	ret = p.wait()

	if 0 != ret:
		raise Exception("Submission failed with error code %d: %s" % (ret, err))

	return re.search(r'Submitted batch job ([0-9]+)', out).group(1)

#
# Interpret state as either done or not-done.
def state_means_done(state):
	# Job state codes (from the squeue man page):
	# PENDING, RUNNING, SUSPENDED, CANCELLED,
	# COMPLETING, COMPLETED, CONFIGURING, FAILED, TIMEOUT,
	# PREEMPTED, NODE_FAIL and SPECIAL_EXIT
	return state in ["COMPLETED", \
	                 "FAILED", "TIMEOUT", "CANCELLED"]

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
def exec_test_batch(test, part):
	assert("batch" == test["type"])	

	jobid = None
	# "submit" can be null in the input JSON file. In this case
	# we only run the frontend process which interacts with the
	# batch system directly.
	if test["submit"]:
		jobid = submit(part, test["submit"])

	fproc_out = test["root"] + "/output/fproc-%s.out" % jobid
	fproc_err = test["root"] + "/output/fproc-%s.err" % jobid

	p = None
	if "fproc" in test.keys() and test["fproc"]:
		p = subprocess.Popen([test["root"] + "/" + test["fproc"], jobid], \
		                     stdout = open(fproc_out, "w"), \
		                     stderr = open(fproc_err, "w"))

	delay = 1.0/test["monitor_hz"]

	stats = {"scontrol": None, "fproc": None}
	while 1:
		done = 1

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
		#      loop time is a multiple of the requested period.
		time.sleep(delay)

	# Return the latest captured stats
	return stats

#
# Execute an interactive job.
def exec_test_interactive(test, part):
	assert("interactive" == test["type"])
	assert(1 == 0)	# Not implemented
	pass

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
# Execute an evaluation command. The scontrol information about the job are
# passed via the environment. Specifically, for the job submitted to the partition
# "batch", several environment variables with the prefix PSTEST_SCONTROL_SBATCH_ are
# available corresponding to the scontrol output.
def exec_eval_command(test, stats):
	env = os.environ.copy()

	env["PSTEST_PARTITIONS"] = " ".join(test["partitions"])
	
	for i, part in enumerate(test["partitions"]):
		if stats[i]["scontrol"]:
			export_scontrol_output_to_env(stats[i]["scontrol"], \
			                              part, env)

		if stats[i]["fproc"]:
			export_fproc_variables_to_env(stats[i]["fproc"], \
			                              part, env)

	cmd = test["eval"]
	cmd[0] = test["root"] + "/" + cmd[0]

	p = subprocess.Popen(cmd, \
		             stdout = open("output/eval.out", "w"), \
		             stderr = open("output/eval.err", "w"), \
	                     env = env)

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
			tmp = [x["ExitCode"] for x in stats]
			if min(tmp) != max(tmp):
				fail = 1

	global BL
	BL.acquire()

	# TODO The placing of the [OK]/[FAIL] text should depend on the terminal width
	#      and should not depend on the length of the test name string.
	if fail:
		print(" %s\t\t\t\t\t\t\t\t[\033[0;31mFAIL\033[0m] " % test["name"])
	else:
		print(" %s\t\t\t\t\t\t\t\t[\033[0;32mOK\033[0m] "   % test["name"])

	BL.release()

#
# Create an empty output folder for the test. Existing folders will be moved
# if they are non-empty.
def create_output_dir(testdir):
	outdir = testdir + "/output"

	if os.path.isdir(outdir) and len(os.listdir(outdir)) > 0:
		i = 1
		while 1:
			tmp = outdir + "-%04d" % i
			if not os.path.isdir(tmp):
				os.rename(outdir, tmp)
				break
			
			i = i+1

	if not os.path.isdir(outdir):
		os.mkdir(outdir)

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
def perform_test(testdir):
	test = json.loads(open(testdir + "/descr.json", "r").read())

	test["name"] = os.path.basename(testdir)
	test["root"] = testdir

	check_test_description(test)

	create_output_dir(testdir)
	os.chdir(testdir)

	if not test["type"] in ["batch", "interactive"]:
		raise Exception("Unknown test type '%s'" % test["type"])

	thr = []
	for part in test["partitions"]:
		fct = {"batch"      : exec_test_batch, \
		       "interactive": exec_test_interactive}[test["type"]]
		thr.append(WorkerThread(fct, [test, part]))

	[x.start() for x in thr]
	[x.join()  for x in thr]
	
	stats = [x.ret for x in thr]

	eval_test_outcome(test, stats)

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

	(opts, args) = parser.parse_args()

	if not os.path.isdir(opts.testsdir):
		parser.error("Invalid tests directory '%s'." % opts.testsdir)

	tests = get_test_list(argv, opts)

	if opts.do_list:
		for testdir in tests:
			print(" " + testdir + "\t\t(%s)" % (opts.testsdir + "/" + testdir))
	else:
		testthr = []
		for testdir in tests:
			testthr.append(WorkerThread(perform_test, [opts.testsdir + "/" + testdir]))
			testthr[-1].start()

			testthr = [x for x in testthr if x.is_alive()]

			# Block until there is room for more threads.
			while len(testthr) >= opts.maxpar:
				time.sleep(0.1)
				testthr = [x for x in testthr if x.is_alive()]

# The big lock
BL = threading.Lock()

main(sys.argv)

