#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = [x for x in helper.job_stderr_lines(p) if not re.match(r'srun:.*', x)]
	test.check(1 == len(lines), p)

	try:
		line = lines[-1]
		test.check(re.match(r'mem_bind=.*task.*0.*mask 0x.*', line), p)
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()

