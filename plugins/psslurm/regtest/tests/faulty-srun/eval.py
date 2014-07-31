#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("FAILED" == helper.job_state(p), p)
	test.check("2:0"    == helper.job_exit_code(p), p)


test.quit()

