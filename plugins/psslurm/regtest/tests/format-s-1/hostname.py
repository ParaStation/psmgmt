#!/usr/bin/env python

import sys
import os
import socket

procid = None
if "SLURM_PROCID" in os.environ:
	procid = int(os.environ["SLURM_PROCID"])
if None == procid:
	procid = int(os.environ["PMI_RANK"])


sys.stdout.write("%d %s %s\n" % (procid, socket.gethostname(), sys.argv[1]))
sys.stderr.write("%d %s %s\n" % (procid, socket.gethostname(), sys.argv[1]))

