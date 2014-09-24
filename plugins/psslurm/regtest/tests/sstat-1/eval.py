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

		test.check(2 == len(sstat["sstat"]), p)
		test.check("%s.0" % helper.job_id(p) == sstat["sstat"][0]["JobID"], p)
		test.check("%s.1" % helper.job_id(p) == sstat["sstat"][1]["JobID"], p)

	test.check(hits > 0, p)


test.quit()

