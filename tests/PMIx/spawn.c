#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>

#include <pmix.h>

#define CUSTOM_KEY "spawn-test"
#define CUSTOM_VALUE "foo"

static pid_t mypid;
static pmix_proc_t myproc;
static pmix_proc_t parent_proc;
static int spawned;
static int timeout = 5;
static bool refresh = true;

#if defined __GNUC__ && __GNUC__ < 8
#define printerr(format, ...) \
    fprintf(stderr, "%d: Client %s:%d: ERROR: " format, mypid, myproc.nspace, \
	    myproc.rank, ##__VA_ARGS__)

#define print(format, ...) \
    fprintf(stderr, "%d: Client %s:%d: " format, mypid, myproc.nspace, \
	    myproc.rank, ##__VA_ARGS__)
#else
#define printerr(format, ...) \
    fprintf(stderr, "%d: Client %s:%d: ERROR: " format, mypid, myproc.nspace, \
	    myproc.rank __VA_OPT__(,) __VA_ARGS__)

#define print(format, ...) \
    fprintf(stderr, "%d: Client %s:%d: " format, mypid, myproc.nspace, \
	    myproc.rank __VA_OPT__(,) __VA_ARGS__)
#endif

int main(int argc, char **argv)
{
    mypid = getpid();

    char hostname[HOST_NAME_MAX+1];
    if (gethostname(hostname, sizeof(hostname))) exit(1);

    char dir[1024];
    if (!getcwd(dir, sizeof(dir))) exit(1);

    fprintf(stderr, "%d running\n", mypid);

    /* init */
    int rc = PMIx_Init(&myproc, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	printerr("PMIx_Init failed: %s\n", PMIx_Error_string(rc));
	exit(0);
    }
    print("PMIx_Init succeeded\n");

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);

    /* get job size */
    pmix_value_t *val = NULL;
    rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
    if (rc != PMIX_SUCCESS) {
	printerr("PMIx_Get job size failed: %s\n", PMIx_Error_string(rc));
    } else {
	print("Job size %d\n", (uint32_t) val->data.uint32);
	PMIX_VALUE_RELEASE(val);
    }

    /* get parent id (for non-spawned process: not found) */
    val = NULL;
    rc = PMIx_Get(&myproc, PMIX_PARENT_ID, NULL, 0, &val);
    if (rc == PMIX_ERR_NOT_FOUND) {
	print("NOT spawned\n");
	spawned = 0;
    } else if (rc == PMIX_SUCCESS && val != NULL) {
	print("spawned\n");
	spawned = 1;
	PMIX_PROC_LOAD(&parent_proc, val->data.proc->nspace,
		       val->data.proc->rank);
	PMIX_VALUE_RELEASE(val);
    } else {
	printerr("PMIx_Get parent id failed: %s\n", PMIx_Error_string(rc));
    }

    /* sync */
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);
    rc = PMIx_Fence(&proc, 1, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	printerr("Initial PMIx_Fence failed: %s\n", PMIx_Error_string(rc));
    }
    print("Initial PMIx_Fence succeeded\n");

    /* non-spawned rank 0 calls spawn (parent) */
    if (spawned == 0 && myproc.rank == 0) {
	/* parent puts a custom key-value pair in KVS */
	PMIX_VALUE_CREATE(val, 1);
	PMIX_VALUE_LOAD(val, CUSTOM_VALUE, PMIX_STRING);
	rc = PMIx_Put(PMIX_GLOBAL, CUSTOM_KEY, val);
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx_Put failed: %s\n", PMIx_Error_string(rc));
	}
	PMIX_VALUE_FREE(val, 1);
	val = NULL;

	rc = PMIx_Commit();
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx_Commit failed: %s\n", PMIx_Error_string(rc));
	}

	/* Fill app data structure for spawning */
	pmix_app_t *app;
	PMIX_APP_CREATE(app, 1);
	if (asprintf(&app->cmd, "%s/%s", dir, argv[0]) < 0) {
	    return 1;
	}
	app->maxprocs = 2;
	app->argv = (char **) malloc(2 * sizeof(char *));
	if (asprintf(&app->argv[0], "%s", argv[0]) < 0) {
	    return 1;
	}
	app->argv[1] = NULL;
	app->env = (char **) malloc(2 * sizeof(char *));
	app->env[0] = strdup("PMIX_ENV_VALUE=3");
	app->env[1] = NULL;

	/* Fill app info data structure */
	PMIX_INFO_CREATE(app->info, 1);
	PMIX_INFO_LOAD(&(app->info[0]), PMIX_WDIR, dir, PMIX_STRING);
	app->ninfo = 1;

	/* Fill job info data structure */
	pmix_info_t *job_info;
	PMIX_INFO_CREATE(job_info, 1);
	PMIX_INFO_LOAD(&(job_info[0]), PMIX_PREFIX, dir, PMIX_STRING);

	print("Calling PMIx_Spawn\n");
	char nspace[PMIX_MAX_NSLEN + 1];
	rc = PMIx_Spawn(job_info, 1, app, 1, nspace);
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx_Spawn failed: %s\n", PMIx_Error_string(rc));
	}
	PMIX_APP_FREE(app, 1);
	PMIX_INFO_FREE(job_info, 1);

	/* get their universe size */
	val = NULL;
	PMIX_LOAD_PROCID(&proc, nspace, PMIX_RANK_WILDCARD);
	rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
	if (rc != PMIX_SUCCESS || !val) {
	    printerr("PMIx_Get job size for spawned nspace failed: %s\n",
		     PMIx_Error_string(rc));
	} else {
	    print("Spawned nspace %s size %u\n", nspace,
		  (uint32_t) val->data.uint32);
	    PMIX_VALUE_RELEASE(val);
	}

	/* get a proc-specific value */
	val = NULL;
	proc.rank = 1;
	rc = PMIx_Get(&proc, PMIX_LOCAL_RANK, NULL, 0, &val);
	if (rc != PMIX_SUCCESS || !val) {
	    printerr("PMIx_Get local rank of spawned nspace failed: %s\n",
		     PMIx_Error_string(rc));
	} else {
	    print("Spawned process %s:%d: local rank %hu\n", proc.nspace,
		  proc.rank, (uint16_t) val->data.uint16);
	    PMIX_VALUE_RELEASE(val);
	}

	/* sync parent with spawned processes */
	print("Sync with spawned processes (timeout=%d)...\n", timeout);
	pmix_proc_t *proc_sync = NULL;
	PMIX_PROC_CREATE(proc_sync, 2);
	PMIX_PROC_LOAD(&proc_sync[0], myproc.nspace, myproc.rank);
	PMIX_PROC_LOAD(&proc_sync[1], nspace, PMIX_RANK_WILDCARD);

	pmix_info_t *fence_info = NULL;
	PMIX_INFO_CREATE(fence_info, 2);
	PMIX_INFO_LOAD(&fence_info[0], PMIX_COLLECT_DATA, &refresh, PMIX_BOOL);
	PMIX_INFO_LOAD(&fence_info[1], PMIX_TIMEOUT, &timeout, PMIX_INT);
	rc = PMIx_Fence(proc_sync, 2, fence_info, 2);
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx Fence of parent with spawned processes failed: %s\n",
		     PMIx_Error_string(rc));
	}
	PMIX_PROC_FREE(proc_sync, 2);
	PMIX_INFO_FREE(fence_info, 2);
    } else if (spawned == 1) {
	/* sync spawned processes with parent process */
	print("Sync with parent process (timeout=%d)...\n", timeout);
	pmix_proc_t *proc_sync = NULL;
	PMIX_PROC_CREATE(proc_sync, 2);
	PMIX_PROC_LOAD(&proc_sync[0], parent_proc.nspace, parent_proc.rank);
	PMIX_PROC_LOAD(&proc_sync[1], myproc.nspace, PMIX_RANK_WILDCARD);

	pmix_info_t *fence_info = NULL;
	PMIX_INFO_CREATE(fence_info, 2);
	PMIX_INFO_LOAD(&fence_info[0], PMIX_COLLECT_DATA, &refresh, PMIX_BOOL);
	PMIX_INFO_LOAD(&fence_info[1], PMIX_TIMEOUT, &timeout, PMIX_INT);
	rc = PMIx_Fence(proc_sync, 2, fence_info, 2);
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx Fence of spawned process with parent %s:%d failed:"
		     " %s\n", parent_proc.nspace, parent_proc.rank,
		     PMIx_Error_string(rc));
	}
	PMIX_PROC_FREE(proc_sync, 2);
	PMIX_INFO_FREE(fence_info, 2);

	/* Get value from KVS, put there by parent process before spawn*/
	rc = PMIx_Get(&parent_proc, CUSTOM_KEY, NULL, 0, &val);
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx_Get for value of parent failed: %s\n",
		     PMIx_Error_string(rc));
	} else {
	    if (strcmp(CUSTOM_VALUE, val->data.string) != 0) {
		printerr("PMIx_Get: expected value %s, got %s", CUSTOM_VALUE,
			 val->data.string);
	    } else {
		print("PMIx_Get OK: %s == %s\n", CUSTOM_KEY, val->data.string);
	    }
	    PMIX_VALUE_RELEASE(val);
	}

	/* Check environment variable specified on spawning */
	char *env = getenv("PMIX_ENV_VALUE");
	if (!env) {
	    printerr("Environment variable %s not found\n", "PMIX_ENV_VALUE");
	} else {
	    if (strcmp("3", env) != 0) {
		printerr("env: expected %s, got %s\n", "3", env);
	    } else {
		print("env OK: %s = %s\n", "PMIX_ENV_VALUE", env);
	    }
	}
    }

    /* Get local rank to make sure this is in place after spawning in all
     * processes */
    rc = PMIx_Get(&myproc, PMIX_LOCAL_RANK, NULL, 0, &val);
    if (rc != PMIX_SUCCESS || !val) {
	printerr("PMIx_Get local rank in spawned process failed: %s\n",
		 PMIx_Error_string(rc));
    } else {
	print("local rank %hu\n", (uint16_t) val->data.uint16);
	PMIX_VALUE_RELEASE(val);
    }

    /* sync */
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);
    rc = PMIx_Fence(&proc, 1, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	printerr("Final PMIx_Fence failed: %s\n", PMIx_Error_string(rc));
    } else {
	print("Final PMIx_Fence succeeded\n");
    }

    /* finalize */
    rc = PMIx_Finalize(NULL, 0);
    if (rc != PMIX_SUCCESS) {
	printerr("PMIx_Finalize failed: %s\n", PMIx_Error_string(rc));
	return 1;
    }
    print("PMIx_Finalize succeeded\n");

    fflush(stderr);

    return 0;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
