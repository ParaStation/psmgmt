#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("TIMEOUT" == helper.job_state(p), p)
        if helper.slurm_version().startswith("17.02"):
            test.check("0:15" == helper.job_exit_code(p) or "143:0" == helper.job_exit_code(p), p)
        else:
	    test.check("0:1" == helper.job_exit_code(p), p)

	sacct = helper.job_sacct_record(p)
	test.check(3 == len(sacct), p)

	d = sacct[0]
	
	print("%s:" % p)
	helper.pretty_print_dict(d)

	test.check("" == d["MaxPagesNode"], p)
	test.check("" == d["MaxPagesTask"], p)
	test.check("" == d["MaxRSSNode"], p)
	test.check("" == d["MaxRSSTask"], p)
	test.check("" == d["MaxVMSizeNode"], p)
	test.check("" == d["MaxVMSizeTask"], p)
	test.check("" == d["MinCPUNode"], p)
	test.check("" == d["MinCPUTask"], p)
	test.check("" == d["MaxVMSize"], p)
	test.check("00:00:00" != d["TotalCPU"], p)
	test.check("00:00:00" != d["UserCPU"], p)
	test.check("00:00:00" != d["SystemCPU"], p)

	for d in sacct[1:]:
		print("%s:" % p)
		helper.pretty_print_dict(d)

		test.check(len(d["MaxPagesNode"]) > 0, p)
		test.check(0 == int(d["MaxPagesTask"]), p)
		test.check(len(d["MaxRSSNode"]) > 0, p)
		test.check(0 == int(d["MaxRSSTask"]), p)
		test.check(len(d["MaxVMSizeNode"]) > 0, p)
		test.check(0 == int(d["MaxVMSizeTask"]), p)
		test.check(len(d["MinCPUNode"]) > 0, p)
		test.check(0 == int(d["MinCPUTask"]), p)
		test.check(not re.match(r'.*G', d["MaxVMSize"]), p)
		test.check("00:00:00" != d["TotalCPU"], p)
		test.check("00:00:00" != d["UserCPU"], p)

test.quit()

