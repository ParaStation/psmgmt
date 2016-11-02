#!/usr/bin/env python

import sys
import os
import subprocess

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	test.check("123:0" == os.environ["PSTEST_SCONTROL_%s_DERIVED_EXIT_CODE" % p.upper()], p)

	sacct    = helper.job_sacct_record(p)
	expected = [["0:0", "123:0", ""], ["0:0", "", ""], ["0:0", "", ""], ["123:0", "", ""]]

	test.check(len(sacct) == len(expected), p)

	for x, s in zip(expected, sacct):
		test.check(x[0] == s["ExitCode"], p)
		test.check(x[1] == s["DerivedExitCode"], p)
		test.check(x[2] == s["Comment"], p)

test.quit()

