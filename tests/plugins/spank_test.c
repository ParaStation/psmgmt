#include <stdlib.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the
 * Slurm plugin loader.
 */
SPANK_PLUGIN(psslurm-test, 1);
const char psid_plugin[] = "yes";

typedef enum {
    SPANK_INIT = 0,
    SPANK_SLURMD_INIT,
    SPANK_JOB_PROLOG,
    SPANK_INIT_POST_OPT,
    SPANK_LOCAL_USER_INIT,
    SPANK_USER_INIT,
    SPANK_TASK_INIT_PRIVILEGED,
    SPANK_TASK_INIT,
    SPANK_TASK_POST_FORK,
    SPANK_TASK_EXIT,
    SPANK_JOB_EPILOG,
    SPANK_SLURMD_EXIT,
    SPANK_EXIT,
    SPANK_END
} Spank_Hook_Calls_t;

static const struct {
    int hook;
    char *strName;
} Spank_Hook_Table[] = {
    { SPANK_INIT,                   "slurm_spank_init"		       },
    { SPANK_SLURMD_INIT,            "slurm_spank_slurmd_init"	       },
    { SPANK_JOB_PROLOG,             "slurm_spank_job_prolog"	       },
    { SPANK_INIT_POST_OPT,          "slurm_spank_init_post_opt"	       },
    { SPANK_LOCAL_USER_INIT,        "slurm_spank_local_user_init"      },
    { SPANK_USER_INIT,              "slurm_spank_user_init"	       },
    { SPANK_TASK_INIT_PRIVILEGED,   "slurm_spank_task_init_privileged" },
    { SPANK_TASK_INIT,              "slurm_spank_task_init"	       },
    { SPANK_TASK_POST_FORK,         "slurm_spank_task_post_fork"       },
    { SPANK_TASK_EXIT,              "slurm_spank_task_exit"	       },
    { SPANK_JOB_EPILOG,             "slurm_spank_job_epilog"	       },
    { SPANK_SLURMD_EXIT,            "slurm_spank_slurmd_exit"	       },
    { SPANK_EXIT,                   "slurm_spank_exit"		       },
    { SPANK_END,                    NULL			       }
};

static int hookCount = 0;

static int optArg = -1;

static int optArgAdd = -1;

static int opt_process(int val, const char *optarg, int remote);
static int opt_process_add(int val, const char *optarg, int remote);

static void getAllEnv(spank_t sp, const char *func)
{
    char buf[1024];
    spank_err_t ret;
    int i;

    for (i=0; Spank_Hook_Table[i].strName; i++) {
	ret = spank_getenv(sp, Spank_Hook_Table[i].strName, buf, sizeof(buf));
	if (ret == ESPANK_SUCCESS) {
	    slurm_info("%s: env%i: %s=%s", func, i,
		       Spank_Hook_Table[i].strName, buf);
	}
    }
}

/*
Spank options structure:

    name
	is the name of the option. Its length is limited to
	SPANK_OPTION_MAXLEN defined in <slurm/spank.h>
    arginfo
	is a description of the argument to the option,
	if the option does take an argument.
    usage
	is a short description of the option suitable for --help output.
    has_arg
	0 if option takes no argument, 1 if option takes an argument,
	and 2 if the option takes an optional argument. (See getopt_long (3)).
    val
	A plugin-local value to return to the option callback function.
    cb
	A callback function that is invoked when the plugin option is
	registered with Slurm. spank_opt_cb_f is typedef'd in <slurm/spank.h>
*/
struct spank_option spank_options[] =
{
    { "testopt",
      "[yes|no]",
      "Forward a test option to spank.",
      2,
      1,
      opt_process },
    { "testopt2",
      "[yes|no]",
      "Forward a second test option to spank.",
      2,
      2,
      opt_process },
    SPANK_OPTIONS_TABLE_END
};

struct spank_option additional_options[] =
{
    { "testaddopt",
      "[yes|no]",
      "Forward additional options to spank.",
      2,
      3,
      NULL },
    { "testaddopt2",
      "[yes|no]",
      "Forward second additional options to spank.",
      2,
      4,
      opt_process_add },
    { "testaddopt3",
      "[yes|no]",
      "Forward second additional options to spank.",
      2,
      5,
      opt_process_add },
    SPANK_OPTIONS_TABLE_END
};

static int testHook(spank_t sp, int ac, char **av, const char *func)
{
    spank_err_t ret;
    int i;
    hookCount++;

    slurm_info("%s: hook-count %u static-opt: %i local uid %i gid %i pid %i "
	       "isremote %i", func, hookCount, optArg, getuid(), getgid(),
	       getpid(), spank_remote(sp));

    /* set environment */
    ret = spank_setenv(sp, func, "psslurm-test", 1); if (ret != ESPANK_SUCCESS) {
	slurm_info("%s: spank_setenv failed: %i, %s", func, ret,
		   spank_strerror(ret));
    }

    getAllEnv(sp, func);

    /* print spank arguments */
    for (i=0; i<ac; i++) {
	slurm_info("%s: av[%i]: %s", func, i, av[i]);
    }

    /* User id (uid_t *)                            */
    uid_t uid;
    ret = spank_get_item(sp, S_JOB_UID, &uid);
    slurm_info("%s: S_JOB_UID: %i ret: %i", func, uid, ret);

    /* Primary group id (gid_t *)                   */
    gid_t gid;
    ret = spank_get_item(sp, S_JOB_GID, &gid);
    slurm_info("%s: S_JOB_GID: %i ret: %i", func, gid, ret);

    /* Slurm job id (uint32_t *)                    */
    uint32_t jobid;
    ret = spank_get_item(sp, S_JOB_ID, &jobid);
    slurm_info("%s: S_JOB_ID: %u ret: %i", func, jobid, ret);

    /* Slurm job step id (uint32_t *)               */
    uint32_t stepid;
    ret = spank_get_item(sp, S_JOB_STEPID, &stepid);
    slurm_info("%s: S_JOB_STEPID: %u ret: %i", func, stepid, ret);

    /* Total number of nodes in job (uint32_t *)    */
    uint32_t nnodes;
    ret = spank_get_item(sp, S_JOB_NNODES, &nnodes);
    slurm_info("%s: S_JOB_NNODES: %u ret: %i", func, nnodes, ret);

    /* Relative id of this node (uint32_t *)        */
    uint32_t nodeid;
    ret = spank_get_item(sp, S_JOB_NODEID, &nodeid);
    slurm_info("%s: S_JOB_NODEID: %u ret: %i", func, nodeid, ret);

    /* Number of local tasks (uint32_t *)           */
    uint32_t taskCount;
    ret = spank_get_item(sp, S_JOB_LOCAL_TASK_COUNT, &taskCount);
    slurm_info("%s: S_JOB_LOCAL_TASK_COUNT: %u ret: %i", func, taskCount, ret);

    /* Total number of tasks in job (uint32_t *)    */
    uint32_t totalTaskCount;
    ret = spank_get_item(sp, S_JOB_TOTAL_TASK_COUNT, &totalTaskCount);
    slurm_info("%s: S_JOB_TOTAL_TASK_COUNT: %u ret: %i", func,
	       totalTaskCount, ret);

    /* Number of CPUs used by this job (uint16_t *) */
    uint16_t numCPUs;
    ret = spank_get_item(sp, S_JOB_NCPUS, &numCPUs);
    slurm_info("%s: S_JOB_NCPUS: %u ret: %i", func, numCPUs, ret);

    /* Command args (int *, char ***)               */
    int argc;
    char **argv;
    ret = spank_get_item(sp, S_JOB_ARGV, &argc, &argv);
    if (ret == ESPANK_SUCCESS) {
	for (i=0; i<argc;i++) {
	    slurm_info("%s: S_JOB_ARGV: arg%u=%s ret: %i", func, i,
		       argv[i], ret);
	}
    } else {
	slurm_info("%s: error: spank_get_item(S_JOB_ARGV) ret:%i, %s\n",
		   __func__, ret, spank_strerror(ret));
    }

    /* Job env array (char ***)                     */
    char **env;
    ret = spank_get_item(sp, S_JOB_ENV, &env);
    if (ret == ESPANK_SUCCESS) {
	if (env && env[0]) {
	    slurm_info("%s: S_JOB_ENV: env[0]:%s ret: %i", func, env[0], ret);
	}
    } else {
	slurm_info("%s: error: spank_get_item(S_JOB_ENV) ret:%i, %s\n",
		   __func__, ret, spank_strerror(ret));
    }

    /* Local task id (int *)                        */
    int taskID;
    ret = spank_get_item(sp, S_TASK_ID, &taskID);
    slurm_info("%s: S_TASK_ID: %u ret: %i", func, taskID, ret);

    /* Global task id (uint32_t *)                  */
    uint32_t taskGlobalID;
    ret = spank_get_item(sp, S_TASK_GLOBAL_ID, &taskGlobalID);
    slurm_info("%s: S_TASK_GLOBAL_ID: %u ret: %i", func, taskGlobalID, ret);

    /* Exit status of task if exited (int *)        */
    uint32_t taskExitStatus;
    ret = spank_get_item(sp, S_TASK_EXIT_STATUS, &taskExitStatus);
    slurm_info("%s: S_TASK_EXIT_STATUS: %u ret: %i", func, taskExitStatus, ret);

    /* Task pid (pid_t *)                           */
    pid_t taskPID;
    ret = spank_get_item(sp, S_TASK_PID, &taskPID);
    slurm_info("%s: S_TASK_PID: %u ret: %i", func, taskPID, ret);

    if (ret == ESPANK_SUCCESS) {
	uint32_t global, local, tmp;

	/* global task id from pid (pid_t, uint32_t *)  */
	ret = spank_get_item(sp, S_JOB_PID_TO_GLOBAL_ID, taskPID, &global);
	slurm_info("%s: S_JOB_PID_TO_GLOBAL_ID: %u ret: %i", func, global, ret);

	/* local task id from pid (pid_t, uint32_t *)   */
	ret = spank_get_item(sp, S_JOB_PID_TO_LOCAL_ID, taskPID, &local);
	slurm_info("%s: S_JOB_PID_TO_LOCAL_ID: %u ret: %i", func, local, ret);

	/* local id to global id (uint32_t, uint32_t *) */
	ret = spank_get_item(sp, S_JOB_LOCAL_TO_GLOBAL_ID, local, &tmp);
	slurm_info("%s: S_JOB_LOCAL_TO_GLOBAL_ID: %u ret: %i", func, tmp, ret);

	/* global id to local id (uint32_t, uint32_t *) */
	ret = spank_get_item(sp,S_JOB_GLOBAL_TO_LOCAL_ID, global, &tmp);
	slurm_info("%s: S_JOB_GLOBAL_TO_LOCAL_ID: %u ret: %i", func, tmp, ret);
    }

    /* Array of suppl. gids (gid_t **, int *)       */
    gid_t *gids;
    int gidCount;
    ret = spank_get_item(sp, S_JOB_SUPPLEMENTARY_GIDS, &gids, &gidCount);
    if (ret == ESPANK_SUCCESS) {
	for (i=0; i<gidCount; i++) {
	    slurm_info("%s: S_JOB_SUPPLEMENTARY_GIDS: %u ret: %i", func,
		       taskPID, ret);
	}
    }

    /* Current Slurm version (char **)              */
    char *slurmVer;
    ret = spank_get_item(sp, S_SLURM_VERSION, &slurmVer);
    slurm_info("%s: S_SLURM_VERSION: %s ret: %i", func, slurmVer, ret);

    /* Slurm version major release (char **)        */
    char *slurmVerMajor;
    ret = spank_get_item(sp, S_SLURM_VERSION_MAJOR, &slurmVerMajor);
    slurm_info("%s: S_SLURM_VERSION_MAJOR: %s ret: %i", func,
	       slurmVerMajor, ret);

    /* Slurm version minor release (char **)        */
    char *slurmVerMinor;
    ret = spank_get_item(sp, S_SLURM_VERSION_MINOR, &slurmVerMinor);
    slurm_info("%s: S_SLURM_VERSION_MINOR: %s ret: %i", func,
	       slurmVerMinor, ret);

    /* Slurm version micro release (char **)        */
    char *slurmVerMicro;
    ret = spank_get_item(sp, S_SLURM_VERSION_MICRO, &slurmVerMicro);
    slurm_info("%s: S_SLURM_VERSION_MICRO: %s ret: %i", func,
	       slurmVerMicro, ret);

    /* CPUs allocated per task (=1 if --overcommit
     * option is used, uint32_t *)                  */
    uint32_t cpusPerTask;
    ret = spank_get_item(sp, S_STEP_CPUS_PER_TASK, &cpusPerTask);
    slurm_info("%s: S_STEP_CPUS_PER_TASK: %u ret: %i", func, cpusPerTask, ret);

    /* Job allocated cores in list format (char **) */
    char *jobAllocCores;
    ret = spank_get_item(sp, S_JOB_ALLOC_CORES, &jobAllocCores);
    if (ret == ESPANK_SUCCESS) {
	slurm_info("%s: S_JOB_ALLOC_CORES: %s ret: %i", func,
	           jobAllocCores, ret);
    } else {
	slurm_info("%s: S_JOB_ALLOC_CORES: NULL ret: %i", func, ret);
    }

    /* Job allocated memory in MB (uint64_t *)      */
    uint64_t jobAllocMem;
    ret = spank_get_item(sp, S_JOB_ALLOC_MEM, &jobAllocMem);
    slurm_info("%s: S_JOB_ALLOC_MEM: %lu ret: %u", func, jobAllocMem, ret);

    /* Step alloc'd cores in list format  (char **) */
    char *stepAllocCores;
    ret = spank_get_item(sp, S_STEP_ALLOC_CORES, &stepAllocCores);
    if (ret == ESPANK_SUCCESS) {
	slurm_info("%s: S_STEP_ALLOC_CORES: %s ret: %i", func,
		   stepAllocCores, ret);
    } else {
	slurm_info("%s: S_STEP_ALLOC_CORES: NULL ret: %i", func, ret);
    }

    /* Step alloc'd memory in MB (uint64_t *)       */
    uint64_t stepAllocMem;
    ret = spank_get_item(sp, S_STEP_ALLOC_MEM, &stepAllocMem);
    slurm_info("%s: S_STEP_ALLOC_MEM: %lu ret: %i", func, stepAllocMem, ret);

    /* Job restart count (uint32_t *)               */
    uint32_t restart;
    ret = spank_get_item(sp, S_SLURM_RESTART_COUNT, &restart);
    slurm_info("%s: S_SLURM_RESTART_COUNT: %u ret: %i", func, restart, ret);

    /* Slurm job array id (uint32_t *) or 0         */
    uint32_t arrayID;
    ret = spank_get_item(sp, S_JOB_ARRAY_ID, &arrayID);
    slurm_info("%s: S_JOB_ARRAY_ID: %u ret: %i", func, arrayID, ret);

    /* Slurm job array task id (uint32_t *)         */
    uint32_t arrayTaskID;
    ret = spank_get_item(sp, S_JOB_ARRAY_TASK_ID, &arrayTaskID);
    slurm_info("%s: S_JOB_ARRAY_TASK_ID: %u ret: %i", func, arrayTaskID, ret);

    /* context */
    int ctx = spank_context();
    char *strCtx = NULL;

    switch (ctx) {
	case S_CTX_ERROR:
	    strCtx = "S_CTX_ERROR";
	    break;
	case S_CTX_LOCAL:
	    strCtx = "S_CTX_LOCAL";
	    break;
	case S_CTX_REMOTE:
	    strCtx = "S_CTX_REMOTE";
	    break;
	case S_CTX_ALLOCATOR:
	    strCtx = "S_CTX_ALLOCATOR";
	    break;
	case S_CTX_SLURMD:
	    strCtx = "S_CTX_SLURMD";
	    break;
	case S_CTX_JOB_SCRIPT:
	    strCtx = "S_CTX_JOB_SCRIPT";
	    break;
	default:
	    strCtx = "unknown";
    }
    slurm_info("%s: current context(%i) %s\n", func, ctx, strCtx);

    return ESPANK_SUCCESS;
}

static int opt_process(int val, const char *optarg, int remote)
{
    slurm_info("%s: val: %i optarg: %s remote: %i", __func__, val, optarg,
	       remote);

    if (!optarg) {
	optArg = -2;
    } else if (!strcmp(optarg, "yes")) {
	optArg = 1;
    } else if (!strcmp(optarg, "no")) {
	optArg = 0;
    }

    return ESPANK_SUCCESS;
}

static int opt_process_add(int val, const char *optarg, int remote)
{
    slurm_info("%s: val: %i optarg: %s remote: %i", __func__, val, optarg,
	       remote);

    if (!optarg) {
	optArgAdd = -2;
    } else if (!strcmp(optarg, "yes")) {
	optArgAdd = 1;
    } else if (!strcmp(optarg, "no")) {
	optArgAdd = 0;
    }

    return ESPANK_SUCCESS;
}

static void printOpt(spank_t sp, const char *func)
{
    /* spank options */
    char *optGet;
    spank_err_t ret = spank_option_getopt(sp, &spank_options[0], &optGet);
    slurm_info("%s: get option: testopt=%s ret: %s\n", func, optGet,
	       spank_strerror(ret));

    optGet = NULL;
    ret = spank_option_getopt(sp, &additional_options[0], &optGet);
    slurm_info("%s: get option: testaddopt=%s ret: %s\n", func, optGet,
	       spank_strerror(ret));

    optGet = NULL;
    ret = spank_option_getopt(sp, &additional_options[1], &optGet);
    slurm_info("%s: get option: testaddopt2=%s ret: %s\n", func, optGet,
	       spank_strerror(ret));

    optGet = NULL;
    ret = spank_option_getopt(sp, &additional_options[2], &optGet);
    slurm_info("%s: get option: testaddopt3=%s ret: %s\n", func, optGet,
	       spank_strerror(ret));
}

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    for (int i=0; additional_options[i].name != NULL; i++) {
	slurm_info("register option %i\n", i);
	spank_option_register(sp, &additional_options[i]);
    }

    return testHook(sp, ac, av, __func__);
}

int slurm_spank_slurmd_init(spank_t sp, int ac, char **av)
{
    slurm_info("%s: never be here!\n", __func__);
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_job_prolog(spank_t sp, int ac, char **av)
{
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_local_user_init(spank_t sp, int ac, char **av)
{
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_user_init(spank_t sp, int ac, char **av)
{
    printOpt(sp, __func__);
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_task_init_privileged(spank_t sp, int ac, char **av)
{
    printOpt(sp, __func__);
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_task_init(spank_t sp, int ac, char **av)
{
    printOpt(sp, __func__);
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_task_post_fork(spank_t sp, int ac, char **av)
{
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_task_exit(spank_t sp, int ac, char **av)
{
    printOpt(sp, __func__);
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_job_epilog(spank_t sp, int ac, char **av)
{
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_slurmd_exit(spank_t sp, int ac, char **av)
{
    return testHook(sp, ac, av, __func__);
}

int slurm_spank_exit(spank_t sp, int ac, char **av)
{
    return testHook(sp, ac, av, __func__);
}
