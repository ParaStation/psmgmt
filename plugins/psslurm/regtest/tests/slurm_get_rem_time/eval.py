#!/usr/bin/env python

import sys
import os
import datetime

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 1, p)

	rem = int(lines[0])

	test.check(rem > 5, p)
	test.check(rem < 65, p)

test.quit()
