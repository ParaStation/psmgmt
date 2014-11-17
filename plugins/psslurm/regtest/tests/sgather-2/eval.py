#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	jobid = helper.job_id(p)
	
	try:
		fil = [x for x in os.listdir(os.environ["PSTEST_OUTDIR"]) if re.match(r'slurm-%s\.txt.*' % jobid, x)][0]
		out = open(os.environ["PSTEST_OUTDIR"] + "/" + fil).read()
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

	test.check("OK\n" == out, p)

	lines = helper.job_stdout_lines(p)

	test.check(5 == len(lines), p)
	test.check("0" == lines[0], p)
	test.check("0" == lines[1], p)
	test.check("1" == lines[2], p)
	test.check("0" == lines[3], p)
	test.check("0" == lines[4], p)


test.quit()

