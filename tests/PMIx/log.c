#include <pmix.h>

#include <stdio.h>
#include <stdlib.h>

#define GET_INFO(name)                                                        \
    if (PMIX_SUCCESS != (rc = PMIx_Get(&scope, name, NULL, 0, &val))) {       \
	printf("Get " #name " failed: %s\n", PMIx_Error_string(rc));          \
    } else {                                                                  \
	printf("[%u]: " #name " is %d\n", proc.rank, val->data.uint32);       \
    }

bool doTest(int callNum, char *test_name, pmix_info_t *data, size_t nData,
	    pmix_info_t *directives, size_t nDir, pmix_status_t xpctdRes)
{
    fprintf(stdout, "#%d PMIx_Log(%s, ...)\n", callNum, test_name);
    fprintf(stderr, "#%d PMIx_Log(%s, ...)\n", callNum, test_name);
    pmix_status_t rc = PMIx_Log(data, nData, directives, nDir);
    printf("#%d, %s [Expected: %s]", callNum, PMIx_Error_string(rc),
	   PMIx_Error_string(xpctdRes));

    printf(" ... %s\n", rc == xpctdRes ? "success" : "failed");

    return rc != xpctdRes;
}

bool test_pmix_logs(char *test_name, char **chan, size_t nChan, bool logOnce,
		    bool genTime, time_t time, pmix_status_t xpctdRes)
{
    static int callNum = 0;
    char str[128];
    snprintf(str, sizeof(str), "----- Test log #%d: %s%s\n", ++callNum,
	     logOnce ? "one of " : "", test_name);

    pmix_info_t data[nChan];
    for (size_t ch = 0; ch < nChan; ch++) {
	PMIX_INFO_LOAD(&data[ch], chan[ch], str, PMIX_STRING);
    }
    pmix_info_t directives[5];
    size_t nDir = 0;
    if (logOnce) {
	bool trueVal = true;
	PMIX_INFO_LOAD(&directives[nDir++], PMIX_LOG_ONCE, &trueVal, PMIX_BOOL);
    }
    if (genTime) {
	bool trueVal = true;
	PMIX_INFO_LOAD(&directives[nDir++], PMIX_LOG_GENERATE_TIMESTAMP,
		       &trueVal, PMIX_BOOL);
    }
    if (time) {
	PMIX_INFO_LOAD(&directives[nDir++], PMIX_LOG_TIMESTAMP, &time, PMIX_TIME);
    }

    return doTest(callNum, test_name, data, nChan, directives, nDir,  xpctdRes);
}


int main(void)
{
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_proc_t proc;

    bool expectStderrFail = getenv("__PMIX_BREAK_STDERR");

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

    int err = 0;
    // Empty request (#1)
    {
	char *channels[] = { };
	err += test_pmix_logs("[]", channels, 0, false, false, 0,
			      PMIX_ERR_BAD_PARAM);
    }
    // Single request (#2)
    {
	char *channels[] = { PMIX_LOG_STDOUT };
	err += test_pmix_logs("[STDOUT]", channels, 1, false, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDERR };
	err += test_pmix_logs("[STDERR]", channels, 1, false, false, 0,
			      expectStderrFail ? PMIX_ERROR : PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_LOCAL_SYSLOG };
	err += test_pmix_logs("[LOCAL_SYSLOG]", channels, 1, false, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_GLOBAL_SYSLOG };
	err += test_pmix_logs("[GLOBAL_SYSLOG]", channels, 1, false, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_SYSLOG };
	err += test_pmix_logs("[SYSLOG]", channels, 1, false, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_EMAIL };
	err += test_pmix_logs("[EMAIL]", channels, 1, false, false, 0,
			      PMIX_ERR_NOT_SUPPORTED);
    }
    // Multiple requests (#8)
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_STDERR };
	err += test_pmix_logs("[STDOUT, STDERR]", channels, 2, false, false, 0,
			      expectStderrFail ?
			      PMIX_ERR_PARTIAL_SUCCESS : PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_SYSLOG };
	err += test_pmix_logs("[STDOUT, SYSLOG]", channels, 2, false, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_SYSLOG };
	err += test_pmix_logs("[STDOUT, SYSLOG] at now", channels, 2, false,
			      true, 0, PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_SYSLOG };
	err += test_pmix_logs("[STDOUT, SYSLOG] at time", channels, 2, false,
			      false, 1743681275, PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDERR, PMIX_LOG_SYSLOG };
	err += test_pmix_logs("[STDERR, SYSLOG]", channels, 2, false, false, 0,
			      expectStderrFail ?
			      PMIX_ERR_PARTIAL_SUCCESS : PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_EMAIL };
	err += test_pmix_logs("[STDOUT, EMAIL]", channels, 2, false, false, 0,
			      PMIX_ERR_PARTIAL_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_EMAIL, PMIX_LOG_GLOBAL_DATASTORE };
	err += test_pmix_logs("[EMAIL, GLOBAL_DATASTORE]", channels, 2,
			      false, false, 0, PMIX_ERR_NOT_SUPPORTED);
    }
    // Multiple request with log once (#15)
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_STDERR };
	err += test_pmix_logs("[STDOUT, STDERR]", channels, 2, true, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_EMAIL };
	err += test_pmix_logs("[STDOUT, EMAIL]", channels, 2, true, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_EMAIL, PMIX_LOG_STDOUT };
	err += test_pmix_logs("[EMAIL, STDOUT]", channels, 2, true, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_EMAIL, PMIX_LOG_GLOBAL_DATASTORE };
	err += test_pmix_logs("[EMAIL, GLOBAL_DATASTORE]", channels, 2,
			      true, false, 0, PMIX_ERR_NOT_SUPPORTED);
    }
    {
	char *channels[] = { PMIX_LOG_STDOUT, PMIX_LOG_SYSLOG };
	err += test_pmix_logs("[STDOUT, SYSLOG]", channels, 2, true, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_STDERR, PMIX_LOG_SYSLOG };
	err += test_pmix_logs("[STDERR, SYSLOG]", channels, 2, true, false, 0,
			      PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_LOCAL_SYSLOG, PMIX_LOG_GLOBAL_SYSLOG };
	err += test_pmix_logs("[LOCAL_SYSLOG, GLOBAL_SYSLOG]", channels, 2,
			      true, false, 0, PMIX_SUCCESS);
    }
    {
	char *channels[] = { PMIX_LOG_LOCAL_SYSLOG };
	err += test_pmix_logs("[LOCAL_SYSLOG] log_once=true", channels, 1, true,
			      false, 0, PMIX_SUCCESS);
    }

    if (PMIX_SUCCESS != (rc = PMIx_Finalize(NULL, 0))) {
	printf("[%s:%u]: PMIx_Finalize failed: %s\n", proc.nspace, proc.rank,
	       PMIx_Error_string(rc));
	return 1;
    }
    return (err);
}
