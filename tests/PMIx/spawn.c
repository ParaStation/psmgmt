#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>

#include <pmix.h>

static pid_t mypid;
static pmix_proc_t myproc;

#define print(format, ...) \
    fprintf(stderr, "%d: Client %s:%d: " format, mypid, myproc.nspace, \
	    myproc.rank __VA_OPT__(,) __VA_ARGS__)

int main(int argc, char **argv)
{
    mypid = getpid();

    char hostname[HOST_NAME_MAX+1];
    if (gethostname(hostname, sizeof(hostname))) {
        exit(1);
    }

    char dir[1024];
    if (!getcwd(dir, sizeof(dir))) {
        exit(1);
    }

    fprintf(stderr, "%d running\n", mypid);

    /* init */
    int rc = PMIx_Init(&myproc, NULL, 0);
    if (rc != PMIX_SUCCESS) {
        print("PMIx_Init failed: %s\n", PMIx_Error_string(rc));
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
        print("PMIx_Get job size failed: %s\n", PMIx_Error_string(rc));
        goto done;
    }
    print("Job size %d\n", (uint32_t) val->data.uint32);
    PMIX_VALUE_RELEASE(val);

    /* sync */
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);
    rc = PMIx_Fence(&proc, 1, NULL, 0);
    if (rc != PMIX_SUCCESS) {
        print("Initial PMIx_Fence failed: %d\n", rc);
        goto done;
    }
    print("Initial PMIx_Fence succeeded\n");

    if (argc > 1) {
	/* spawnee */
        val = NULL;
        proc.rank = 1;
        rc = PMIx_Get(&proc, PMIX_PARENT_ID, NULL, 0, &val);
	if (rc != PMIX_SUCCESS || !val) {
            print("PMIx_Get parent id failed: %s\n", PMIx_Error_string(rc));
            goto done;
        }
        print("Spawnee of parent nspace %s rank %u\n", val->data.proc->nspace,
               val->data.proc->rank);
        PMIX_VALUE_RELEASE(val);
	goto done;
    }

    /* rank 0 calls spawn */
    if (myproc.rank == 0) {
	pmix_app_t *app;
        PMIX_APP_CREATE(app, 1);
        if (asprintf(&app->cmd, "%s/%s", dir, argv[0]) < 0) {
            return 1;
        }
        app->maxprocs = 2;
        app->argv = (char **) malloc(3 * sizeof(char *));
        if (asprintf(&app->argv[0], "%s/%s", dir, argv[0]) < 0) {
            return 1;
        }
        if (asprintf(&app->argv[0], "%s/%s", dir, argv[0]) < 0) {
            return 1;
        }
        app->argv[1] = strdup("2");
        app->argv[2] = NULL;
        app->env = (char **) malloc(2 * sizeof(char *));
        app->env[0] = strdup("PMIX_ENV_VALUE=3");
        app->env[1] = NULL;

        print("Calling PMIx_Spawn\n");
	char nspace[PMIX_MAX_NSLEN + 1];
        rc = PMIx_Spawn(NULL, 0, app, 1, nspace);
	if (rc != PMIX_SUCCESS) {
            print("PMIx_Spawn failed: %s\n", PMIx_Error_string(rc));
            goto done;
        }
        PMIX_APP_FREE(app, 1);

        /* get their universe size */
        val = NULL;
	PMIX_LOAD_PROCID(&proc, nspace, PMIX_RANK_WILDCARD);
	rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
	if (rc != PMIX_SUCCESS || !val) {
            print("PMIx_Get job size failed: %s\n", PMIx_Error_string(rc));
            goto done;
        }
        print("Spawned nspace %s size %u\n", nspace,
              (uint32_t) val->data.uint32);
        PMIX_VALUE_RELEASE(val);

        /* get a proc-specific value */
        val = NULL;
        proc.rank = 1;
        rc = PMIx_Get(&proc, PMIX_LOCAL_RANK, NULL, 0, &val);
	if (rc != PMIX_SUCCESS || !val) {
            print("PMIx_Get local rank failed: %s\n", PMIx_Error_string(rc));
            goto done;
        }
        print("Spawned nspace %s local rank %hu\n", nspace,
	      (uint16_t) val->data.uint16);
        PMIX_VALUE_RELEASE(val);
    }

done:
    /* sync */
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);
    rc = PMIx_Fence(&proc, 1, NULL, 0);
    if (rc != PMIX_SUCCESS) {
        print("Final PMIx_Fence failed: %s\n", PMIx_Error_string(rc));
        goto done;
    }
    print("Final PMIx_Fence succeeded\n");

    /* finalize */
    rc = PMIx_Finalize(NULL, 0);
    if (rc != PMIX_SUCCESS) {
        print("PMIx_Finalize failed: %s\n", PMIx_Error_string(rc));
	return 1;
    }
    print("PMIx_Finalize succeeded\n");
    
    fflush(stderr);

    return 0;
}
