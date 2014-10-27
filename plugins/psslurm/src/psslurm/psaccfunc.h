/*
 *               ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PS_PELOGUE_ACCOUNT_FUNC
#define __PS_PELOGUE_ACCOUNT_FUNC

#include <stdbool.h>
#include <stdint.h>
#include <sys/resource.h>

#include "pluginenv.h"
#include "psnodes.h"
#include "psslurmconfig.h"

typedef struct {
    uint64_t cputime;
    uint64_t utime;
    uint64_t stime;
    uint64_t mem;
    uint64_t vmem;
    int count;
} psaccAccountInfo_t;

typedef struct {
    uint64_t maxThreads;
    uint64_t maxVsize;
    uint64_t maxRss;
    uint64_t avgThreads;
    uint64_t avgThreadsCount;
    uint64_t avgVsize;
    uint64_t avgVsizeCount;
    uint64_t avgRss;
    uint64_t avgRssCount;
    uint64_t cutime;
    uint64_t cstime;
    uint64_t cputime;
    uint64_t pageSize;
    struct rusage rusage;
} AccountDataExt_t;

/**
 * @brief Send a signal to a session.
 *
 * @param session The session ID to send the signal to.
 *
 * @param sig The signal to send.
 *
 * @return Returns the number of children which the signal
 * was sent to.
 */
int (*psAccountsendSignal2Session)(pid_t, int);

/**
 * @brief Register a PBS jobscript via its pid.
 *
 * This function is called by the psmom, because only
 * the psmom knows the pid of the jobscript. The psaccount
 * plugin is then able to identify all processes associated
 * with this jobscript.
 *
 * This enables the psmom the get valid accounting data for
 * all processes in the job, although it only knows the jobscript.
 *
 * @param jsPid The pid of the jobscript to register.
 *
 * @param jobid The torque jobid.
 *
 * @return No return value.
 */
void (*psAccountRegisterJob)(pid_t, char *);

/**
 * @brief Unregister a PBS jobscript.
 *
 * The job has finished and the psmom is telling us to stop
 * accounting for this jobscript.
 *
 * @param jsPid The pid of the jobscript to un-register.
 *
 * @return No return value.
 */
void (*psAccountUnregisterJob)(pid_t);

/**
 * @brief Enable the global collection of accounting data.
 *
 * This function is called by the psmom to enable
 * the global collection of accounting data. This way all
 * psaccount plugins will automatic forward all information
 * to the node were the logger is executed.
 *
 * @param active If flag is 1 the global collect mode is switched
 * on. If the flag is 0 it is swichted off.
 *
 * @return No return value.
 */
void (*psAccountSetGlobalCollect)(int);

/**
 * @brief Get account info for a jobscript.
 *
 * @param jobscript The jobscript to get the info for.
 *
 * @param accData A pointer to an accountInfo structure which will receive the
 * requested information.
 *
 * @return Returns 1 on success and 0 on error.
 */
int (*psAccountGetJobInfo)(pid_t, psaccAccountInfo_t *);

int (*psAccountGetJobData)(pid_t, AccountDataExt_t *);

int (*psPelogueAddPluginConfig)(char *, Config_t *);

void (*psPelogueAddJob)(const char *, const char *, uid_t, gid_t, int,
			PSnodes_ID_t *,
			void (*pluginCallback)(char *, int, int));

int (*psPelogueStartPE)(const char *, const char *, bool, env_t *);

void (*psPelogueDeleteJob)(const char *, const char *);

int (*psPelogueSignalPE)(const char *, const char *, int, char *);

int (*psMungeEncode)(char **);
int (*psMungeDecode)(const char *, uid_t *, gid_t *);
int (*psMungeDecodeBuf)(const char *, void **, int *, uid_t *, gid_t *);

#endif
