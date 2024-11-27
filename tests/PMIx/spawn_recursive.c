#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>

#include <pmix.h>

static int depth = 0;
static pmix_proc_t myproc;
static uint32_t wSize;

static pmix_proc_t parent_proc;
static pid_t mypid;

static bool spawned;

static int timeout = 5;
static const bool TRUE = true;

static FILE *outfile;

#define logerr(format, ...) \
    fprintf(outfile, "%d/%d(%d): ERROR: " format,			\
	    depth, myproc.rank, wSize __VA_OPT__(,) __VA_ARGS__)

#define log(format, ...) \
    fprintf(outfile, "%d/%d(%d): " format,			\
	    depth, myproc.rank, wSize __VA_OPT__(,) __VA_ARGS__)

#define abort(status) {					\
	logerr("abort in %s@%d\n", __func__, __LINE__);	\
	PMIx_Abort(status, __func__, NULL, 0); }

int main(int argc, char **argv)
{
    outfile = stdout;
    if (argc > 1) depth = strtol(argv[1], NULL, 0);
    mypid = getpid();

    char hostname[HOST_NAME_MAX+1];
    if (gethostname(hostname, sizeof(hostname)) == -1) {
	logerr("Unable to get hostname: %m\n");
	exit(-1);
    }

    char wDir[1024];
    if (!getcwd(wDir, sizeof(wDir))) {
	logerr("Unable to get current working directory: %m\n");
	exit(-1);
    }

    /* init */
    int rc = PMIx_Init(&myproc, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	logerr("PMIx_Init failed: %s\n", PMIx_Error_string(rc));
	exit(-1);
    }
    log("%d running in %s on %s in ns %s\n", mypid, wDir, hostname, myproc.nspace);

    /* maybe log to some outfile */
    char ofName[512];
    sprintf(ofName, "%s-%d.out", myproc.nspace, myproc.rank);
    //outfile = fopen(ofName, "w");

    log("PMIx_Init succeeded\n");

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);

    /* get job size */
    pmix_value_t *val = NULL;
    rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
    if (rc != PMIX_SUCCESS) {
	logerr("PMIx_Get job size failed: %s\n", PMIx_Error_string(rc));
    } else {
	wSize = val->data.uint32;
	log("Job size %d\n", wSize);
	PMIX_VALUE_RELEASE(val);
    }

    /* get parent id (for non-spawned process: not found) */
    val = NULL;
    rc = PMIx_Get(&myproc, PMIX_PARENT_ID, NULL, 0, &val);
    if (rc == PMIX_ERR_NOT_FOUND) {
	log("NOT spawned\n");
	spawned = false;
    } else if (rc == PMIX_SUCCESS && val != NULL) {
	log("spawned\n");
	spawned = true;
	PMIX_PROC_LOAD(&parent_proc, val->data.proc->nspace,
		       val->data.proc->rank);
	PMIX_VALUE_RELEASE(val);
    } else {
	logerr("PMIx_Get parent id failed: %s\n", PMIx_Error_string(rc));
    }

    /* sync */
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);
    rc = PMIx_Fence(&proc, 1, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	logerr("Initial PMIx_Fence failed: %s\n", PMIx_Error_string(rc));
    }
    log("Initial PMIx_Fence succeeded\n");

    /* first sync spawned processes with parent process and fetch parent info */
    if (spawned) {
	log("Sync with parent process (timeout=%d)...\n", timeout);
	pmix_proc_t *proc_sync = NULL;
	PMIX_PROC_CREATE(proc_sync, 2);
	PMIX_PROC_LOAD(&proc_sync[0], parent_proc.nspace, parent_proc.rank);
	PMIX_PROC_LOAD(&proc_sync[1], myproc.nspace, PMIX_RANK_WILDCARD);

	pmix_info_t *fence_info = NULL;
	PMIX_INFO_CREATE(fence_info, 2);
	PMIX_INFO_LOAD(&fence_info[0], PMIX_COLLECT_DATA, &TRUE, PMIX_BOOL);
	PMIX_INFO_LOAD(&fence_info[1], PMIX_TIMEOUT, &timeout, PMIX_INT);
	rc = PMIx_Fence(proc_sync, 2, fence_info, 2);
	if (rc != PMIX_SUCCESS) {
	    logerr("PMIx Fence of spawned process with parent %s:%d failed:"
		     " %s\n", parent_proc.nspace, parent_proc.rank,
		     PMIx_Error_string(rc));
	}
	PMIX_PROC_FREE(proc_sync, 2);
	PMIX_INFO_FREE(fence_info, 2);

	/* get parent's job size */
	val = NULL;
	PMIX_LOAD_PROCID(&proc, parent_proc.nspace, PMIX_RANK_WILDCARD);
	rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
	if (rc != PMIX_SUCCESS || !val) {
	    logerr("PMIx_Get job size for spawner %s/%d failed: %s\n",
		     proc.nspace, proc.rank, PMIx_Error_string(rc));
	} else {
	    log("Spawner's nspace %s size %u\n",
		parent_proc.nspace, val->data.uint32);
	    PMIX_VALUE_RELEASE(val);
	}
    }

    /* rank 0 calls spawn until final leaf is reached */
    if (depth > 0 && myproc.rank == 0) {
	/* Fill app data structure for spawning */
	pmix_app_t *app;
	PMIX_APP_CREATE(app, 1);
	app->maxprocs = wSize;
	if (asprintf(&app->cmd, "%s", argv[0]) < 0) abort(-1);
	app->argv = (char **) malloc(3 * sizeof(*app->argv));
	if (!app->argv) abort(-1);
	if (asprintf(&app->argv[0], "%s", argv[0]) < 0) abort(-1);
	if (asprintf(&app->argv[1], "%d", depth - 1) < 0) abort(-1);
	app->argv[2] = NULL;
	app->env = NULL;
	app->ninfo = 0;

	log("Calling PMIx_Spawn\n");
	char nspace[PMIX_MAX_NSLEN + 1];
	rc = PMIx_Spawn(NULL, 0, app, 1, nspace);
	if (rc != PMIX_SUCCESS) {
	    logerr("PMIx_Spawn failed: %s\n", PMIx_Error_string(rc));
	    abort(-1);
	}
	PMIX_APP_FREE(app, 1);

	/* get their universe size */
	val = NULL;
	PMIX_LOAD_PROCID(&proc, nspace, PMIX_RANK_WILDCARD);
	rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
	if (rc != PMIX_SUCCESS || !val) {
	    logerr("Cannot PMIx_Get size of spawned nspace %s: %s\n", nspace,
		   PMIx_Error_string(rc));
	} else {
	    log("Spawned nspace %s size %u\n", nspace, val->data.uint32);
	    PMIX_VALUE_RELEASE(val);
	}

	/* sync parent with spawned processes */
	log("Sync with spawned processes (timeout=%d)...\n", timeout);
	pmix_proc_t *proc_sync = NULL;
	PMIX_PROC_CREATE(proc_sync, 2);
	PMIX_PROC_LOAD(&proc_sync[0], myproc.nspace, myproc.rank);
	PMIX_PROC_LOAD(&proc_sync[1], nspace, PMIX_RANK_WILDCARD);

	pmix_info_t *fence_info = NULL;
	PMIX_INFO_CREATE(fence_info, 2);
	PMIX_INFO_LOAD(&fence_info[0], PMIX_COLLECT_DATA, &TRUE, PMIX_BOOL);
	PMIX_INFO_LOAD(&fence_info[1], PMIX_TIMEOUT, &timeout, PMIX_INT);
	rc = PMIx_Fence(proc_sync, 2, fence_info, 2);
	if (rc != PMIX_SUCCESS) {
	    logerr("PMIx Fence of parent with spawned processes failed: %s\n",
		   PMIx_Error_string(rc));
	}
	PMIX_PROC_FREE(proc_sync, 2);
	PMIX_INFO_FREE(fence_info, 2);
    }

    /* sync */
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);
    rc = PMIx_Fence(&proc, 1, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	logerr("Final PMIx_Fence failed: %s\n", PMIx_Error_string(rc));
    } else {
	log("Final PMIx_Fence succeeded\n");
    }

    /* finalize */
    rc = PMIx_Finalize(NULL, 0);
    if (rc != PMIX_SUCCESS) {
	logerr("PMIx_Finalize failed: %s\n", PMIx_Error_string(rc));
	abort(-1);
    }
    log("PMIx_Finalize succeeded\n");
    if (myproc.rank == 0) sleep(depth);

    fflush(outfile);

    return 0;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
