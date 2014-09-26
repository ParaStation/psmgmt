#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]
	nodes = helper.job_node_list(p)

	dd = {}

	for node in nodes:
		dd[node] = 0

	for line in lines:
		dd[line] += 1

	print(dd)

	for node in nodes:
		test.check(2 == dd[node], p)

test.quit()

