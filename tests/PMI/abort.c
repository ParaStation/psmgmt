#include <stdio.h>
#include <unistd.h>

#include "pmi.h"

int main(void)
{
    int has_parent;
    int rc = PMI_Init(&has_parent);
    if (rc != PMI_SUCCESS) return -1;

    int rank;
    PMI_Get_rank(&rank);
    int size;
    PMI_Get_size(&size);

    printf("[%d] size %d %sparent\n", rank, size, has_parent ? "" : "no ");

    PMI_Abort(rank, "killing");
    printf("[%d] return from abort\n", rank);
    fflush(stdout);

    printf("[%d]: Ending now\n", rank);

    if (!rank) sleep(5);

    PMI_Finalize();

    return 0;
}
