/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psidutil.h"
#include "psidhook.h"
#include "psidsignal.h"

#include "psaccountconfig.h"
#include "psaccountlog.h"

/** List of all known processes cached from /proc */
static LIST_HEAD(procList);

/** data structure to handle a pool of proc snapshot structures */
static PSitems_t procPool = NULL;

static bool relocProc(void *item)
{
    ProcSnapshot_t *orig = item, *repl = PSitems_getItem(procPool);
    if (!repl) return false;

    /* copy proc snapshot struct's content */
    repl->state = orig->state;
    repl->uid = orig->uid;
    repl->pid = orig->pid;
    repl->ppid = orig->ppid;
    repl->pgrp = orig->pgrp;
    repl->session = orig->session;
    repl->cutime = orig->cutime;
    repl->cstime = orig->cstime;
    repl->threads = orig->threads;
    repl->vmem = orig->vmem;
    repl->mem = orig->mem;
    repl->majflt = orig->majflt;
    repl->cpu = orig->cpu;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

static void proc_gc(void)
{
    if (!PSitems_gcRequired(procPool)) return;

    fdbg(PSACC_LOG_PROC, "\n");
    PSitems_gc(procPool, relocProc);
}

static int clearMem(void *dummy)
{
    PSitems_clearMem(procPool);
    procPool = NULL;
    INIT_LIST_HEAD(&procList);

    return 0;
}

/* ---------------------------------------------------------------------- */

#define PROC_CPU_INFO	"/proc/cpuinfo"
#define SYS_CPU_FREQ	"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq"

/** array of CPU frequencies in kHz */
static int *cpuFreq = NULL;

static int cpuCount = 0;

static bool cpuGovEnabled = false;

static int getCPUCount(void)
{
    int count = 0;
    char buf[256];
    FILE *fd = fopen(PROC_CPU_INFO, "r");

    if (!fd) {
	flog("open '%s' failed\n", PROC_CPU_INFO);
	return 0;
    }

    while (fgets(buf, sizeof(buf), fd)) {
	if (!strncmp(buf, "processor", 9)) count++;
    }
    fclose(fd);

    return count;
}

static void initCpuFreq(void)
{
    char buf[256];
    struct stat sbuf;

    cpuFreq = umalloc(sizeof(*cpuFreq) * cpuCount);

    snprintf(buf, sizeof(buf), SYS_CPU_FREQ, 0);
    if (!stat(buf, &sbuf)) {
	 /* we have a scaling governor */
	cpuGovEnabled = true;
    } else {
	/* try to determine static frequency assuming all CPUs are identical */
	FILE *fd = fopen(PROC_CPU_INFO, "r");
	double freq, freqFactor = 1.0;

	cpuGovEnabled = false;
	if (!fd) {
	    cpuCount = 0;
	    ufree(cpuFreq);
	    return;
	}

	while (fgets(buf, sizeof(buf), fd)) {
	    if (!strncmp(buf, "cpu MHz", 7)) {
		freqFactor = 1.0e3;
		break;
	    } else if (!strncmp(buf, "cpu GHz", 7)) {
		freqFactor = 1.0e6;
		break;
	    }
	}

	/* did we find anything? */
	if (!feof(fd)) { /* yes, we did */
	    char *sfreq = strchr(buf, ':') + 2;

	    if (sscanf(sfreq, "%lf", &freq) != 1) {
		/* read failed => fall back to 1 GHz default */
		freq = 1.0e6;
		freqFactor = 1.0;
	    }
	} else {
	    /* Assume the default of 1 GHz */
	    freq = 1.0e6;
	}
	fclose(fd);

	int i;
	for (i=0; i<cpuCount; i++) cpuFreq[i] = freq * freqFactor;
    }
}

static void updateCpuFreq(void)
{
    int i;
    char buf[256];

    for (i=0; i<cpuCount; i++) {
	snprintf(buf, sizeof(buf), SYS_CPU_FREQ, i);
	FILE *fd = fopen(buf, "r");

	if (!fd) {
	    fwarn(errno, "fopen(%s) failed : ", buf);
	    continue;
	}

	if (fscanf(fd, "%d", &cpuFreq[i]) != 1) {
	    fwarn(errno, "fscanf(%s) failed : ", buf);
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
    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
	if (proc->pid == pid) return proc;
    }

    return NULL;
}

/**
 * @brief Send the actual kill signal using pskill()
 *
 * @param pid PID of process to kill
 *
 * @param pgroup Pgroup of process to kill
 *
 * @param sig Signal to send.
 *
 * @return No return value.
 */
static void doKill(pid_t pid, pid_t pgroup, int sig)
{
    fdbg(PSACC_LOG_SIGNAL, "(pid %d pgroup %d sig %d)\n", pid, pgroup, sig);

    ProcSnapshot_t *ps = findProcSnapshot(pid);
    if (!ps) {
	flog("proc snapshot for PID %i not found\n", pid);
	/* fallback to direct kill() */
	if (pgroup > 0) kill(-pgroup, sig);
	kill(pid, sig);
    } else {
	ps->state = PROC_SIGNALED;
	if (pgroup > 0) pskill(-pgroup, sig, ps->uid);
	pskill(pid, sig, ps->uid);
    }
}

/**
 * @brief Send signal to process and all descendants
 *
 * Send the signal @a sig to the process @a pid and all its
 * descendants. At the same time it is ensured that the signal will
 * not be sent to the process @a mypid. This is to ensure that a
 * process will not kill itself by accident. Beyond that the not only
 * the processes itself are killed but also the corresponding process
 * group @a pgrp unless it is 0. In the latter case the process group
 * of the process itself as determined by psaccount will receive the
 * signal.
 *
 * @param mypid My own PID to protect myself
 *
 * @param pid PID of the process to receive the first signal
 *
 * @param pgrp Process group to also receive the signal
 *
 * @param sig Signal to send
 *
 * @return Total number of signals sent
 */
static int signalChildren(pid_t mypid, pid_t pid, pid_t pgrp, int sig)
{
    int sendCount = 0;

    /* never send signal to myself */
    if (pid == mypid) return 0;

    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
	if (proc->state == PROC_SIGNALED) continue;
	if (proc->state == PROC_UNUSED) {
	    flog("abort due to UNUSED proc snapshot\n");
	    break;
	}
	if (proc->ppid == pid) {
	    sendCount += signalChildren(mypid, proc->pid,
					(pgrp > 0) ? proc->pgrp : pgrp, sig);
	}
    }
    doKill(pid, pgrp, sig);
    sendCount++;

    return sendCount;
}

int signalSession(pid_t session, int sig)
{
    pid_t mypid = getpid();
    int sendCount = 0;

    /* don't kill zombies */
    if (session < 1) return 0;

    /* block signals to prevent asynchronous call via sighandler */
    bool blockedTERM = PSID_blockSig(SIGTERM, true);
    bool blockedALRM = PSID_blockSig(SIGALRM, true);

    /* we need up2date information */
    updateProcSnapshot();

    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
	if (proc->state == PROC_SIGNALED) continue;
	if (proc->state == PROC_UNUSED) {
	    flog("(%d, %d): abort due to UNUSED proc snapshot\n", session, sig);
	    break;
	}
	if (proc->session == session && proc->pid != mypid && proc->pid > 0) {
	    sendCount += signalChildren(mypid, proc->pid, proc->pgrp, sig);
	}
    }

    PSID_blockSig(SIGALRM, blockedALRM);
    PSID_blockSig(SIGTERM, blockedTERM);

    return sendCount;
}

void findDaemonProcs(uid_t uid, bool kill, bool warn)
{
    pid_t mypid = getpid();

    /* we need up2date information */
    updateProcSnapshot();

    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
	if (proc->state == PROC_SIGNALED) continue;
	if (proc->state == PROC_UNUSED) {
	    flog("abort due to UNUSED proc snapshot\n");
	    break;
	}
	if (proc->uid == uid && proc->ppid == 1) {
	    if (warn) mlog("found %sdaemon process: pid %i uid %i\n",
			   kill ? "and kill " : "", proc->pid, uid);
	    if (kill) signalChildren(mypid, proc->pid, proc->pgrp, SIGKILL);
	}
    }
}

static bool isDescendantSnap(pid_t parent, pid_t child)
{
    ProcSnapshot_t *procChild;

    if (!child) return false;

    procChild = findProcSnapshot(child);
    if (!procChild) {
	fdbg(PSACC_LOG_PROC, "child %i not found\n", child);
	return false;
    }

    if (procChild->ppid == parent) return true;

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

    if (!readProcStat(child, &pS)) return false;
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
    char fileName[128];
    struct stat sbuf;
    int ret, eno;

    /* format string of /proc/<pid>/io */
    static char io_format[] =
		    "%*[^:]: "
		    "%"PRIu64" "     /* rchar / disk read */
		    "%*[^:]: "
		    "%"PRIu64" "     /* wchar / disk write */
		    "%*[^:]: "
		    "%*"PRIu64" "    /* syscr: number of read I/O operations */
		    "%*[^:]: "
		    "%*"PRIu64" "    /* syscw: number of write I/O operations */
		    "%*[^:]: "
		    "%"PRIu64" "     /* read_bytes */
		    "%*[^:]: "
		    "%"PRIu64" ";    /* write_bytes */

    memset(io, 0, sizeof(*io));

    snprintf(fileName, sizeof(fileName), "/proc/%i/io", pid);
    if (stat(fileName, &sbuf) == -1) return false;

    fd = fopen(fileName,"r");
    if (!fd) {
	flog("open '%s' failed\n", fileName);
	return false;
    }

    do {
	ret = fscanf(fd, io_format, &io->diskRead, &io->diskWrite,
		     &io->readBytes, &io->writeBytes);
	eno = errno;
    } while (ret == -1 && eno == EINTR);
    fclose(fd);
    if (ret != 4) {
	fwarn(eno, "fscanf() returns %d", ret);
	return false;
    }

    fdbg(PSACC_LOG_PROC, "io stat: diskRead %zu diskWrite %zu readBytes %zu"
	 " writeBytes %zu\n", io->diskRead, io->diskWrite,
	 io->readBytes, io->writeBytes);
    return true;
}

bool readProcStat(pid_t pid, ProcStat_t *pS)
{
    FILE *fd;
    char fileName[128];
    struct stat sbuf;
    int ret, eno;

    /* format string of /proc/<pid>/stat */
    static char stat_format[] =
		    "%*d "	    /* pid */
		    "(%*[^)]%*[)] " /* comm */
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

    pS->state[0] = '\0';

    snprintf(fileName, sizeof(fileName), "/proc/%i/stat", pid);
    if (stat(fileName, &sbuf) == -1) return false;

    fd = fopen(fileName,"r");
    if (!fd) {
	flog("open '%s' failed\n", fileName);
	return false;
    }

    do {
	ret = fscanf(fd, stat_format, pS->state, &pS->ppid, &pS->pgrp,
		     &pS->session, &pS->majflt, &pS->cmajflt, &pS->utime,
		     &pS->stime, &pS->cutime, &pS->cstime, &pS->threads,
		     &pS->vmem, &pS->mem, &pS->cpu);
	eno = errno;
    } while (ret == -1 && eno == EINTR);
    fclose(fd);
    if (ret != 14) {
	fwarn(eno, "fscanf(%d) returns %d", pid, ret);
	return false;
    }

    return true;
}

static bool readProcUID(pid_t pid, ProcStat_t *pS)
{
    FILE *fd;
    char fileName[128];
    struct stat sbuf;
    int ret, eno;

    snprintf(fileName, sizeof(fileName), "/proc/%i/task", pid);
    if (stat(fileName, &sbuf) == -1) return false;
    pS->uid = sbuf.st_uid;

    snprintf(fileName, sizeof(fileName), "/proc/%i/loginuid", pid);
    if (stat(fileName, &sbuf) == -1) return false;

    fd = fopen(fileName,"r");
    if (!fd) {
	flog("open '%s' failed\n", fileName);
	return false;
    }

    do {
	ret = fscanf(fd, "%u", &pS->loginUid);
	eno = errno;
    } while (ret == -1 && eno == EINTR);
    fclose(fd);
    if (ret != 1) {
	fwarn(eno, "fscanf() returns %d", ret);
	return false;
    }
    return true;
}

/**
 * @brief Add a new proc snapshot.
 *
 * @return Returns the created proc snapshot.
 */
static ProcSnapshot_t *addProc(pid_t pid, ProcStat_t *pS)
{
    ProcSnapshot_t *proc = PSitems_getItem(procPool);

    if (!proc) return NULL;
    proc->state = PROC_USED;
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
    list_t *p, *tmp;
    list_for_each_safe(p, tmp, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
	list_del(&proc->next);
	PSitems_putItem(procPool, proc);
    }
}

#define MAX_USER 300

void getSessionInfo(int *count, char *buf, size_t size, int *userCount)
{
    uid_t users[MAX_USER];

    buf[0] = '\0';
    *count = 0;
    *userCount = 0;

    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
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
		flog("MAX_USER exceeded. Truncating sessions\n");
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
    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);

	if (proc->ppid == pid) {
	    res->mem += proc->mem;
	    res->vmem += proc->vmem;
	    res->cutime += proc->cutime;
	    res->cstime += proc->cstime;

	    fdbg(PSACC_LOG_PROC, "pid:%i ppid:%i cutime:%lu cstime:%lu mem:%lu"
		 " vmem:%lu\n", proc->pid, proc->ppid, proc->cutime,
		 proc->cstime, proc->mem, proc->vmem);
	    collectDescendantData(proc->pid, res);
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

    fdbg(PSACC_LOG_PROC, "pid:%i mem:%lu vmem:%lu cutime:%lu cstime:%lu\n", pid,
	 res->mem, res->vmem, res->cutime, res->cstime);
}

void updateProcSnapshot(void)
{
    DIR *dir = opendir("/proc/");
    struct dirent *dent;
    ProcStat_t pS;
    bool ignoreRoot = getConfValueI(config, "IGNORE_ROOT_PROCESSES");

    /* clear all previous proc entrys */
    clearAllProcSnapshots();

    if (!dir) {
	flog("open /proc failed\n");
	return;
    }

    if (cpuGovEnabled) updateCpuFreq();

    rewinddir(dir);
    while ((dent = readdir(dir))) {
	pid_t pid = atoi(dent->d_name);
	if (pid <= 0) {
	    fdbg(PSACC_LOG_PROC, "pid %i too small for d_name '%s'\n", pid,
		 dent->d_name);
	    continue;
	}

	if (readProcStat(pid, &pS) && readProcUID(pid, &pS)
	    && (!ignoreRoot || pS.uid) && pS.loginUid != (uid_t)-1
	    && pS.state[0] != 'Z') {
	    addProc(pid, &pS);
	}
    }
    closedir(dir);
}

void initProcPool(void)
{
    if (PSitems_isInitialized(procPool)) return;
    procPool = PSitems_new(sizeof(ProcSnapshot_t), "procSnapshots");
}

void initProc(void)
{
    if (PSitems_isInitialized(procPool)) return;

    cpuCount = getCPUCount();
    if (cpuCount) initCpuFreq();
    initProcPool();

    PSID_registerLoopAct(proc_gc);
    PSIDhook_add(PSIDHOOK_CLEARMEM, clearMem);
}

void finalizeProc(void)
{
    clearAllProcSnapshots();
    clearCpuFreq();

    clearMem(NULL);
    PSID_unregisterLoopAct(proc_gc);
    PSIDhook_del(PSIDHOOK_CLEARMEM, clearMem);
}
