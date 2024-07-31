#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>

#include <pmix.h>

static pid_t mypid;
static pmix_proc_t myproc;
static pmix_proc_t parent_proc;
static int spawned;
static int timeout = 5;
static const bool TRUE = true;

static FILE *outfile;

#define printerr(format, ...) \
    fprintf(outfile, "%d: Client %s:%d: ERROR: " format, mypid, myproc.nspace, \
	    myproc.rank __VA_OPT__(,) __VA_ARGS__)

#define print(format, ...) \
    fprintf(outfile, "%d: Client %s:%d: " format, mypid, myproc.nspace, \
	    myproc.rank __VA_OPT__(,) __VA_ARGS__)

int main(int argc, char **argv)
{
    outfile = stdout;
    mypid = getpid();

    char hostname[HOST_NAME_MAX+1];
    if (gethostname(hostname, sizeof(hostname))) exit(1);

    char dir[1024];
    if (!getcwd(dir, sizeof(dir))) exit(1);

    fprintf(outfile, "%d: running in %s on %s\n", mypid, dir, hostname);

    /* init */
    int rc = PMIx_Init(&myproc, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	printerr("PMIx_Init failed: %s\n", PMIx_Error_string(rc));
	exit(0);
    }
    char ofName[512];
    sprintf(ofName, "out-%s-%d", myproc.nspace, myproc.rank);
    //outfile = fopen(ofName, "w");
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
    if (!spawned && myproc.rank == 0) {
	/* Fill app data structure for spawning */
	pmix_app_t *app;
	PMIX_APP_CREATE(app, 2);
	app[0].maxprocs = 2;
	if (asprintf(&app[0].cmd, "%s/%s", dir, argv[0]) < 0) return 1;
	app[0].argv = (char **) malloc(2 * sizeof(char *));
	if (asprintf(&app[0].argv[0], "%s", argv[0]) < 0) return 1;
	app[0].argv[1] = NULL;
	app[0].env = NULL;
	app[0].ninfo = 0;

	app[1].maxprocs = 1;
	if (asprintf(&app[1].cmd, "%s", argv[0]) < 0) return 1;
	app[1].argv = NULL;
	app[1].env = NULL;
	app[1].ninfo = 0;

	pmix_info_t *spawn_info = NULL;
	PMIX_INFO_CREATE(spawn_info, 2);
	PMIX_INFO_LOAD(&spawn_info[0], PMIX_MERGE_STDERR_STDOUT, &TRUE, PMIX_BOOL);
	// all files will end up in $CWD
	snprintf(dir + strlen(dir), sizeof(dir) - strlen(dir), "/out");
	PMIX_INFO_LOAD(&spawn_info[1], PMIX_OUTPUT_TO_FILE, dir, PMIX_STRING);

	print("Calling PMIx_Spawn\n");
	char nspace[PMIX_MAX_NSLEN + 1];
	rc = PMIx_Spawn(spawn_info, 2, app, 2, nspace);
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx_Spawn failed: %s\n", PMIx_Error_string(rc));
	}
	PMIX_APP_FREE(app, 2);
	PMIX_INFO_FREE(spawn_info, 2);

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
	PMIX_INFO_LOAD(&fence_info[0], PMIX_COLLECT_DATA, &TRUE, PMIX_BOOL);
	PMIX_INFO_LOAD(&fence_info[1], PMIX_TIMEOUT, &timeout, PMIX_INT);
	rc = PMIx_Fence(proc_sync, 2, fence_info, 2);
	if (rc != PMIX_SUCCESS) {
	    printerr("PMIx Fence of parent with spawned processes failed: %s\n",
		     PMIx_Error_string(rc));
	}
	PMIX_PROC_FREE(proc_sync, 2);
	PMIX_INFO_FREE(fence_info, 2);
    } else if (spawned) {
	/* sync spawned processes with parent process */
	print("Sync with parent process (timeout=%d)...\n", timeout);
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
	    printerr("PMIx Fence of spawned process with parent %s:%d failed:"
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
	    printerr("PMIx_Get job size for spawner %s/%d failed: %s\n",
		     proc.nspace, proc.rank, PMIx_Error_string(rc));
	} else {
	    print("Spawner's nspace %s size %u\n", parent_proc.nspace,
		  (uint32_t) val->data.uint32);
	    PMIX_VALUE_RELEASE(val);
	}
    }

    /* Get local rank to make sure this is in place after spawning in all
     * processes */
    val = NULL;
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
    if (!spawned) sleep(1);

    fflush(outfile);

    return 0;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
