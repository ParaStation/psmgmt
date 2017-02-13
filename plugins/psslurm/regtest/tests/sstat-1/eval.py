#!/usr/bin/env python

import sys
import os
import re
import json

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("0" == helper.fproc_exit_code(p), p)

	lines = helper.fproc_stdout_lines(p)
	test.check(0 == len(helper.fproc_stderr_lines(p)), p)

	hits = 0
	for line in lines:
		sstat = json.loads(line)
		if 0 == len(sstat["sstat"]):
			continue

		hits += 1
		test.check(len(sstat["sstat"]) in [1, 2], p)	# One job step might already be terminated


		jobId   = sstat["jobId"]
		stepIds = [sstat["sstat"][x]["JobID"] for x in [0, 1]]

		if 2 == len(stepIds):
			test.check("%s.0" % jobId == stepIds[0], p)
			test.check("%s.1" % jobId == stepIds[1], p)
		else:
			test.check(("%s.0" % jobId == stepIds[0]) or \
			           ("%s.1" % jobId == stepIds[0]), p)

	test.check(hits > 0, p)


test.quit()

