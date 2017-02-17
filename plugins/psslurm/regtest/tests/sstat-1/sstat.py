#!/usr/bin/env python

import os
import sys
import subprocess
import pprint
import re
import time
import json

# TODO The following two functions are taken (with minor modifications)
#      from regtest.py.
#      It would be better to move them into a module that can be
#      used by the frontend processes as well as the test driver.

#
# Parse a single line of "scontrol --detail -o show job" output.
def parse_scontrol_output_line(line):
        stats = {}

        while len(line) > 0:
                x = re.search(r'([^ ]+)=(.*?)( ([^ ]+)=|$)', line)
                stats[x.group(1)] = x.group(2)

                line = line.replace(x.group(1) + "=" + x.group(2), "", 1).strip()

        return stats

#
# Query the status of a job using scontrol. The argument jobid must
# be a string.
def query_scontrol(jobid):
        p = subprocess.Popen(["scontrol", "--detail", "-o", "show", "job", jobid], \
                             stdout = subprocess.PIPE, \
                             stderr = subprocess.PIPE)

        out, err = p.communicate()
        ret = p.wait()

        stats = None
        if 0 == ret:
		return parse_scontrol_output_line(out)

        return stats


#
# Run sstat and return the exit code.
def execute_sstat(jobid):
        p    = subprocess.Popen(["sstat", "-a", "-P", "-j", jobid], \
                                stdout = subprocess.PIPE,
                                stderr = subprocess.PIPE)

        o, e = p.communicate()
        ret  = p.wait()

	return ret, o, e

sbatch = ["sbatch"]
if "" != os.environ["PSTEST_PARTITION"]:
        sbatch += ["--partition", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
        sbatch += ["--reservation", os.environ["PSTEST_RESERVATION"]]
if "" != os.environ["PSTEST_QOS"]:
        sbatch += ["--qos", os.environ["PSTEST_QOS"]]
if "" != os.environ["PSTEST_ACCOUNT"]:
        sbatch += ["--account", os.environ["PSTEST_ACCOUNT"]]

cmd = sbatch + ["-N", "1", "-t", "1", "test.sh"]

p = subprocess.Popen(cmd, \
                     stdout = subprocess.PIPE, \
                     stderr = subprocess.PIPE, \
                     stdin  = subprocess.PIPE)
o, _ = p.communicate()
_ = p.wait()

x = re.search(r'.*Submitted batch job ([0-9]+).*', o)
if not x:
	sys.exit(1)
else:
	jobid = x.group(1)

# Wait until the job is running
while 1:
	tmp = query_scontrol(jobid)

	if tmp["JobState"] in ["PENDING", "CONFIGURING"]:
		time.sleep(0.1)
		continue

	if "RUNNING" == tmp["JobState"]:
		break

	sys.stderr.write("Unexpected JobState: %s\n" % json.dumps(tmp))
	sys.exit(1)


start = time.time()

while 1:
	ret, o, e = execute_sstat(jobid)
		
	sys.stderr.write(e)
	
	if 0 != ret:
		sys.exit(ret)

	lines = o.split('\n')
	keys  = lines[0].split("|")

	dobrk = 0
	if time.time() - start > 120:
		dobrk = 1

	sstat = {"now": time.time() - start, "jobId": jobid, "sstat": []}

	for line in lines[1:]:
		if '' == line or '-' == line.strip()[0]:
			continue

		dobrk = 0

		dd = {}

		for i, v in enumerate(line.split("|")):
			dd[keys[i]] = v

		sstat["sstat"].append(dd)

	if dobrk:
		break

	sys.stdout.write("%s\n" % json.dumps(sstat))
	sys.stdout.flush()

	time.sleep(10)


sys.exit(0)

