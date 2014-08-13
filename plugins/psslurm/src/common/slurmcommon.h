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

#define MAX_GOVERNOR_LEN 24
#define MAX_STR_LEN (16 * 1024 * 1024)
#define MAX_ARRAY_LEN (128 * 1024)
#define MAX_MEM_LEN (16 * 1024 * 1024)
#define MAX_MSG_SIZE     (128*1024*1024)

#define PATH_BUFFER_LEN 1024

#define SLURM_GLOBAL_AUTH_KEY   0x0001

#define SLURM_14_03_PROTOCOL_VERSION ((27 << 8) | 0)
#define SLURM_2_6_PROTOCOL_VERSION ((26 << 8) | 0)
#define SLURM_2_5_PROTOCOL_VERSION ((25 << 8) | 0)
#define SLURM_BATCH_SCRIPT (0xfffffffe) /* stepid of batch jobs */

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

/* magic slurm signals */
#define SIG_PREEMPTED   994     /* Dummy signal value for job preemption */
#define SIG_DEBUG_WAKE  995     /* Dummy signal value to wake procs stopped
                                 * for debugger */
#define SIG_TIME_LIMIT  996     /* Dummy signal value for time limit reached */
#define SIG_ABORT       997     /* Dummy signal value to abort a job */
#define SIG_NODE_FAIL   998     /* Dummy signal value to signify node failure */
#define SIG_FAILURE     999     /* Dummy signal value to signify sys failure */

/* task flags */
enum task_flag_vals {
    TASK_PARALLEL_DEBUG = 0x1,
    TASK_UNUSED1 = 0x2,
    TASK_UNUSED2 = 0x4
};


#endif
