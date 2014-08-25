#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

env = {}

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)

	d = {}

	for line in lines:
		x = line.split("=")
		z = x[0].strip()
		if len(z) > 0:
			d[z] = "=".join(x[1:])

	env[p] = d

for p, e in env.iteritems():
	print("%s:" % p)
	helper.pretty_print_dict(e)

for p, e in env.iteritems():
	for q, f in env.iteritems():
		if p == q:
			continue

		for k, v in e.iteritems():
			test.check(k in f.keys(), "k = %s, p = %s, q = %s" % (k, p, q))

test.quit()

