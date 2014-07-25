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

sys.exit(procid + 1)

