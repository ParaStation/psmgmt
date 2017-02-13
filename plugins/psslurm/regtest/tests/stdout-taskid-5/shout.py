#!/usr/bin/env python

import os
import sys
import select

procid = None
if "SLURM_PROCID" in os.environ:
	procid = int(os.environ["SLURM_PROCID"])
if None == procid:
	procid = int(os.environ["PMI_RANK"])

sys.stdout.write("Hello from %s\n" % procid)
sys.exit(0)

