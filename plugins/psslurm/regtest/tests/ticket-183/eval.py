#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
        helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)

	env = {}

	for line in lines:
		x = line.split("=")
		z = x[0].strip()
		if len(z) > 0:
			env[z] = "=".join(x[1:])

	print("%s:" % p)
	helper.pretty_print_dict(env)

	test.check("1" == env["LICENSE_A"]  , p)
	test.check("2" == env["LICENSE_A_B"], p)


test.quit()

