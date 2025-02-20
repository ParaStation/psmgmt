#include <pmix.h>

#include <stdio.h>
#include <stdlib.h>

#define GET_INFO(name)                                                        \
    if (PMIX_SUCCESS != (rc = PMIx_Get(&scope, name, NULL, 0, &val))) {       \
	printf("Get " #name " failed: %s\n", PMIx_Error_string(rc));          \
    } else {                                                                  \
	printf("[%u]: " #name " is %d\n", proc.rank, val->data.uint32);       \
    }

bool test_pmix_log(char *test_name, pmix_info_t *data, size_t ndata,
		   pmix_status_t expected_result)
{
    fprintf(stdout, "PMIx_Log(%s, ...)\n", test_name);
    fprintf(stderr, "PMIx_Log(%s, ...)\n", test_name);
    pmix_status_t rc = PMIx_Log(data, ndata, NULL, 0);
    if (PMIX_SUCCESS == rc) {
	printf("PMIX_SUCCESS [Expected: %s]", PMIx_Error_string(expected_result));
    } else {
	printf("%s [Expected: %s]", PMIx_Error_string(rc), PMIx_Error_string(expected_result));
    }
    if (rc == expected_result) {
	printf(" ... success\n");
    } else {
	printf(" ... failed\n");
    }
    return rc != expected_result;
}

bool test_pmix_log_channels(char *test_name, char **channels, size_t nchannels,
			    bool log_once, pmix_status_t expected_result)
{
    const char* str = "Test log";
    size_t ndata = log_once ? nchannels+1 : nchannels;
    pmix_info_t data[ndata];
    for(size_t ch = 0; ch < nchannels; ch++) {
	PMIX_INFO_LOAD(&data[ch], channels[ch], str, PMIX_STRING);
    }
    if(log_once) {
	bool true_value = true;
	PMIX_INFO_LOAD(&data[ndata-1], PMIX_LOG_ONCE, &true_value, PMIX_BOOL);
    }

    return test_pmix_log(test_name, data, ndata, expected_result);
}


int main(void)
{
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_proc_t proc;

    if (PMIX_SUCCESS != (rc = PMIx_Init(&proc, NULL, 0))) {
	printf("[%s:%u]: PMIx_Init failed: %s\n", proc.nspace,
	       proc.rank, PMIx_Error_string(rc));
	return 1;
    }

    printf("[%s:%u]: Running\n", proc.nspace, proc.rank);

    pmix_proc_t scope;
    PMIX_PROC_CONSTRUCT(&scope);
    PMIX_LOAD_PROCID(&scope, proc.nspace, PMIX_RANK_WILDCARD);

    /* get various information */
    GET_INFO(PMIX_JOB_SIZE);
    PMIX_LOAD_PROCID(&scope, proc.nspace, proc.rank);
    GET_INFO(PMIX_APPNUM);

    // Single request
    int err = 0;
    char *channels1[] = { PMIX_LOG_STDOUT };
    err += test_pmix_log_channels("[STDOUT]", channels1, 1, false,
				  PMIX_SUCCESS);
    char *channels2[] = { PMIX_LOG_STDERR };
    err += test_pmix_log_channels("[STDERR]", channels2, 1, false,
				  PMIX_SUCCESS);
    char *channels3[] = { PMIX_LOG_LOCAL_SYSLOG };
    err += test_pmix_log_channels("[LOCAL_SYSLOG]", channels3, 1, false,
				  PMIX_SUCCESS);
    char *channels4[] = { PMIX_LOG_GLOBAL_SYSLOG };
    err += test_pmix_log_channels("[GLOBAL_SYSLOG]", channels4, 1, false,
				  PMIX_ERROR); // Alternativly PMIX_ERR_NOT_SUPPORTED
    // Multiple request
    char *channels5[] = { PMIX_LOG_STDOUT, PMIX_LOG_STDERR };
    err += test_pmix_log_channels("[STDOUT, STDERR]", channels5, 2, false,
				  PMIX_SUCCESS);
    char *channels6[] = { PMIX_LOG_STDOUT, PMIX_LOG_GLOBAL_SYSLOG };
    err += test_pmix_log_channels("[STDOUT, GLOBAL_SYSLOG]", channels6, 2,
				  false, PMIX_ERR_PARTIAL_SUCCESS);
    char *channels7[] = { PMIX_LOG_SYSLOG, PMIX_LOG_GLOBAL_SYSLOG };
    err += test_pmix_log_channels("[SYSLOG, GLOBAL_SYSLOG]", channels7, 2,
				  false, PMIX_ERROR);
    // Multiple request with log once
    char *channels8[] = { PMIX_LOG_STDOUT, PMIX_LOG_STDERR };
    err += test_pmix_log_channels("[STDOUT, STDERR] log_once=true", channels8,
				  2, true, PMIX_SUCCESS);
    char *channels9[] = { PMIX_LOG_STDOUT, PMIX_LOG_GLOBAL_SYSLOG };
    err += test_pmix_log_channels("[STDOUT, GLOBAL_SYSLOG] log_once=true",
				  channels9, 2, true, PMIX_SUCCESS);
    char *channels10[] = { PMIX_LOG_GLOBAL_SYSLOG, PMIX_LOG_STDOUT };
    err += test_pmix_log_channels("[GLOBAL_SYSLOG, STDOUT] log_once=true",
				  channels10, 2, true, PMIX_SUCCESS);
    char *channels11[] = { PMIX_LOG_SYSLOG, PMIX_LOG_GLOBAL_SYSLOG };
    err += test_pmix_log_channels("[SYSLOG, GLOBAL_SYSLOG] log_once=true",
				  channels11, 2, true, PMIX_ERROR);
    char *channels12[] = { PMIX_LOG_LOCAL_SYSLOG };
    err += test_pmix_log_channels("[LOCAL_SYSLOG] log_once=true", channels12,
				  1, true, PMIX_SUCCESS);
    char *channels13[] = { PMIX_LOG_GLOBAL_SYSLOG };
    err += test_pmix_log_channels("[GLOBAL_SYSLOG], log_once=true", channels13,
				  1, true, PMIX_ERROR); // Alternativly PMIX_ERR_NOT_SUPPORTED

    if (PMIX_SUCCESS != (rc = PMIx_Finalize(NULL, 0))) {
	printf("[%s:%u]: PMIx_Finalize failed: %s\n", proc.nspace,
	       proc.rank, PMIx_Error_string(rc));
	return 1;
    }
    return (err);
}
