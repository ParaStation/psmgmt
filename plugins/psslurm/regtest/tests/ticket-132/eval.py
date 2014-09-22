#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	hosts = helper.job_node_list(p)
	lines = [x for x in map(lambda z: z.strip(), open("%s/mpir_proctable.txt" % os.environ["PSTEST_OUTDIR"], "r").read().split("\n")) if x]
	
	cnt = {}
	for h in hosts:
		cnt[h] = 0;

	for line in lines:
		h = line.split()[0]
		cnt[h] += 1

	for h in hosts:
		test.check(cnt[h] == int(helper.partition_cpus(p)), p)

test.quit()

