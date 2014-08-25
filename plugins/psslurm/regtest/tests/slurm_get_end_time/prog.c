
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>


int main(int argc, char **argv)
{
	uint32_t jobid;
	time_t time;
	struct tm *tm;

	jobid = strtol(argv[1], NULL, 0);

	if (slurm_get_end_time(jobid, &time))
		slurm_perror("slurm_get_end_time error");
	else {
		tm = gmtime(&time);

		printf("%d-%02d-%02dT%02d:%02d:%02d\n",
		       tm->tm_year + 1900,
		       tm->tm_mon + 1,
		       tm->tm_mday,
		       tm->tm_hour,
		       tm->tm_min,
		       tm->tm_sec);
	}

	return 0;
}

