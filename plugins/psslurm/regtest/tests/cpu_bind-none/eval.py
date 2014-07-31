#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 2, p)

	try:
		count = 0
		for line in lines:
			tmp = line.split()
			if 0 == int(tmp[0]):
				test.check(4294967295 == int(tmp[1], base = 16), p)
				count += 1
			if 1 == int(tmp[0]):
				test.check(4294967295 == int(tmp[1], base = 16), p)
				count += 1
		test.check(len(lines) == count, p)
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
