#!/usr/bin/env python

import os
import sys
import time
import datetime

procid = None
if "SLURM_PROCID" in os.environ:
	procid = int(os.environ["SLURM_PROCID"])
if None == procid:
	procid = int(os.environ["PMI_RANK"])

time.sleep(10)

if 5 == procid:
	sys.exit(0)

while 1:
	time.sleep(10)
	if 1 == procid:
		sys.stdout.write("Alive at %s\n" % datetime.datetime.now().isoformat())
		sys.stdout.flush()

sys.exit(1)

