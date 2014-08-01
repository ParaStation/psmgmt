#!/usr/bin/env python

import sys
import signal

def quit(*_):
	sys.exit(0)

signal.signal(signal.SIGUSR1, quit)

sys.stdout.write("srun -n 1 hostname\n")
sys.stdout.flush()

sys.stdout.write("echo $?\n")
sys.stdout.flush()

sys.stdout.write("exit\n")
sys.stdout.flush()

signal.pause()

sys.exit(1)

