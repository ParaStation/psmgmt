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
	test.check(2 == len(lines))

	for line in lines:
		test.check("0: 0" == line or \
		           "1: 1" == line)


test.quit()

