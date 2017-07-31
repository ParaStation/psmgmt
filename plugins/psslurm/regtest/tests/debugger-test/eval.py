#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("TIMEOUT" == helper.job_state(p), p)
        if helper.slurm_version().startswith("17.02"):
            test.check("0:15" == helper.job_exit_code(p), p)
        else:
	    test.check("0:1"     == helper.job_exit_code(p), p)

	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]

	for line in lines:
		test.check("NOK" != line, p)

test.quit()

