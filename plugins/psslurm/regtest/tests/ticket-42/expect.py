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

cmd = srun + ["-v", "-N", "2", "-t", "1", "--pty", "/bin/bash"]

stdin = """
set timeout -1

spawn %s

expect -re "launching"
expect -re "tasks started"
expect -re "bash" { send "exit\n" }
expect eof

catch wait result
exit [lindex $result 3]
""" % " ".join(cmd)

env = os.environ.copy()
env["PS1"] = "bash$ "

p = subprocess.Popen(["/usr/bin/expect", "-"], stdin = subprocess.PIPE, env = env)
p.communicate(stdin)

sys.exit(p.wait())

