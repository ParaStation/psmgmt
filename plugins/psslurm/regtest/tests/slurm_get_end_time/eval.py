#!/usr/bin/env python

import sys
import os
import datetime

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == 1, p)

	end1 = datetime.datetime.strptime(lines[0], "%Y-%m-%dT%H:%M:%S")
	end2 = datetime.datetime.strptime(os.environ["PSTEST_SCONTROL_%s_END_TIME" % p.upper()], "%Y-%m-%dT%H:%M:%S")
	diff = abs(end1 - end2)

	# Due to the to CEST/CET it is not so easy to convert end1 from UTC so we just
	# match it coarsely.
	test.check(diff < datetime.timedelta(hours = 2, minutes = 2), p)

test.quit()
