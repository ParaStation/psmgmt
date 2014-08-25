#!/usr/bin/env python

import os
import sys
import stat


prog = """
#define _GNU_SOURCE

#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <time.h>


int get_process_rank()
{
	const char* str;
	char* tailptr;

	str = getenv("SLURM_PROCID");
	if (str) {
		return (int )strtol(str, &tailptr, 0);
	}

	str = getenv("PMI_RANK");
	if (str) {
		return (int )strtol(str, &tailptr, 0);
	}

	return -1;
}

unsigned int get_cpu_affinity_mask()
{
	cpu_set_t set;
	int i;
	unsigned int mask;

	sched_getaffinity(getpid(), sizeof(set), &set);

	mask = 0;
	for (i = 0; i < 32; ++i)
	        mask |= ((!!CPU_ISSET(i, &set)) << i);

	return mask;
}

int main(int argc, char** argv)
{
	char host[1024];
	gethostname(host, sizeof(host));

	printf("%d %s 0x%x\\n", get_process_rank(), host, get_cpu_affinity_mask());
	return 0;
}
"""

tests = {
	"hint-compute_bound"	: [2, 32, "--hint=compute_bound", [[0, 0x10001],[0, 0x1000100],[0, 0x10001],[0, 0x1000100],[0, 0x20002],[0, 0x2000200],[0, 0x20002],[0, 0x2000200],[0, 0x40004],[0, 0x4000400],[0, 0x40004],[0, 0x4000400],[0, 0x80008],[0, 0x8000800],[0, 0x80008],[0, 0x8000800],[1, 0x10001],[1, 0x1000100],[1, 0x10001],[1, 0x1000100],[1, 0x20002],[1, 0x2000200],[1, 0x20002],[1, 0x2000200],[1, 0x40004],[1, 0x4000400],[1, 0x40004],[1, 0x4000400],[1, 0x80008],[1, 0x8000800],[1, 0x80008],[1, 0x8000800]]],
	"hint-memory_bound"	: [2, 32, "--hint=memory_bound", [[0, 0x10001],[0, 0x1000100],[0, 0x10001],[0, 0x1000100],[0, 0x20002],[0, 0x2000200],[0, 0x20002],[0, 0x2000200],[0, 0x40004],[0, 0x4000400],[0, 0x40004],[0, 0x4000400],[0, 0x80008],[0, 0x8000800],[0, 0x80008],[0, 0x8000800],[1, 0x10001],[1, 0x1000100],[1, 0x10001],[1, 0x1000100],[1, 0x20002],[1, 0x2000200],[1, 0x20002],[1, 0x2000200],[1, 0x40004],[1, 0x4000400],[1, 0x40004],[1, 0x4000400],[1, 0x80008],[1, 0x8000800],[1, 0x80008],[1, 0x8000800]]],
	"hint-multithread"	: [2, 32, "--hint=multithread", [[0, 0x1],[0, 0x100],[0, 0x10000],[0, 0x1000000],[0, 0x2],[0, 0x200],[0, 0x20000],[0, 0x2000000],[0, 0x4],[0, 0x400],[0, 0x40000],[0, 0x4000000],[0, 0x8],[0, 0x800],[0, 0x80000],[0, 0x8000000],[1, 0x1],[1, 0x100],[1, 0x10000],[1, 0x1000000],[1, 0x2],[1, 0x200],[1, 0x20000],[1, 0x2000000],[1, 0x4],[1, 0x400],[1, 0x40000],[1, 0x4000000],[1, 0x8],[1, 0x800],[1, 0x80000],[1, 0x8000000]]],
	"hint-nomultithread"	: [2, 32, "--hint=nomultithread", [[0, 0x1],[0, 0x100],[0, 0x2],[0, 0x200],[0, 0x4],[0, 0x400],[0, 0x8],[0, 0x800],[0, 0x10],[0, 0x1000],[0, 0x20],[0, 0x2000],[0, 0x40],[0, 0x4000],[0, 0x80],[0, 0x8000],[1, 0x1],[1, 0x100],[1, 0x2],[1, 0x200],[1, 0x4],[1, 0x400],[1, 0x8],[1, 0x800],[1, 0x10],[1, 0x1000],[1, 0x20],[1, 0x2000],[1, 0x40],[1, 0x4000],[1, 0x80],[1, 0x8000]]]
}

for k, v in tests.iteritems():
	if not os.path.isdir(k):
		os.mkdir(k)

	open("%s/descr.json" % k, "w").write("""{
	"type":	"batch",
	"partitions": ["batch", "psslurm"],
	"reservations": ["", "psslurm"],
	"submit": "salloc -N %d -t 1 ./test.sh",
	"eval": ["eval.py"],
	"fproc": null,
	"monitor_hz": 10
}
""" % v[0])

	open("%s/prog.c" % k, "w").write(prog)

	open("%s/test.sh" % k, "w").write("""#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc prog.c -o output-${JOB_NAME}/prog.exe
srun -n %d %s output-${JOB_NAME}/prog.exe

""" % (v[1], v[2]))
	os.chmod("%s/test.sh" % k, stat.S_IRWXU)

	evl = """#!/usr/bin/env python

import sys
import os

sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == %d, p)

	nodes = helper.job_node_list(p)

	nodeno = {}
	for i, n in enumerate(nodes):
		nodeno[n] = i

	try:
		result = [None] * len(lines)
		for line in lines:
			rank, host, mask = line.split()
			result[int(rank)] = [nodeno[host], int(mask, base = 16)]
""" % len(v[3])

	for i, x in enumerate(v[3]):
		evl += """
		test.check(%d == result[%d][0], p)
		test.check(%d == result[%d][1], p)
""" % (x[0], i, x[1], i)

	evl += """
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
"""

	open("%s/eval.py" % k, "w").write(evl)
	os.chmod("%s/eval.py" % k, stat.S_IRWXU)

