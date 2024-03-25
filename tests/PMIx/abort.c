#include <pmix.h>

#include <stdio.h>
#include <stdlib.h>

#define GET_INFO(name)				\
    if (PMIX_SUCCESS != (rc = PMIx_Get(&scope, name, NULL, 0, &val))) { \
	printf("Get "#name" failed: %s\n", PMIx_Error_string(rc));	\
	exit(1);							\
    }									\
    printf("[%u]: "#name" is %d\n", proc.rank, val->data.uint32);

int main(void)
{
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_proc_t proc;

    if (PMIX_SUCCESS != (rc = PMIx_Init(&proc, NULL, 0))) {
	printf("[%s:%u]: PMIx_Init failed: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	exit(1);
    }

    printf("[%s:%u]: Running\n", proc.nspace, proc.rank);

    pmix_proc_t scope;
    PMIX_PROC_CONSTRUCT(&scope);
    PMIX_LOAD_PROCID(&scope, proc.nspace, PMIX_RANK_WILDCARD);

    /* get various information */
    GET_INFO(PMIX_UNIV_SIZE);
    GET_INFO(PMIX_JOB_SIZE);
    GET_INFO(PMIX_APP_SIZE);

    if (proc.rank == 0) {
	sleep(2);
	PMIx_Abort(-1, "Just aborting!", NULL, 0);
    }

    sleep(5);
    printf("[%s:%u]: ERROR: still running, so PMIx_Abort() did not work\n",
	   proc.nspace, proc.rank);

    if (PMIX_SUCCESS != (rc = PMIx_Finalize(NULL, 0))) {
	printf("[%s:%u]: PMIx_Finalize failed: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	exit(1);
    }
    return (rc);
}
