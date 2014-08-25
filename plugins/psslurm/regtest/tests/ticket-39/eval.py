#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

        test.check(os.path.isfile("%s/slurm-%s.out" % (os.environ["PSTEST_OUTDIR"], \
                                                       os.environ["PSTEST_SCONTROL_%s_JOB_ID" % p.upper()])), p)
        test.check(not os.path.isfile("slurm-%s.out" % os.environ["PSTEST_SCONTROL_%s_JOB_ID" % p.upper()]), p)

	lines = [x for x in helper.job_stdout_lines(p) if "i = 10" == x]
	
	test.check(4 == len(lines), p)


test.quit()

