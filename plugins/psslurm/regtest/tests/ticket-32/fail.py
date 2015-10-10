#!/usr/bin/env python

import sys
import os
import time

fd = None
if "PMI_FD" in os.environ.keys():
	fd = int(os.environ["PMI_FD"])

if fd:
	os.write(fd, "cmd=init pmi_version=1 pmi_subversion=1\n")
	x = ""
	while not "\n" in x:
		x = os.read(fd, 1024)

procid = None
if "SLURM_PROCID" in os.environ:
	procid = int(os.environ["SLURM_PROCID"])
if None == procid:
	procid = int(os.environ["PMI_RANK"])

time.sleep(10)

if fd:
	os.write(fd, "cmd=finalize\n")
	x = ""
	while not "\n" in x:
		x = os.read(fd, 1024)

sys.exit(procid + 1)

