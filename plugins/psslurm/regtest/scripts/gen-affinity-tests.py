#!/usr/bin/env python

import os
import sys
import stat


prog = """
#define _GNU_SOURCE

#if 1 == CPU_MASK
# include <sched.h>
#endif
#if 1 == MEM_MASK
# include <numa.h>
#endif

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

#if 1 == CPU_MASK
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

#endif

#if 1 == MEM_MASK
unsigned int get_mem_affinity_mask()
{
	struct bitmask *bmask;
	int i;
	unsigned int mask;

	bmask = numa_get_membind();

	mask = 0;
	for (i = 0; i < 32; ++i)
		mask |= ((!!numa_bitmask_isbitset(bmask, i)) << i);

	return mask;
}
#endif

#if 1 == CPU_MASK
unsigned int get_affinity_mask()
{
	return get_cpu_affinity_mask();
}
#endif
#if 1 == MEM_MASK
unsigned int get_affinity_mask()
{
	return get_mem_affinity_mask();
}
#endif

int main(int argc, char** argv)
{
	printf("%d 0x%x\\n", get_process_rank(), get_affinity_mask());
	return 0;
}
"""

tests = {
	"cpu_bind-default"	: ["", "", [0x1, 0x2], "-DCPU_MASK=1", ""],
	"cpu_bind-threads"	: ["--cpu_bind=threads", "", [0x1, 0x2], "-DCPU_MASK=1", ""],	# Differs from standard Slurm
	"mem_bind-rank"		: ["--mem_bind=rank", "", [0x1, 0x2], "-DMEM_MASK=1", "-lnuma"],
	"cpu_bind-none"		: ["--cpu_bind=none", "", [0xFFFFFFFF, 0xFFFFFFFF], "-DCPU_MASK=1", ""],
	"cpu_bind-map_cpu"	: ["--cpu_bind=map_cpu:0,2", "", [0x1, 0x4], "-DCPU_MASK=1", ""],
	"cpu_bind-mask_cpu"	: ["--cpu_bind=mask_cpu:0x3,0xC", "", [0x3, 0xC], "-DCPU_MASK=1", ""],
	"cpu_bind-rank_ldom"	: ["--cpu_bind=rank_ldom", "", [0x00FF00FF, 0xFF00FF00], "-DCPU_MASK=1", ""],
	"cpu_bind-map_ldom"	: ["--cpu_bind=map_ldom:0,1", "", [0x00FF00FF, 0xFF00FF00], "-DCPU_MASK=1", ""],
	"cpu_bind-mask_ldom"	: ["--cpu_bind=mask_ldom:0x1,0x2", "", [0x00FF00FF, 0xFF00FF00], "-DCPU_MASK=1", ""],
	"cpu_bind-socket"	: ["--cpu_bind=socket", "", [0x00FF00FF, 0xFF00FF00], "-DCPU_MASK=1", ""],
	"cpu_bind-cores"	: ["--cpu_bind=cores", "", [None, None], "-DCPU_MASK=1", ""],	# Differs from standard Slurm
	"cpu_bind-ldoms"	: ["--cpu_bind=ldoms", "", [0x00FF00FF, 0xFF00FF00], "-DCPU_MASK=1", ""],
	"cpu_bind-boards"	: ["--cpu_bind=boards", "", [0xFFFFFFFF, 0xFFFFFFFF], "-DCPU_MASK=1", ""],	# Differs from standard Slurm
	"mem_bind-default"	: ["", "", [0x3, 0x3], "-DMEM_MASK=1", "-lnuma"],
	"mem_bind-none"		: ["--mem_bind=none", "", [0x3, 0x3], "-DMEM_MASK=1", "-lnuma"],
	"mem_bind-local"	: ["--mem_bind=local", "--ntasks-per-socket=1", [0x1, 0x2], "-DMEM_MASK=1", "-lnuma"],
	"mem_bind-map_mem"	: ["--mem_bind=map_mem:0,1", "", [0x1, 0x2], "-DMEM_MASK=1", "-lnuma"],
	"mem_bind-mask_mem"	: ["--mem_bind=mask_mem:0x2,0x3", "", [0x2, 0x3], "-DMEM_MASK=1", "-lnuma"]
}

for k, v in tests.iteritems():
	if not os.path.isdir(k):
		os.mkdir(k)

	open("%s/descr.json" % k, "w").write("""{
	"type":	"batch",
	"submit": "salloc -N 1 -t 1 ./test.sh",
	"eval": ["eval.py"],
	"fproc": null,
	"monitor_hz": 10
}
""")

	open("%s/prog.c" % k, "w").write(prog)

	tmp = ""
	if len(v[1]) > 0:
		tmp = v[1] + " "

	open("%s/test.sh" % k, "w").write("""#!/bin/bash

JOB_NAME=$(scontrol show job ${SLURM_JOB_ID} | head -n 1 | awk '{print $2}' | sed 's/Name=//g')

gcc %s prog.c -o output-${JOB_NAME}/prog.exe %s
srun -n %d %s %soutput-${JOB_NAME}/prog.exe

""" % (v[3], v[4], len(v[2]), v[0], tmp))
	os.chmod("%s/test.sh" % k, stat.S_IRWXU)

	evl = """#!/usr/bin/env python

import sys
import os
"""

	if not v[2][0]:
		evl += """import re
"""

	evl += """
sys.path.append("/".join(os.path.abspath(os.path.dirname(sys.argv[0])).split('/')[0:-2] + ["lib"]))
from testsuite import *

helper.pretty_print_env()

for p in helper.partitions():
"""

	if not v[2][0]:
		evl += """	test.check("FAILED" == helper.job_state(p), p)
	test.check("1:0"    == helper.job_exit_code(p), p)

	out = helper.job_stdout(p)
	test.check(re.match(r'.*not supported.*', out), p)
"""
	else:
		evl += """	helper.check_job_completed_ok(p)

	lines = helper.job_stdout_lines(p)
	test.check(len(lines) == %d, p)

	try:
		count = 0
		for line in lines:
			tmp = line.split()
""" % len(v[2])

		for i, x in enumerate(v[2]):
			evl += """			if %d == int(tmp[0]):
				test.check(%d == int(tmp[1], base = 16), p)
				count += 1
""" % (i, x)

		evl += """		test.check(len(lines) == count, p)
	except Exception as e:
		test.check(1 == 0, p + ": " + str(e))

test.quit()
"""

	open("%s/eval.py" % k, "w").write(evl)
	os.chmod("%s/eval.py" % k, stat.S_IRWXU)

