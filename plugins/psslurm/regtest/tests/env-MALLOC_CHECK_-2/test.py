#!/usr/bin/env python

import sys
import os

fail = 0

if not "MALLOC_CHECK_" in os.environ.keys():
	fail = 1

sys.exit(fail)

