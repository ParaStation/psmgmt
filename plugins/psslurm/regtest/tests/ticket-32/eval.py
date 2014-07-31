#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("FAILED" == helper.job_state(p), p)
	test.check("16:0"   == helper.job_exit_code(p), p)

	lines = helper.job_stderr_lines(p)

	for i in range(16):
		test.check(1 == len([x for x in lines if re.match(r'srun: error:.*Exited with exit code %d$' % (i + 1), x)]), p) 


test.quit()

