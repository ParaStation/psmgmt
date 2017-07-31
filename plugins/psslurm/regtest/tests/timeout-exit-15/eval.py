#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	exp = "143"	# 128 (signaled) + 15
	if helper.slurm_version().startswith("14.03"):
		exp = "15"

        if helper.slurm_version().startswith("17.02"):
            test.check("0:15" == helper.job_exit_code(p), p)
        else:
	    test.check("0:1" == helper.job_exit_code(p), p)
	test.check(exp   == helper.submit_exit_code(p), p)

test.quit()

