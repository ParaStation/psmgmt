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

jobid = os.environ["PSTEST_JOB_ID"]

# Wait until the job is running
while 1:
	tmp = query_scontrol(jobid)

	if "PENDING" == tmp["JobState"]:
		time.sleep(0.1)
		continue

	if "RUNNING" == tmp["JobState"]:
		break

	sys.stderr.write("Unexpected JobState: %s\n" % json.dumps(tmp))
	sys.exit(1)

time.sleep(60)

p = subprocess.Popen(["scancel", "--batch", "-s", "HUP", jobid], \
                     stdout = subprocess.PIPE,
                     stderr = subprocess.PIPE)

o, e = p.communicate()
ret  = p.wait()

if 0 != ret:
	sys.exit(ret)

sys.exit(0)

