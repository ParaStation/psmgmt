#!/usr/bin/env python

import sys
import os
import re

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	test.check("0" == helper.fproc_exit_code(p))

	lines = helper.fproc_stdout_lines(p)

	test.check(64 == len(lines), p)
	test.check(64 == len([x for x in lines if re.match(r'.*\'OK\'.*', x)]), p)


test.quit()

