#!/usr/bin/env python

import sys
import os
import time
import signal


QUIT = 0

def handleHUP(signum, frame):
	global QUIT
	sys.stdout.write("Received signal %d\n" % signum)
	QUIT = 1

signal.signal(signal.SIGHUP, handleHUP)

while 0 == QUIT:
	time.sleep(1)

sys.exit(0)

