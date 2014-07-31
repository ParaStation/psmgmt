#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	try:
		outA = open(os.environ["PSTEST_OUTDIR"] + "/slurm-" + \
		            os.environ["PSTEST_SCONTROL_%s_JOB_ID" % p.upper()] + "-A.out").read()
		outB = open(os.environ["PSTEST_OUTDIR"] + "/slurm-" + \
		            os.environ["PSTEST_SCONTROL_%s_JOB_ID" % p.upper()] + "-B.out").read()
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

	try:
		lines = [x for x in map(lambda z: z.strip(), outA.split("\n")) if len(x) > 0]
		test.check(len(lines) == 2, p)

		test.check("1" == lines[0], p)
		test.check("2" == lines[1], p)

		lines = [x for x in map(lambda z: z.strip(), outB.split("\n")) if len(x) > 0]
		test.check(len(lines) == 1, p)

		test.check("2" == lines[0], p)
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))


test.quit()

