#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)
		
	jobid = helper.job_id(p)

	prologf = "%s/prolog-%s.txt" % (os.environ["PSTEST_OUTDIR"], jobid)
	epilogf = "%s/epilog-%s.txt" % (os.environ["PSTEST_OUTDIR"], jobid)

	out = ""
	try:
		out = open(prologf).read()
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))
	test.check("OK\n" == out, p)

	out = ""
	try:
		out = open(epilogf).read()
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))
	test.check("OK\n" == out, p)


test.quit()

