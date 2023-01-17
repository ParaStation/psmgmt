#include <pmix.h>
#include <stdbool.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

int main(int argc, char **argv)
{
    int rc;
    pmix_value_t *val;
    pmix_value_t pvalue;
    pmix_proc_t proc;
    pmix_info_t info;

    if (PMIX_SUCCESS != (rc = PMIx_Init(&proc, NULL, 0))) {
	printf("[%s:%u] PMIx_Init failed: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	exit(1);
    }

    printf("[%s:%u] Running\n", proc.nspace, proc.rank);

    /* get the #processes in our job */
    pmix_proc_t scope;
    PMIX_PROC_CONSTRUCT(&scope);
    PMIX_LOAD_PROCID(&scope, proc.nspace, PMIX_RANK_WILDCARD);

    if (PMIX_SUCCESS != (rc = PMIx_Get(&scope, PMIX_JOB_SIZE, NULL, 0, &val))) {
	printf("[%s:%u] Get PMIX_JOB_SIZE failed: %s\n",
	       proc.nspace, proc.rank, PMIx_Error_string(rc));
	exit(1);
    }

    printf("[%u] PMIX_JOB_SIZE is %d\n", proc.rank, val->data.uint32);
    uint32_t jobSize = val->data.uint32;
    fflush(stdout);

    char valStr[32];
    snprintf(valStr, sizeof(valStr), "hello%d", proc.rank * 100);
    PMIX_VALUE_LOAD(&pvalue, valStr, PMIX_STRING);
    if (PMIX_SUCCESS != (rc = PMIx_Put(PMIX_GLOBAL, "my.foo", &pvalue))) {
	printf("[%s:%u] Put my.foo failed: %s\n",
	       proc.nspace, proc.rank, PMIx_Error_string(rc));
	exit(1);
    }

    PMIx_Commit();

    bool tvalue = true;
    strcpy(info.key, PMIX_COLLECT_DATA);
    PMIX_VALUE_LOAD(&(info.value), &tvalue, PMIX_BOOL);
    PMIx_Fence(NULL, 0, &info, 1);

    printf("[%s:%u] Fence complete\n", proc.nspace, proc.rank);

    for (uint32_t r = 0; r < jobSize; r++) {
	scope.rank = r;

	if (PMIX_SUCCESS != PMIx_Get(&scope, "my.foo", NULL, 0, &val)) {
	    printf("Get 'my.foo' from rank %d failed\n", r);
	    exit(1);
	}
	printf("[%u] Got '%s'for rank %d\n", proc.rank, val->data.string, r);
    }

    printf("[%s:%u] complete and successful\n", proc.nspace, proc.rank);

    if (PMIX_SUCCESS != (rc = PMIx_Finalize(NULL, 0))) {
	printf("[%s:%u] PMIx_Finalize failed: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	exit(1);
    }

    return rc;
}
