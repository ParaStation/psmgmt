#include <pmix.h>

#include <stdio.h>
#include <stdlib.h>

#define GET_INFO(name)				\
    if (PMIX_SUCCESS != (rc = PMIx_Get(&scope, name, NULL, 0, &val))) { \
	printf("Get "#name" failed: %s\n", PMIx_Error_string(rc));	\
    } else {								\
	printf("[%u]: "#name" is %d\n", proc.rank, val->data.uint32);	\
    }

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
    GET_INFO(PMIX_JOB_SIZE);
    PMIX_LOAD_PROCID(&scope, proc.nspace, proc.rank);
    GET_INFO(PMIX_APPNUM);

    size_t ndata = 1;
    pmix_info_t data[ndata];
    PMIX_INFO_LOAD(&data[0], PMIX_LOG_SYSLOG, "Test output for syslog", PMIX_STRING);

    size_t ndirs = 1;
    pmix_info_t dirs[ndirs];
    bool bv = 1;
    PMIX_INFO_LOAD(&dirs[0], PMIX_LOG_GENERATE_TIMESTAMP, &bv, PMIX_BOOL);


    if (PMIX_SUCCESS != (rc = PMIx_Log(data, ndata, dirs, ndirs))) {
	printf("[%s:%u]: PMIx_Log: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	exit(1);
    }

    if (PMIX_SUCCESS != (rc = PMIx_Finalize(NULL, 0))) {
	printf("[%s:%u]: PMIx_Finalize failed: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	exit(1);
    }
    return (rc);
}
