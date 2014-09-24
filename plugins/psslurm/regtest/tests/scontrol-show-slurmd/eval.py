#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	nodes = helper.job_node_list(p)

	dd = {}

	for line in lines:
		tmp = [x for x in map(lambda z: z.strip(), line.split("=")) if x]
		dd[tmp[0]] = tmp[1]

	test.check(nodes[0] == dd["Hostname"], p)
	test.check("%s.0" % helper.job_id(p) == dd["Active Steps"], p)

test.quit()

