#!/usr/bin/env python

import os
import sys
import subprocess
import pprint
import re
import time
import json


#
# Run sstat and return the exit code.
def execute_sstat(jobid):
        p    = subprocess.Popen(["sstat", "-a", "-P", "-j", jobid], \
                                stdout = subprocess.PIPE,
                                stderr = subprocess.PIPE)

        o, e = p.communicate()
        ret  = p.wait()

	return ret, o, e


start = time.time()
jobid = os.environ["PSTEST_JOB_ID"]

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

	sstat = {"now": time.time() - start, "sstat": []}

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

