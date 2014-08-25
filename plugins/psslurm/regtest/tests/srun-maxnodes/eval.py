#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]

	test.check(4 == len(lines), p)
	test.check(2 == len([x for x in lines if x == lines[0]]))
	test.check(2 == len([x for x in lines if x == lines[1]]))
	test.check(2 == len([x for x in lines if x == lines[2]]))
	test.check(2 == len([x for x in lines if x == lines[3]]))


test.quit()

