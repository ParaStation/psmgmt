/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "psaccountlog.h"
#include "psaccountconfig.h"
#include "psaccountclient.h"

#include "psaccountproc.h"

#define PROC_CPU_INFO	"/proc/cpuinfo"
#define SYS_CPU_FREQ	"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq"

static LIST_HEAD(procList);

static int *cpuFreq = NULL;

static int cpuCount = 0;

static bool cpuGovEnabled = false;

static int getCPUCount()
{
    int count = 0;
    char buf[256];
    FILE *fd;

    if (!(fd = fopen(PROC_CPU_INFO, "r"))) {
	mlog("%s: open '%s' failed\n", __func__, PROC_CPU_INFO);
	return 0;
    }

    while ((fgets(buf, sizeof(buf), fd))) {
	if (!(strncmp(buf, "processor", 9))) count++;
    }
    fclose(fd);

    return count;
}

static void initCpuFreq()
{
    FILE *fd;
    char buf[256], *tmp, *sfreq;
    struct stat sbuf;
    int freq, i;

    cpuFreq = umalloc(sizeof(*cpuFreq) * cpuCount);

    snprintf(buf, sizeof(buf), SYS_CPU_FREQ, 0);

    if ((stat(buf, &sbuf)) == -1) {
	if (!(fd = fopen(PROC_CPU_INFO, "r"))) {
	    cpuCount = 0;
	    ufree(cpuFreq);
	    return;
	}

	while ((fgets(buf, sizeof(buf), fd))) {
	    if (!(strncmp(buf, "cpu MHz", 7)) ||
		!(strncmp(buf, "cpu GHz", 7))) {
		break;
	    }
	}

	sfreq = strchr(buf, ':') + 2;
	tmp = sfreq;
	while (tmp++) {
	    if (tmp[0] == '.') {
		tmp[0] = '0';
		break;
	    }
	}
	i = strlen(sfreq);
	sfreq[i-2] = '\0';

	if ((sscanf(sfreq, "%d", &freq)) != 1) {
	    cpuCount = 0;
	    ufree(cpuFreq);
	    fclose(fd);
	    return;
	}

	for (i=0; i<cpuCount; i++) {
	    cpuFreq[i] = freq;
	}

	fclose(fd);
	cpuGovEnabled = false;
    } else {
	cpuGovEnabled = true;
    }
}

static void updateCpuFreq()
{
    FILE *fd;
    int i;
    char buf[256];

    for (i=0; i<cpuCount; i++) {
	snprintf(buf, sizeof(buf), SYS_CPU_FREQ, i);

	if (!(fd = fopen(buf, "r"))) {
	    mwarn(errno, "%s: fopen(%s) failed : ", __func__, buf);
	    continue;
	}

	if ((fscanf(fd, "%d", &cpuFreq[i])) != 1) {
	    mwarn(errno, "%s: fscanf(%s) failed : ", __func__, buf);
	}

	fclose(fd);
    }
}

int getCpuFreq(int cpuID)
{
    if (cpuID < cpuCount) return cpuFreq[cpuID];

    return -1;
}

static void clearCpuFreq(void)
{
    if (cpuCount) ufree(cpuFreq);
    cpuCount = 0;
}

ProcSnapshot_t *findProcSnapshot(pid_t pid)
{
    list_t *pos;
    list_for_each(pos, &procList) {
	ProcSnapshot_t *proc = list_entry(pos, ProcSnapshot_t, next);
	if (proc->pid == pid) return proc;
    }

    return NULL;
}

/**
 * @brief Send the actual kill signal.
 *
 * @param child PID of child to kill.
 *
 * @param pgroup Pgroup of child to kill.
 *
 * @param sig Signal to send.
 *
 * @return No return value.
 */
static void doKill(pid_t child, pid_t pgroup, int sig)
{
    /*
    mlog("%s: killing child '%i' pgroup '%i' sig '%i'\n", __func__, child,
	    pgroup, sig);
    */
    if (pgroup > 0) killpg(pgroup, sig);
    kill(child, sig);
}

int signalChildren(pid_t mypid, pid_t child, pid_t pgrp, int sig)
{
    int sendCount = 0;
    list_t *pos;

    /* never send signal to myself */
    if (child == mypid) return 0;

    list_for_each(pos, &procList) {
	ProcSnapshot_t *childProc = list_entry(pos, ProcSnapshot_t, next);
	int myPgrp = (pgrp > 0) ? childProc->pgrp : pgrp;
	if (childProc->ppid == child) {
	    sendCount += signalChildren(mypid, childProc->pid, myPgrp, sig);
	}
    }
    doKill(child, pgrp, sig);
    sendCount++;

    return sendCount;
}

int signalSession(pid_t session, int sig)
{
    pid_t mypid = getpid();
    int sendCount = 0;
    list_t *pos;

    /* don't kill zombies */
    if (session < 1) return 0;

    /* we need up2date information */
    updateProcSnapshot();

    list_for_each(pos, &procList) {
	ProcSnapshot_t *childProc = list_entry(pos, ProcSnapshot_t, next);
	pid_t child = childProc->pid;

	if (childProc->session == session) {
	    if (child != mypid && child > 0) {
		sendCount += signalChildren(mypid, child, childProc->pgrp, sig);
	    }
	}
    }

    return sendCount;
}

void findDaemonProcesses(uid_t uid, bool kill, bool warn)
{
    pid_t mypid = getpid();
    list_t *pos;

    /* we need up2date information */
    updateProcSnapshot();

    list_for_each(pos, &procList) {
	ProcSnapshot_t *childProc = list_entry(pos, ProcSnapshot_t, next);

	if (childProc->uid == uid && childProc->ppid == 1) {
	    if (warn) mlog("found %sdaemon process: pid '%i' uid '%i'\n",
			   kill ? "and kill " : "", childProc->pid, uid);
	    if (kill) signalChildren(mypid, childProc->pid,
				     childProc->pgrp, SIGKILL);
	}
    }
}

static bool isDescendantSnap(pid_t parent, pid_t child)
{
    ProcSnapshot_t *procChild;

    if (!child) return false;

    procChild = findProcSnapshot(child);
    if (!procChild) {
	//mlog("%s: child %i  not found in snapshot\n", __func__, child);
	return false;
    }

    if (procChild->ppid == parent) return true;

    //mlog("%s: child(%i)->parent %i not parent %i\n", __func__,
    //		procChild->pid, procChild->ppid, parent);
    return isDescendantSnap(parent, procChild->ppid);
}

/**
 * @brief Check if a child pid belongs to a parent pid.
 *
 * @param parent The parent pid.
 *
 * @param child The child pid.
 *
 * @return Returns 1 if the second pid is a child of the first pid or 0
 * otherwise.
 */
static bool isDescendantLive(pid_t parent, pid_t child)
{
    ProcStat_t pS;

    if (child <= 1) return false;

    if (!readProcStatInfo(child, &pS)) return false;
    if (pS.ppid == parent) return true;

    return isDescendantLive(parent, pS.ppid);
}

bool isDescendant(pid_t parent, pid_t child)
{
    if (list_empty(&procList)) return isDescendantLive(parent,child);

    return isDescendantSnap(parent, child);
}

bool readProcIO(pid_t pid, ProcIO_t *io)
{
    FILE *fd;
    char buf[200];
    struct stat sbuf;

    memset(io, 0, sizeof(*io));

    /* format string of /proc/<pid>/io */
    static char io_format[] =
		    "%*[^:]: "
		    "%"PRIu64" "     /* rchar / disk read */
		    "%*[^:]: "
		    "%"PRIu64" "     /* wchar / disk write */
		    "%*[^:]: "
		    "%*"PRIu64" "    /* syscr */
		    "%*[^:]: "
		    "%*"PRIu64" "    /* syscw */
		    "%*[^:]: "
		    "%"PRIu64" "     /* read_bytes */
		    "%*[^:]: "
		    "%"PRIu64" ";    /* write_bytes */

    snprintf(buf, sizeof(buf), "/proc/%i/io", pid);

    if (stat(buf, &sbuf) == -1) return false;

    fd = fopen(buf,"r");
    if (!fd) {
	mlog("%s: open '%s' failed\n", __func__, buf);
	return false;
    }

    if (fscanf(fd, io_format, &io->diskRead, &io->diskWrite, &io->readBytes,
	       &io->writeBytes) != 4) {
	fclose(fd);
	return false;
    }

    /*
    mlog("%s: io stat: rChar '%zu' wChar '%zu' read_byes '%zu' writeBytes "
	    "'%zu'\n", __func__, io->rChar, io->wChar, io->readBytes,
	    io->writeBytes);
    */
    fclose(fd);
    return true;
}

bool readProcStatInfo(pid_t pid, ProcStat_t *pS)
{
    FILE *fd;
    char buf[200];
    struct stat sbuf;

    /* format string of /proc/<pid>/stat */
    static char stat_format[] =
		    "%*d "	    /* pid */
		    "(%*[^)]) "	    /* comm */
		    "%c "	    /* state */
		    "%u "	    /* ppid */
		    "%u "	    /* pgrp */
		    "%u "	    /* session */
		    "%*d "	    /* tty_nr */
		    "%*d "	    /* tpgid */
		    "%*u "	    /* flags */
		    "%*lu %*lu "    /* minflt cminflt */
		    "%lu %lu "      /* majflt cmajflt */
		    "%lu %lu "      /* utime stime */
		    "%lu %lu "      /* cutime cstime */
		    "%*d "	    /* priority */
		    "%*d "	    /* nice */
		    "%lu "	    /* num_threads */
		    "%*u "	    /* itrealvalue */
		    "%*u "	    /* starttime */
		    "%lu %lu "	    /* vsize rss*/
		    "%*u "	    /* rlim */
		    "%*u %*u "	    /* startcode endcode startstack */
		    "%*u "	    /* startstack */
		    "%*u %*u "	    /* kstkesp kstkeip */
		    "%*u %*u "	    /* signal blocked */
		    "%*u %*u "	    /* sigignore sigcatch */
		    "%*u "	    /* wchan */
		    "%*u %*u "	    /* nswap cnswap */
		    "%*d "	    /* exit_signal (kernel 2.1.22) */
		    "%d "	    /* processor  (kernel 2.2.8) */
		    "%*lu "	    /* rt_priority (kernel 2.5.19) */
		    "%*lu "	    /* policy (kernel 2.5.19) */
		    "%*llu";	    /* delayacct_blkio_ticks (kernel 2.6.18) */

    snprintf(buf, sizeof(buf), "/proc/%i/stat", pid);

    if (stat(buf, &sbuf) == -1) return false;
    pS->uid = sbuf.st_uid;

    fd = fopen(buf,"r");
    if (!fd) {
	mlog("%s: open '%s' failed\n", __func__, buf);
	return false;
    }

    pS->state[0] = '\0';
    if (fscanf(fd, stat_format, pS->state, &pS->ppid, &pS->pgrp,
	       &pS->session, &pS->majflt, &pS->cmajflt, &pS->utime,
	       &pS->stime, &pS->cutime, &pS->cstime, &pS->threads,
	       &pS->vmem, &pS->mem, &pS->cpu) != 14) {
	fclose(fd);
	return false;
    }

    fclose(fd);
    return true;
}

/**
 * @brief Add a new proc snapshot.
 *
 * @return Returns the created proc snapshot.
 */
static ProcSnapshot_t *addProc(pid_t pid, ProcStat_t *pS)
{
    ProcSnapshot_t *proc;

    proc = umalloc(sizeof(*proc));
    if (!proc) return NULL;
    proc->uid = pS->uid;
    proc->pid = pid;
    proc->ppid = pS->ppid;
    proc->pgrp = pS->pgrp;
    proc->session = pS->session;
    proc->cutime = pS->utime + pS->cutime;
    proc->cstime = pS->stime + pS->cstime;
    proc->majflt = pS->majflt + pS->cmajflt;
    proc->threads = pS->threads;
    proc->mem = pS->mem;
    proc->vmem = pS->vmem;
    proc->cpu = pS->cpu;

    list_add_tail(&proc->next, &procList);
    return proc;
}

/**
 * @brief Delete all proc snapshots.
 *
 * @return No return value.
 */
static void clearAllProcSnapshots(void)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &procList) {
	ProcSnapshot_t *proc = list_entry(pos, ProcSnapshot_t, next);
	list_del(&proc->next);
	ufree(proc);
    }
}

#define MAX_USER 300

void getSessionInfo(int *count, char *buf, size_t size, int *userCount)
{
    uid_t users[MAX_USER];
    list_t *pos;

    buf[0] = '\0';
    *count = 0;
    *userCount = 0;

    list_for_each(pos, &procList) {
	ProcSnapshot_t *proc = list_entry(pos, ProcSnapshot_t, next);
	char sessStr[50];
	int i;

	if (proc->uid == 0 || proc->session == 0) continue;
	(*count)++;
	snprintf(sessStr, sizeof(sessStr), "%i ", proc->session);
	strncat(buf, sessStr, size - strlen(buf));

	for (i=0; i < *userCount; i++) {
	    if (users[i] == proc->uid) break;
	}
	if (i == *userCount) {
	    if (*userCount == MAX_USER-1) {
		mlog("%s: MAX_USER exceeded. Truncating sessions\n", __func__);
		continue;
	    }
	    users[(*userCount)++] = proc->uid;
	}
    }
    buf[size-1] = '\0';
    if (strlen(buf) > 0) buf[strlen(buf) - 1] = '\0';
}

/**
 * @brief Collect resources for descendants
 *
 * recursively collect resource information for descendants of the
 * given process identified by @a pid. Resource information is
 * collected in @a res.
 *
 * @param pid Process ID of the root process
 *
 * @param res Structure used to accumulate resource information
 *
 * @return No return value
 */
static void collectDescendantData(pid_t pid, ProcSnapshot_t *res)
{
    list_t *pos;
    list_for_each(pos, &procList) {
	ProcSnapshot_t *childProc = list_entry(pos, ProcSnapshot_t, next);

	if (childProc->ppid == pid) {
	    res->mem += childProc->mem;
	    res->vmem += childProc->vmem;
	    res->cutime += childProc->cutime;
	    res->cstime += childProc->cstime;

	    mdbg(PSACC_LOG_PROC, "%s: pid:%i ppid:%i cutime:%lu "
		 "cstime:%lu mem:%lu vmem:%lu\n", __func__,
		 childProc->pid, childProc->ppid,
		 childProc->cutime, childProc->cstime,
		 childProc->mem, childProc->vmem);
	    collectDescendantData(childProc->pid, res);
	}
    }
}

void getDescendantData(pid_t pid, ProcSnapshot_t *res)
{
    if (!res) return;

    res->pid = pid;
    res->ppid = 0;
    res->session = 0;
    res->mem = 0;
    res->vmem = 0;
    res->cutime = 0;
    res->cstime = 0;
    res->majflt = 0;
    res->cpu = 0;

    collectDescendantData(pid, res);

    mdbg(PSACC_LOG_PROC, "%s: pid:%i mem:%lu vmem:%lu cutime:%lu cstime:%lu\n",
	 __func__, pid, res->mem, res->vmem, res->cutime, res->cstime);
}

void updateProcSnapshot(void)
{
    DIR *dir;
    struct dirent *dent;
    pid_t pid = -1;
    ProcStat_t pS;
    bool ignoreRoot = getConfValueI(&config, "IGNORE_ROOT_PROCESSES");

    /* clear all previous proc entrys */
    clearAllProcSnapshots();

    if (!(dir = opendir("/proc/"))) {
	mlog("%s: open /proc failed\n", __func__);
	return;
    }

    if (cpuGovEnabled) updateCpuFreq();

    rewinddir(dir);
    while ((dent = readdir(dir)) != NULL) {
	if ((pid = atoi(dent->d_name)) <= 0) {
	    mdbg(PSACC_LOG_PROC, "%s: pid '%i' too small for d_name '%s'\n",
		__func__, pid, dent->d_name);
	    continue;
	}

	if (readProcStatInfo(pid, &pS) && (!ignoreRoot || pS.uid)) {
	    addProc(pid, &pS);
	}
    }
    closedir(dir);
}

void initProc(void)
{
    cpuCount = getCPUCount();
    if (cpuCount) initCpuFreq();
}

void finalizeProc(void)
{
    clearAllProcSnapshots();
    clearCpuFreq();
}
