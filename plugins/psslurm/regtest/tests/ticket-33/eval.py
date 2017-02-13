#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	exp = "130"	# 128 (signaled) + 2
	if helper.slurm_version().startswith("14.03"):
		exp = "2"

	test.check(exp == helper.fproc_exit_code(p), p)

test.quit()

