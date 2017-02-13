#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("0" == helper.fproc_exit_code(p), p)

	try:
		out   = open("%s/slurm-%s.out" % (os.environ["PSTEST_OUTDIR"], p)).read()
		lines = [x for x in map(lambda z: z.strip(), out.split("\n")) if len(x) > 0]
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

	test.check("SIGINT" == lines[-1], p)


test.quit()

