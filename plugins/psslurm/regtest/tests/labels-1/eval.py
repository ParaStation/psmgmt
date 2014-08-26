#!/usr/bin/env python

import sys
import os
import re
import pprint

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

stdout = {}

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = [x for x in helper.job_stdout_lines(p) if x != "Submitted batch job %s" % helper.job_id(p)]
	nodes = helper.job_node_list(p)

	tmp = []
	for line in lines:
		for node in nodes:
			line = re.sub(r'%s$' % node, "", line)
		tmp.append(line)

	stdout[p] = tmp

for p, e in stdout.iteritems():
	print("%s:" % p)
	pprint.pprint(e, indent = 1)

for p, e in stdout.iteritems():
	for q, f in stdout.iteritems():
		if p == q:
			continue

		test.check(len(e) == len(f))

		for x in e:
			test.check(1 == len([z for z in f if z.strip() == x.strip()]), \
			       "x = %s, p = %s, q = %s" % (x, p, q))

test.quit()

