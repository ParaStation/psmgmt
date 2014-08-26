#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	fn = os.environ["PSTEST_SCONTROL_%s_STD_OUT" % p.upper()]
	if os.path.isfile(fn):
		os.rename(fn, os.environ["PSTEST_OUTDIR"] + "/%s" % os.path.basename(fn))
	fn = os.environ["PSTEST_SCONTROL_%s_STD_ERR" % p.upper()]
	if os.path.isfile(fn):
		os.rename(fn, os.environ["PSTEST_OUTDIR"] + "/%s" % os.path.basename(fn))

test.quit()

