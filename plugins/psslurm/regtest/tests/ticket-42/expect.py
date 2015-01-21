#!/usr/bin/env python

import os
import sys
import subprocess


srun = ["srun"]
if "" != os.environ["PSTEST_PARTITION"]:
	srun += ["--partition", os.environ["PSTEST_PARTITION"]]
if "" != os.environ["PSTEST_RESERVATION"]:
	srun += ["--reservation", os.environ["PSTEST_RESERVATION"]]
if "" != os.environ["PSTEST_QOS"]:
	srun += ["--qos", os.environ["PSTEST_QOS"]]
if "" != os.environ["PSTEST_ACCOUNT"]:
	srun += ["--account", os.environ["PSTEST_ACCOUNT"]]

cmd = srun + ["-N", "2", "-t", "1", "--pty", "/bin/bash"]

stdin = """
set timeout -1

spawn %s

expect -re "queued and waiting for resources"
expect -re "has been allocated resources"
expect -re "bash" { send "exit\n" }
expect eof

catch wait result
exit [lindex $result 3]
""" % " ".join(cmd)

p = subprocess.Popen(["/usr/bin/expect", "-"], stdin = subprocess.PIPE)
p.communicate(stdin)

sys.exit(p.wait())

