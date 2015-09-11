#!/usr/bin/env python

import sys
import os
import subprocess


N = 4096
L = 256

env = os.environ.copy()
for i in range(N):
	env["X%08d" % i] = "a"*L

cmd = ["srun", "-n", "1", "true"]

p = subprocess.Popen(cmd, env = env)

sys.exit(p.wait())

