#!/usr/bin/env python

import os
import sys
import signal
import time


start = time.time()

def handler(signum, frame):
	print("%d" % (time.time() - start))
	sys.exit(0)

signal.signal(signal.SIGTERM, handler)

while 1:
	pass

sys.exit(1)

