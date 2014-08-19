#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

inplines = [x for x in map(lambda z: z.strip(), open(os.environ["PSTEST_OUTDIR"] + "/../input.txt", "r").readlines()) if len(x) > 0]

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)

	test.check(4*len(inplines) == len(lines), p)

	for line in inplines:
		test.check(4 == len([x for x in lines if x == line]), p)

test.quit()

