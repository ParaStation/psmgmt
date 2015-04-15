#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("FAILED" == helper.job_state(p), p)
	test.check("1:0"    == helper.job_exit_code(p), p)

	out = helper.job_stdout(p)
	test.check(re.match(r'.*not supported.*', out), p)
