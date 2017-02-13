
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>


int main(int argc, char **argv)
{
	uint32_t jobid;
	long rem;

	jobid = strtol(argv[1], NULL, 0);

	printf("%ld\n", slurm_get_rem_time(jobid));

	return 0;
}

