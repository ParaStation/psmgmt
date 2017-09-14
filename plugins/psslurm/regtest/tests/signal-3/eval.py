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
            test.check("0:15"     == helper.job_exit_code(p), p)
        else:
	    test.check("0:1"     == helper.job_exit_code(p), p)

	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p) and \
	                                                  not re.match(r'.*error.*', x)]

	test.check(1 == len(lines), p)

	time = int(lines[0])
	test.check(time < 120, p + ": time = %d" % time)


test.quit()

