#ifndef __PS_SLURM_COMMON
#define __PS_SLURM_COMMON

#include "slurmerrno.h"

#define JOB_BATCH 0
#define JOB_TASKS 1
#define SLURM_SUCCESS 0
#define SLURM_ERROR -1

#define NO_VAL (0xfffffffe)
#define GRES_MAGIC 0x438a34d4
#define JOB_OPTIONS_TAG "job_options"
#define KILL_JOB_BATCH 0x0001

#define MAX_GOVERNOR_LEN 24
#define MAX_STR_LEN	 (16 * 1024 * 1024)
#define MAX_ARRAY_LEN	 (128 * 1024)
#define MAX_MEM_LEN	 (16 * 1024 * 1024)
#define MAX_MSG_SIZE	 (128*1024*1024)

#define PATH_BUFFER_LEN 1024

#define SLURM_GLOBAL_AUTH_KEY   0x0001


/* protocol versions */
#ifdef SLURM_PROTOCOL_1605
 #define SLURM_CUR_VERSION 0x100502
 #define SLURM_CUR_PROTOCOL_VERSION_STR "16.05"
 #define SLURM_CUR_PROTOCOL_VERSION  SLURM_16_05_PROTOCOL_VERSION
#else
 #define SLURM_CUR_PROTOCOL_VERSION_STR "14.03"
 #define SLURM_CUR_PROTOCOL_VERSION  SLURM_14_03_PROTOCOL_VERSION
#endif

#define SLURM_16_05_PROTOCOL_VERSION ((30 << 8) | 0)
#define SLURM_15_08_PROTOCOL_VERSION ((29 << 8) | 0)
#define SLURM_14_11_PROTOCOL_VERSION ((28 << 8) | 0)
#define SLURM_14_03_PROTOCOL_VERSION ((27 << 8) | 0)
#define SLURM_2_6_PROTOCOL_VERSION   ((26 << 8) | 0)
#define SLURM_2_5_PROTOCOL_VERSION   ((25 << 8) | 0)

/* stepid of batch jobs */
#define SLURM_BATCH_SCRIPT (0xfffffffe)

/* IO */
#define IO_PROTOCOL_VERSION 0xb001
#define SLURM_IO_KEY_SIZE 8
#define SLURM_IO_STDIN 0
#define SLURM_IO_STDOUT 1
#define SLURM_IO_STDERR 2
#define SLURM_IO_ALLSTDIN 3
#define SLURM_IO_CONNECTION_TEST 4

/* open mode */
#define OPEN_MODE_APPEND        1
#define OPEN_MODE_TRUNCATE      2

/* CPU BIND */
typedef enum cpu_bind_type {    /* cpu binding type from --cpu_bind=... */
        /* verbose can be set with any other flag */
        CPU_BIND_VERBOSE   = 0x01, /* =v, */
        /* the following auto-binding flags are mutually exclusive */
        CPU_BIND_TO_THREADS= 0x02, /* =threads */
        CPU_BIND_TO_CORES  = 0x04, /* =cores */
        CPU_BIND_TO_SOCKETS= 0x08, /* =sockets */
        CPU_BIND_TO_LDOMS  = 0x10, /* locality domains */
        CPU_BIND_TO_BOARDS = 0x1000, /* =boards */
        /* the following manual binding flags are mutually exclusive */
        /* CPU_BIND_NONE needs to be the lowest value among manual bindings */
        CPU_BIND_NONE      = 0x20, /* =no */
        CPU_BIND_RANK      = 0x40, /* =rank */
        CPU_BIND_MAP       = 0x80, /* =map_cpu:<list of CPU IDs> */
        CPU_BIND_MASK      = 0x100,/* =mask_cpu:<list of CPU masks> */
        CPU_BIND_LDRANK    = 0x200,/* =locality domain rank */
        CPU_BIND_LDMAP     = 0x400,/* =map_ldom:<list of locality domains> */
        CPU_BIND_LDMASK    = 0x800,/* =mask_ldom:<list of ldom masks> */

        /* the following is used primarily for the
           --hint=nomultithread when -mblock:block is requested. */
        CPU_BIND_ONE_THREAD_PER_CORE = 0x2000,/* Only bind to one
                                               * thread of a core */

        /* the following is used only as a flag for expressing
         * the contents of TaskPluginParams */
        CPU_BIND_CPUSETS   = 0x8000
} cpu_bind_type_t;

typedef enum mem_bind_type {    /* memory binding type from --mem_bind=... */
        /* verbose can be set with any other flag */
        MEM_BIND_VERBOSE= 0x01, /* =v, */
        /* the following manual binding flags are mutually exclusive */
        /* MEM_BIND_NONE needs to be the first in this sub-list */
        MEM_BIND_NONE   = 0x02, /* =no */
        MEM_BIND_RANK   = 0x04, /* =rank */
        MEM_BIND_MAP    = 0x08, /* =map_mem:<list of CPU IDs> */
        MEM_BIND_MASK   = 0x10, /* =mask_mem:<list of CPU masks> */
        MEM_BIND_LOCAL  = 0x20  /* =local */
} mem_bind_type_t;

/* Possible task distributions across the nodes */
typedef enum task_dist_states {
        /* NOTE: start SLURM_DIST_CYCLIC at 1 for HP MPI */
        SLURM_DIST_CYCLIC = 1,  /* distribute tasks 1 per node, round robin */
        SLURM_DIST_BLOCK,       /* distribute tasks filling node by node */
        SLURM_DIST_ARBITRARY,   /* arbitrary task distribution  */
        SLURM_DIST_PLANE,       /* distribute tasks by filling up
                                   planes of lllp first and then by
                                   going across the nodes See
                                   documentation for more
                                   information */
        SLURM_DIST_CYCLIC_CYCLIC,/* distribute tasks 1 per node,
                                    round robin, same for lowest
                                    level of logical processor (lllp) */
        SLURM_DIST_CYCLIC_BLOCK, /* cyclic for node and block for lllp  */
        SLURM_DIST_BLOCK_CYCLIC, /* block for node and cyclic for lllp  */
        SLURM_DIST_BLOCK_BLOCK, /* block for node and block for lllp  */
        SLURM_NO_LLLP_DIST,     /* No distribution specified for lllp */
        SLURM_DIST_UNKNOWN,     /* unknown dist */
        SLURM_DIST_CYCLIC_CFULL, /* Same as cyclic:cyclic except for
                                    multi-cpu tasks cyclically
                                    bind cpus */
        SLURM_DIST_BLOCK_CFULL, /* Same as block:cyclic except for
                                   multi-cpu tasks cyclically
                                   bind cpus  */
} task_dist_states_t;

/* magic slurm signals */
#define SIG_PREEMPTED   994     /* Dummy signal value for job preemption */
#define SIG_DEBUG_WAKE  995     /* Dummy signal value to wake procs stopped
                                 * for debugger */
#define SIG_TIME_LIMIT  996     /* Dummy signal value for time limit reached */
#define SIG_ABORT       997     /* Dummy signal value to abort a job */
#define SIG_NODE_FAIL   998     /* Dummy signal value to signify node failure */
#define SIG_FAILURE     999     /* Dummy signal value to signify sys failure */

#define KILL_JOB_BATCH  0x0001  /* signal batch shell only */
#define KILL_JOB_ARRAY  0x0002  /* kill all elements of a job array */
#define KILL_STEPS_ONLY 0x0004  /* Do not signal batch script */
#define KILL_FULL_JOB   0x0008  /* Signal all steps, including batch script */


/* task flags */
enum task_flag_vals {
    TASK_PARALLEL_DEBUG = 0x1,
    TASK_UNUSED1 = 0x2,
    TASK_UNUSED2 = 0x4
};

#endif
