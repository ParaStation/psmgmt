#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("0:1" == helper.job_exit_code(p), p)
	test.check("15"  == helper.submit_exit_code(p), p)


test.quit()

