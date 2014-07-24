#!/usr/bin/env python

import os
import sys
import time

procid = None
if "SLURM_PROCID" in os.environ:
	procid = int(os.environ["SLURM_PROCID"])
if None == procid:
	procid = int(os.environ["PMI_RANK"])

time.sleep(10)

if 5 == procid:
	sys.exit(1)

time.sleep(10)

raise Exception("FAIL")

