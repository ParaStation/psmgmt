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
	test.check("Hello from 5" == lines[0], p)


test.quit()

