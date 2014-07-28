
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
	printf("%d 0x%x\n", get_process_rank(), get_affinity_mask());
	return 0;
}
