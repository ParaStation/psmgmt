#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]
	err   = helper.job_stderr(p)

	test.check(6 == len(lines), p)

	test.check(lines[0] == lines[1], p)
	test.check(lines[0] == lines[2], p)

	if not re.match(r'.*cannot modify limit.*', err):
		test.check(lines[3] != lines[0], p)
		test.check(lines[3] == lines[4], p)
		test.check(lines[3] == lines[5], p)

test.quit()
