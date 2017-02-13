#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = sorted([x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p) and \
	                                                      not re.match('sbatch:.*', x)])

	test.check(re.match(r'.*cannot stat.*', lines[0]), p)


test.quit()

