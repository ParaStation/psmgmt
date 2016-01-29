#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)

	test.check(5 == len(lines), p)
	test.check("0" == lines[1], p)
	test.check("0" == lines[4], p)


test.quit()

