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

	q = subprocess.Popen(["sacct", "-n", "-p", "-o", "ExitCode,DerivedExitCode,Comment", "-j", helper.job_id(p)], \
	                     stdout = subprocess.PIPE,
	                     stderr = subprocess.PIPE)
	tmp, _ = q.communicate()
	q.wait()

	expected = [["0:0", "123:0", ""], ["0:0", "", ""], ["0:0", "", ""], ["123:0", "", ""]]

	for i, line in enumerate([x for x in map(lambda z: z.strip(), tmp.split("\n")) if len(x) > 0]):
		tmp = line.split('|')

		print(tmp)

		test.check(expected[i][0] == tmp[0], p)
		test.check(expected[i][1] == tmp[1], p)
		test.check(expected[i][2] == tmp[2], p)

test.quit()

