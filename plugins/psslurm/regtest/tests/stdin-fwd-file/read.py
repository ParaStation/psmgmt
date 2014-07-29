#!/usr/bin/env python

import os
import sys
import select

procid = None
if "SLURM_PROCID" in os.environ:
	procid = int(os.environ["SLURM_PROCID"])
if None == procid:
	procid = int(os.environ["PMI_RANK"])

ready, _, _ = select.select([sys.stdin], [], [], 60)
if len(ready) > 0:
	sys.stdout.write("%2d" % procid + "\t'" + sys.stdin.readline().strip() + "'\n")
	sys.exit(0)

sys.stdout.write("%2d" % procid + "\tFAIL\n")
sys.exit(1)

