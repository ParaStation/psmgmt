/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */


#ifndef __PS_ACCOUNT_TYPES
#define __PS_ACCOUNT_TYPES

#include <sys/resource.h>
#include <stdint.h>

#include "pstask.h"

typedef struct {
    uint64_t cputime;
    uint64_t utime;
    uint64_t stime;
    uint64_t mem;
    uint64_t vmem;
    int count;
} psaccAccountInfo_t;

typedef struct {
    pid_t session;
    pid_t pgroup;
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
    uint64_t majflt;
    uint64_t rChar;
    uint64_t wChar;
    uint64_t readBytes;
    uint64_t writeBytes;
    uint64_t cpuFreq;
    uint64_t cpuWeight;
} AccountData_t;

typedef enum {
    ACCID_MAX_VSIZE,
    ACCID_MAX_RSS,
    ACCID_MAX_PAGES,
    ACCID_MIN_CPU,
    ACCID_MAX_DISKREAD,
    ACCID_MAX_DISKWRITE
} ExAccTaskIds_t;

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
    uint64_t minCputime;
    uint64_t pageSize;
    uint32_t numTasks;
    uint64_t mem;
    uint64_t vmem;
    uint64_t maxMajflt;
    uint64_t totMajflt;
    uint64_t totCputime;
    uint64_t cpuFreq;
    double maxDiskRead;
    double totDiskRead;
    double maxDiskWrite;
    double totDiskWrite;
    PStask_ID_t taskIds[6];
    struct rusage rusage;
} AccountDataExt_t;

typedef struct {
    pid_t ppid;
    pid_t pgroup;
    pid_t session;
    char state[1];
    uint64_t utime;
    uint64_t stime;
    uint64_t cutime;
    uint64_t cstime;
    uint64_t threads;
    uint64_t vmem;
    uint64_t mem;
    uint64_t majflt;
    uint64_t cmajflt;
    uint16_t cpu;
    uid_t uid;
} ProcStat_t;

#endif
