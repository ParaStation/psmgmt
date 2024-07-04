#include <pmix.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define GET_INFO(name)				\
    if (PMIX_SUCCESS != (rc = PMIx_Get(&scope, name, info, ninfo, &val))) { \
	printf("Get "#name" failed: %s\n", PMIx_Error_string(rc));	\
	exit(1);							\
    }									\
    printf("[%u]: "#name" is %d\n", proc.rank, val->data.uint32);

int main(void)
{
    pmix_status_t rc;
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
    pmix_value_t *val;
    pmix_info_t *info = NULL;
    size_t ninfo = 0;

    GET_INFO(PMIX_UNIV_SIZE);
    GET_INFO(PMIX_JOB_SIZE);

    PMIX_LOAD_PROCID(&scope, proc.nspace, proc.rank);
    GET_INFO(PMIX_APPNUM);

    PMIX_LOAD_PROCID(&scope, proc.nspace, PMIX_RANK_WILDCARD);
    ninfo = 2;
    PMIX_INFO_CREATE(info, ninfo);
    PMIX_INFO_LOAD(&(info[0]), PMIX_APP_INFO, NULL, PMIX_BOOL);
    PMIX_INFO_LOAD(&(info[1]), PMIX_APPNUM, &val->data.uint32, PMIX_UINT32);
    GET_INFO(PMIX_APP_SIZE);
    PMIX_INFO_FREE(info, ninfo);
    ninfo = 0;

    sleep(1);
    if (PMIX_SUCCESS != (rc = PMIx_Finalize(NULL, 0))) {
	printf("[%s:%u]: PMIx_Finalize failed: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	exit(1);
    }
    return (rc);
}
