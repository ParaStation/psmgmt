#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	count = 0
	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]

	for line in lines:
		test.check("0" == line or "11" == line, p)
		if "11" == line:
			count += 1

	test.check(count < 10, p + ": count = %d" % count)	# number of failures < 5%


test.quit()

