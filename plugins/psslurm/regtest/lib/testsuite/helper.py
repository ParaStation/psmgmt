
import os
import re
import subprocess

import testsuite.test as test


#
# Get the list of partitions
def partitions():
	return [x.strip() for x in os.environ["PSTEST_PARTITIONS"].split()]

#
# Access the environment without terminating the test if something goes wrong
def save_env_access(part, key):
	try:
		tmp = os.environ[key % part.upper()]
	except Exception as e:
		test.check(1 == 0, part + ": " + str(e))
		return ""

	return tmp

#
# Get the job exit code for the specified partition.
def job_exit_code(part):
	return save_env_access(part, "PSTEST_SCONTROL_%s_EXIT_CODE")

#
# Get the exit code of the frontend process.
def fproc_exit_code(part):
	return save_env_access(part, "PSTEST_FPROC_%s_EXIT_CODE")

#
# Get the exit code of the submission process.
def submit_exit_code(part):
	return save_env_access(part, "PSTEST_SUBMIT_%s_EXIT_CODE")

#
# Get the job state for the specified partition.
def job_state(part):
	return save_env_access(part, "PSTEST_SCONTROL_%s_JOB_STATE")

#
# Get the job id.
def job_id(part):
	return save_env_access(part, "PSTEST_SCONTROL_%s_JOB_ID")

#
# Get the number of CPUs (in Slurm terminology) of nodes in the partition.
def partition_cpus(part):
	return save_env_access(part, "PSTEST_%s_CPUS")

#
# Get the job node list. In order for this to function the host names of the compute
# nodes must conform to some standards. The name prefix (which is common for all compute
# nodes) must start and end with a letter and may contain numbers only in the middle part.
def job_node_list(part):
	nodelist = save_env_access(part, "PSTEST_SCONTROL_%s_NODE_LIST")

	 # Replaces ([0-9]+)-([0-9]+) by a comma separted list
	def match1(matchobj):
		tmp0, tmp1 = matchobj.group(0).split('-')
		tmp2 = range(int(tmp0), int(tmp1) + 1)
		# slurm completely ignores len(tmp1) and we shall follow
		# that example.
		fmt = "%%0%dd" % len(tmp0)
		return ",".join([fmt % z for z in tmp2])

	def match2(matchobj):
		tmp = matchobj.group(0)
		return re.sub(r'([0-9]+)-([0-9]+)', match1, tmp)

	def match3(matchobj):
		tmp0, tmp1 = matchobj.group(1), matchobj.group(2)
		return ",".join([tmp0 + z for z in tmp1.split(",")])

	lst = re.sub(r'([^,]*)\[([0-9,]+)\]', match3, \
	             re.sub(r'\[([0-9,-]+)\]', match2, nodelist)).split(",")
	return lst

#
# Check that the job completed with zero exit code. This is a commonly used
# test so it makes sense to provide a convience wrapper.
def check_job_completed_ok(part):
 	test.check("COMPLETED" == job_state(part), part)
 	test.check("0:0"       == job_exit_code(part), part)

#
# Retrieve the stdout/stderr of a job
def job_output(part, key):
	try:
		tmp = open(os.environ[key % part.upper()]).read()
	except Exception as e:
		test.check(1 == 0, part + ": " + str(e))
		return None

	return tmp

#
# Retrieve the stdout/stderr of a job as an array of stripped lines.
def job_output_lines(part, key):
	try:
		tmp = [x for x in map(lambda z: z.strip(), job_output(part, key).split("\n")) if len(x) > 0]
	except Exception as e:
		test.check(1 == 0, part + ": " + str(e))
		return []

	return tmp

#
# Get the standard output of the job.
def job_stdout(part):
	return job_output(part, "PSTEST_SCONTROL_%s_STD_OUT")

#
# Get the standard output of the job splitted into lines.
def job_stdout_lines(part):
	return job_output_lines(part, "PSTEST_SCONTROL_%s_STD_OUT")

#
# Get the standard output of the frontend process.
def fproc_stdout(part):
	return job_output(part, "PSTEST_FPROC_%s_STD_OUT")

#
# Get the standard output of the frontend process splitted into lines.
def fproc_stdout_lines(part):
	return job_output_lines(part, "PSTEST_FPROC_%s_STD_OUT")

#
# Get the standard error of the job.
def job_stderr(part):
	return job_output(part, "PSTEST_SCONTROL_%s_STD_ERR")

#
# Get the standard error of the job splitted into lines.
def job_stderr_lines(part):
	return job_output_lines(part, "PSTEST_SCONTROL_%s_STD_ERR")

#
# Get the standard error of the frontend process.
def fproc_stderr(part):
	return job_output(part, "PSTEST_FPROC_%s_STD_ERR")

#
# Get the standard error of the frontend process splitted into lines.
def fproc_stderr_lines(part):
	return job_output_lines(part, "PSTEST_FPROC_%s_STD_ERR")

#
# Pretty print a dictionary
def pretty_print_dict(d):
	print("{\n " + ",\n ".join(["'%s': '%s'" % (str(k), str(v)) for k, v in d.iteritems()]) + "\n}")


#
# Print the environment to stdout 
def pretty_print_env():
	pretty_print_dict(os.environ)

#
# Get the sacct record of the job. The function returns an array of dictionaries
def job_sacct_record(part):
	try:
		cmd = ["sacct", "-o", "ALL", "-P", "-j", os.environ["PSTEST_SCONTROL_%s_JOB_ID" % part.upper()]]

		q = subprocess.Popen(cmd, \
		                     stdout = subprocess.PIPE, \
		                     stderr = subprocess.PIPE)

		x, _ = q.communicate()
		q.wait()

		lines = x.split('\n')   # Important: Do not strip here
		keys  = lines[0].split("|")

		sacct = []
		for line in [x for x in lines[1:] if len(x.strip()) > 0]:
			d = {}
			for i, v in enumerate(line.split("|")):
				d[keys[i]] = v

			sacct.append(d)

		return sacct
	except Exception as e:
		test.check(1 == 0, part + ": " + str(e))

	return None

