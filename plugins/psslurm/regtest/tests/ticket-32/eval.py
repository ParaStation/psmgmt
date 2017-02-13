#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	n = int(helper.partition_cpus(p))/2

	test.check("FAILED"   == helper.job_state(p), p)
	test.check("%d:0" % n == helper.job_exit_code(p), p)

	lines = helper.job_stderr_lines(p)

	for i in range(n):
		test.check(1 == len([x for x in lines if re.match(r'srun: error:.*Exited with exit code %d$' % (i + 1), x)]), p) 


test.quit()

