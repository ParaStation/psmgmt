

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

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


int main(int argc, char** argv)
{
        int rank;
        struct timespec ts;
 
        rank = get_process_rank();

        if (1 == rank) {
                kill(getpid(), SIGTRAP);
        } else {
                memset(&ts, 0, sizeof(ts));
                ts.tv_sec = 2;
                nanosleep(&ts, NULL);
        }

        return 0;
}
