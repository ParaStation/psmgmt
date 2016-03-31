#!/usr/bin/python2

import os
import sys
import subprocess
import select
import time
import multiprocessing
import re

def fst(x):
        return x[0]

def snd(x):
        return x[1]

def trd(x):
	return x[2]

def createProcess(argv):
        p = subprocess.Popen(argv,
                             stdout = subprocess.PIPE,
                             stderr = subprocess.PIPE)

        return p

def waitForProcess(p):
        o, e = p.communicate()
        x = p.wait()

        return (x, o, e)

def createProcessAndWait(argv):
        return waitForProcess(createProcess(argv))

def srun(step):
	if step > 0:
		time.sleep(0.5)

        return createProcessAndWait(["/usr/bin/srun", "-n", "1", "ps", "-AF", "f"])

if __name__ == '__main__':
        rx  = re.compile(r'.*pshealthcheck.*', re.DOTALL | re.MULTILINE)
        out = multiprocessing.Pool(processes = 8).map(srun, range(8))

	with open ("%s/summary.txt" % os.environ["PSTEST_OUTDIR"], "w") as f:
		f.write("srun exit code: %s\n" % str([fst(z) for z in out]))
		f.write("srun stdout:\n")
		for i, (_, z, _) in enumerate(out):
			f.write("%d:\n" % i)
			f.write("\n".join(map(lambda u: "\t" + u, z.split("\n"))))
		f.write("srun stderr:\n")
		for i, (_, _, z) in enumerate(out):
			f.write("%d:\n" % i)
			f.write("\n".join(map(lambda u: "\t" + u, z.split("\n"))))

        if len(filter(lambda z: fst(z), out)) > 0:
                sys.stderr.write("FAIL (srun exit codes)\n")
                sys.exit(1)
        if len(filter(lambda z: re.match(rx, snd(z)), out)) > 0:
                sys.stderr.write("FAIL (output)\n")
                sys.exit(1)

        sys.stdout.write("OK\n")
        sys.exit(0)

