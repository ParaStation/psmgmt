#ifndef __PS_SLURM_COMMON
#define __PS_SLURM_COMMON

#include "slurmerrno.h"

#define JOB_BATCH 0
#define JOB_TASKS 1
#define SLURM_SUCCESS 0
#define SLURM_ERROR -1

#define INFINITE8  (0xff)
#define INFINITE16 (0xffff)
#define INFINITE   (0xffffffff)
#define INFINITE64 (0xffffffffffffffff)
#define NO_VAL8    (0xfe)
#define NO_VAL16   (0xfffe)
#define NO_VAL     (0xfffffffe)
#define NO_VAL64   (0xfffffffffffffffe)

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
#define SLURM_MAX_PROTO_VERSION SLURM_19_05_PROTO_VERSION
#define SLURM_MIN_PROTO_VERSION SLURM_17_11_PROTO_VERSION

#define SLURM_19_05_PROTO_VERSION ((34 << 8) | 0)
#define SLURM_18_08_PROTO_VERSION ((33 << 8) | 0)
#define SLURM_17_11_PROTO_VERSION ((32 << 8) | 0)

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
        CPU_BIND_CPUSETS   = 0x8000,

	/* default binding if auto binding doesn't match. */
        CPU_AUTO_BIND_TO_THREADS = 0x04000,
        CPU_AUTO_BIND_TO_CORES   = 0x10000,
        CPU_AUTO_BIND_TO_SOCKETS = 0x20000,

        /* the following is used only as a flag for expressing
         * the contents of TaskPluginParams */
        SLURMD_OFF_SPEC            = 0x40000,
        CPU_BIND_OFF               = 0x80000    /* Disable binding */
} cpu_bind_type_t;

/* memory binding */
typedef enum mem_bind_type {
        /* verbose can be set with any other flag */
        MEM_BIND_VERBOSE= 0x01, /* =v, */
        /* the following manual binding flags are mutually exclusive */
        /* MEM_BIND_NONE needs to be the first in this sub-list */
        MEM_BIND_NONE   = 0x02, /* =no */
        MEM_BIND_RANK   = 0x04, /* =rank */
        MEM_BIND_MAP    = 0x08, /* =map_mem:<list of CPU IDs> */
        MEM_BIND_MASK   = 0x10, /* =mask_mem:<list of CPU masks> */
        MEM_BIND_LOCAL  = 0x20, /* =local */
	/* sort and prefer can be set with any other flags */
        MEM_BIND_SORT   = 0x40, /* =sort */
        MEM_BIND_PREFER = 0x80  /* =prefer */
} mem_bind_type_t;

/* accelerator binding */
typedef enum accel_bind_type {
        ACCEL_BIND_VERBOSE         = 0x01, /* 'v' verbose */
        ACCEL_BIND_CLOSEST_GPU     = 0x02, /* 'g' Use closest GPU to the CPU */
        ACCEL_BIND_CLOSEST_MIC     = 0x04, /* 'm' Use closest NIC to CPU */
        ACCEL_BIND_CLOSEST_NIC     = 0x08  /* 'n' Use closest NIC to CPU */
} accel_bind_type_t;

/*
 * Task distribution states/methods
 *
 * Symbol format is SLURM_DIST_<node>_<socket>_<core>
 *
 * <node>   = Method for distributing tasks to nodes.
 *            This determines the order in which task ids are
 *            distributed to the nodes selected for the job/step.
 * <socket> = Method for distributing allocated lllps across sockets.
 *            This determines the order in which allocated lllps are
 *            distributed across sockets for binding to tasks.
 * <core>   = Method for distributing allocated lllps across cores.
 *            This determines the order in which allocated lllps are
 *            distributed across cores for binding to tasks.
 *
 * Note that the socket and core distributions apply only to task affinity.
 */
typedef enum task_dist_states {
	/* NOTE: start SLURM_DIST_CYCLIC at 1 for HP MPI */
	SLURM_DIST_CYCLIC               = 0x0001,
	SLURM_DIST_BLOCK                = 0x0002,
	SLURM_DIST_ARBITRARY            = 0x0003,
	SLURM_DIST_PLANE                = 0x0004,
	SLURM_DIST_CYCLIC_CYCLIC        = 0x0011,
	SLURM_DIST_CYCLIC_BLOCK         = 0x0021,
	SLURM_DIST_CYCLIC_CFULL         = 0x0031,
	SLURM_DIST_BLOCK_CYCLIC         = 0x0012,
	SLURM_DIST_BLOCK_BLOCK          = 0x0022,
	SLURM_DIST_BLOCK_CFULL          = 0x0032,
	SLURM_DIST_CYCLIC_CYCLIC_CYCLIC = 0x0111,
	SLURM_DIST_CYCLIC_CYCLIC_BLOCK  = 0x0211,
	SLURM_DIST_CYCLIC_CYCLIC_CFULL  = 0x0311,
	SLURM_DIST_CYCLIC_BLOCK_CYCLIC  = 0x0121,
	SLURM_DIST_CYCLIC_BLOCK_BLOCK   = 0x0221,
	SLURM_DIST_CYCLIC_BLOCK_CFULL   = 0x0321,
	SLURM_DIST_CYCLIC_CFULL_CYCLIC  = 0x0131,
	SLURM_DIST_CYCLIC_CFULL_BLOCK   = 0x0231,
	SLURM_DIST_CYCLIC_CFULL_CFULL   = 0x0331,
	SLURM_DIST_BLOCK_CYCLIC_CYCLIC  = 0x0112,
	SLURM_DIST_BLOCK_CYCLIC_BLOCK   = 0x0212,
	SLURM_DIST_BLOCK_CYCLIC_CFULL   = 0x0312,
	SLURM_DIST_BLOCK_BLOCK_CYCLIC   = 0x0122,
	SLURM_DIST_BLOCK_BLOCK_BLOCK    = 0x0222,
	SLURM_DIST_BLOCK_BLOCK_CFULL    = 0x0322,
	SLURM_DIST_BLOCK_CFULL_CYCLIC   = 0x0132,
	SLURM_DIST_BLOCK_CFULL_BLOCK    = 0x0232,
	SLURM_DIST_BLOCK_CFULL_CFULL    = 0x0332,
	SLURM_DIST_NODECYCLIC           = 0x0001,
	SLURM_DIST_NODEBLOCK            = 0x0002,
	SLURM_DIST_SOCKCYCLIC           = 0x0010,
	SLURM_DIST_SOCKBLOCK            = 0x0020,
	SLURM_DIST_SOCKCFULL            = 0x0030,
	SLURM_DIST_CORECYCLIC           = 0x0100,
	SLURM_DIST_COREBLOCK            = 0x0200,
	SLURM_DIST_CORECFULL            = 0x0300,

	SLURM_DIST_NO_LLLP              = 0x1000,
	SLURM_DIST_UNKNOWN              = 0x2000
} task_dist_states_t;

#define SLURM_DIST_STATE_BASE		0x00FFFF
#define SLURM_DIST_STATE_FLAGS		0xFF0000
#define SLURM_DIST_PACK_NODES		0x800000
#define SLURM_DIST_NO_PACK_NODES	0x400000

#define SLURM_DIST_NODEMASK               0xF00F
#define SLURM_DIST_SOCKMASK               0xF0F0
#define SLURM_DIST_COREMASK               0xFF00
#define SLURM_DIST_NODESOCKMASK           0xF0FF

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
#define KILL_FED_REQUEUE 0x0010 /* Mark job as requeued when requeued */

/* task flags */
#define LAUNCH_PARALLEL_DEBUG   0x00000001
#define LAUNCH_MULTI_PROG       0x00000002
#define LAUNCH_PTY              0x00000004
#define LAUNCH_BUFFERED_IO      0x00000008
#define LAUNCH_LABEL_IO         0x00000010
#define LAUNCH_USER_MANAGED_IO  0x00000020

/* node registration flags */
#define SLURMD_REG_FLAG_STARTUP  0x0001
#define SLURMD_REG_FLAG_RESP     0x0002

#endif
