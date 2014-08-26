#!/usr/bin/env python

import os
import sys
import subprocess
import time

i = 0
while i < 10:
	p = subprocess.Popen(["hostname"], stdout = subprocess.PIPE)
	o, _ = p.communicate()
	p.wait()

	print("%d" % os.getpid() + " on " + o.strip())

	time.sleep(1)
	i += 1

print("i = %d" % i)

