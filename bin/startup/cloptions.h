/*
 * ParaStation
 *
 * Copyright (C) 2017-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Handle command-line options of mpiexec et al.
 */
#ifndef __CLOPTIONS_H
#define __CLOPTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "psenv.h"
#include "psreservation.h"

/** Information on executables to start */
typedef struct {
    int np;             /**< number of instances to spawn */
    uint32_t hwType;    /**< hardware type the used node has to support */
    int ppn;            /**< number of processes per node */
    int tpp;            /**< number of HW-threads used by a single process */
    PSrsrvtn_ID_t resID;/**< ID of the reservation holding resources */
    int argc;           /**< number of arguments in argv */
    char **argv;        /**< executable's argument vector */
    char *wdir;         /**< executable's working directory */
    env_t env;          /**< executable's environment set at CL */
    char *envList;      /**< executable's list of environment to propagate */
    bool envall;        /**< Flag to propagate the whole environment */
} Executable_t;

/** configuration derived from command line options */
typedef struct {
    int uSize;          /**< Universe size of the job to create */
    int np;             /**< Total number of processes to start */
    Executable_t *exec; /**< Array w/ description of executables to start */
    int execCount;      /**< Number of valid entries in @ref exec */
    int execMax;        /**< Total number of entries in @ref exec */
    int maxTPP;         /**< Maximum @ref tpp entry in @ref exec */
    int envTPP;         /**< Thread per process requested via environment */
    bool dryrun;        /**< Flag dryrun, i.e. do not spawn processes */
    bool envall;        /**< Flag to propagate the whole environment */
    bool execEnvall;    /**< Flag propagate whole environment for executable */
    mode_t u_mask;      /**< File mode creation mask to be used */
    /* resource options */
    char *nList;        /**< List of node IDs to use (not required for batch) */
    char *hList;        /**< List of hosts to use (not required for batch) */
    char *hFile;        /**< File with hosts to use (not required for batch) */
    char *sort;         /**< How to sort resource candidates for assignment */
    bool overbook;      /**< Flag to use assigned HW-threads multiple times */
    bool exclusive;     /**< Flag exclusive use resources (HW-threads) */
    bool wait;          /**< Flag to wait for HW-threads to become available */
    bool loopnodesfirst;/**< Flag HW-thread allocation by looping over nodes */
    bool dynamic;       /**< Flag the dynamic extenion of HW-threads */
    bool fullPartition; /**< Flag getting complete nodes in list as partition */
    /* special modes */
    bool gdb;           /**< Flag debugging mode, i.e. start processes in gdb */
    bool gdb_noargs;    /**< Flag to don't call gdb with --args option */
    bool valgrind;      /**< Flag valgrind mode */
    bool memcheck;      /**< Flag use of valgrind's leak-check=full mode */
    bool callgrind;     /**< Flag use of valgrind's callgrind tool */
    bool mpichComp;     /**< Flag MPIch-1 compatibility */
    bool PMIx;          /**< Flag PMIx support */
    /* PMI options */
    bool pmiTCP;        /**< Flag use of TCP sockets to listen for PMI calls */
    bool pmiSock;       /**< Flag use of Unix sockets to listen for PMI calls */
    int pmiTmout;       /**< Timeout to be used by PMI */
    bool pmiDbg;        /**< Flag debug messages from PMI */
    bool pmiDbgClient;  /**< Flag additional debug from the PMI plugin */
    bool pmiDbgKVS;     /**< Flag additional debug from PMI's key-value space */
    bool pmiDisable;    /**< Flag overall disabling of PMI */
    /* options going to pscom library */
    int PSComSndbuf;    /**< PSCom's TCP send-buffer size */
    int PSComRcvbuf;    /**< PSCom's TCP receive-buffer size */
    bool PSComNoDelay;  /**< PSCom's TCP no delay flag */
    bool PSComSchedYield;/**< PSCom's sched yield flag */
    int PSComRetry;     /**< PSCom's TCP retry count */
    bool PSComSigQUIT;  /**< Flag to let PSCom dump state info on SIGQUIT */
    int PSComOnDemand;  /**< Flag PSCom to (not)use / leave on-demand conn */
    bool PSComColl;     /**< Flag PSCom to use enhanced collectives */
    char *PSComPlgnDir; /**< Hint PSCom to alternative location of plugins */
    char *PSComDisCom;  /**< Tell PSCom which plugins *not* to use */
    char *PSComNtwrk;   /**< Tell PSCom which (TCP-)network to use */
    /* options going to psmgmt (psid/logger/forwarder) */
    bool sourceprintf;  /**< Flag to prepend info on source of output */
    bool merge;         /**< Flag intelligent merging of output */
    int mergeDepth;     /**< Depth of output-merging */
    int mergeTmout;     /**< Timeout for merging output */
    bool rusage;        /**< Flag to give info on used resources upon exit */
    bool timestamp;     /**< Flag to timestamp all output */
    bool interactive;   /**< Flag interactive execution of program to start */
    int maxtime;        /**< Maximum runtime of program to start */
    char *dest;         /**< Destination ranks of input */
    char *envList;      /**< List of environment variables to propagate */
    char *path;         /**< Search PATH to use within application */
    /* debug options */
    bool loggerDbg;     /**< Flag debug output from psilogger */
    bool forwarderDbg;  /**< Flag debug output from forwarder */
    bool PSComDbg;      /**< Flag debug output from PSCom */
    bool loggerrawmode; /**< Flag switching logger to raw-mode */
    unsigned int psiDbgMask;  /**< Set libpsi's debug mask */
    bool verbose;       /**< Flag to be more verbose by oneself */
} Conf_t;

/**
 * @brief Parse and check the command line options
 *
 * Parse and check the command line options and return the result
 * within a structure of type @ref Conf_t. The structure is
 * dynamically allocated and shall be freed by passing it to @ref
 * releaseConf().
 *
 * @param argc Number of arguments in @a argv
 *
 * @param argv Arguments to parse
 *
 * @return Pointer to a dynamically allocated structure of type @ref
 * Conf_t containing a description of the parsed command line
 * information. To be freed by passing to @ref releaseConf().
*/
Conf_t * parseCmdOptions(int argc, const char *argv[]);

/**
 * @brief Free configuration
 *
 * Release the configuration @a conf created via @ref
 * parseCmdOptions(). This includes freeing all dynamic memory
 * allocated for @a conf.
 *
 * @param conf Configuration to release
 *
 * @retrun No return value
 */
void releaseConf(Conf_t *conf);

#endif /* __CLOPTIONS_H */
