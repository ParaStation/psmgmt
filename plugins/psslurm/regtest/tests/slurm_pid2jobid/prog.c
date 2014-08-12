
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>


int main (int argc, char **argv)
{
	uint32_t jobid;

	if (slurm_pid2jobid(getpid(), &jobid))
		slurm_perror("slurm_load_jobs error");
	else
		printf("Slurm job id = %u\n", jobid);

	return 0;
}
