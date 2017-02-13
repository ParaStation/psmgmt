#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	pass

# Since we cannot assess whether the problem created problems on the compute
# nodes we just let the test pass. It is still a useful test in the suite, though,
# as it we can check the system health after running the test suite.

test.quit()

