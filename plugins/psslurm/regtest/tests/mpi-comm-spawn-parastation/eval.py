#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 4, p)

	lines.sort()

	test.check("OK P" == lines[0], p)
	test.check("OK S" == lines[1], p)
	test.check("OK S" == lines[2], p)
	test.check("OK S" == lines[3], p)

	sacct = helper.job_sacct_record(p)
	test.check(3 == len(sacct), p)

test.quit()
