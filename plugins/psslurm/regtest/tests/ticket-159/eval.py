#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check(re.match("[1-9]+:0", helper.job_exit_code(p)), p)

	rx = re.compile(r'.*unable to change directory to work directory.*', re.MULTILINE | re.DOTALL)
	err = helper.job_stderr(p)

	test.check(re.match(rx, err), p)

test.quit()

