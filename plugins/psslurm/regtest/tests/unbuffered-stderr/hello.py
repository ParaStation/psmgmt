#!/usr/bin/python -u

import os
import sys
import time

for x in "Hello World!":
	sys.stderr.write(x)
	time.sleep(1)

time.sleep(10)

sys.stderr.write("\n")

time.sleep(10)

sys.exit(0)

