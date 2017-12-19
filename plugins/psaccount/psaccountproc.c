/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
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

#include "psidutil.h"
#include "psidhook.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "psaccountlog.h"
#include "psaccountconfig.h"
#include "psaccountclient.h"

#include "psaccountproc.h"

/**
 * Number of proc snapshot structures allocated at once. Ensure this
 * chunk is larger than 128 kB to force it into mmap()ed memory
 */
#define PROC_CHUNK (int)((128*1024)/sizeof(ProcSnapshot_t) + 1)

/**
 * Single chunk of proc snapshot structures allocated at once within
 * incFreeList()
 */
typedef struct {
    list_t next;                      /**< Use to put into chunkList */
    ProcSnapshot_t procs[PROC_CHUNK]; /**< the proc snapshot structures */
} proc_chunk_t;

/**
 * Pool of proc snapshot structures ready to use. Initialized by @ref
 * incFreeList(). To get a buffer from this pool, use @ref getProc(),
 * to put it back into it use @ref putProc().
 */
static LIST_HEAD(procFreeList);

/** List of all known processes cached from /proc */
static LIST_HEAD(procList);

/** List of chunks of proc snapshot structures */
static LIST_HEAD(chunkList);

/** Number of  proc snapshot structures currently in use */
static unsigned int usedProcs = 0;

/** Total number of  proc snapshot structures currently available */
static unsigned int availProcs = 0;

/**
 * @brief Increase proc snapshot structures
 *
 * Increase the number of available proc snapshot structures. For that,
 * a chunk of @ref PROC_CHUNK proc snapshot structures is
 * allocated. All proc snapshot structures are appended to the list of
 * free proc snapshot structures @ref procFreeList. Additionally, the
 * chunk is registered within @ref chunkList. Chunks might be released
 * within @ref proc_gc() as soon as enough proc snapshot structures
 * are available again.
 *
 * @return On success, true is returned. Or false if allocating the
 * required memory failed. In the latter case errno is set appropriately.
 */
static bool incFreeList(void)
{
    proc_chunk_t *chunk = malloc(sizeof(*chunk));
    unsigned int i;

    if (!chunk) return false;

    list_add_tail(&chunk->next, &chunkList);

    for (i=0; i<PROC_CHUNK; i++) {
	chunk->procs[i].state = PROC_UNUSED;
	list_add_tail(&chunk->procs[i].next, &procFreeList);
    }

    availProcs += PROC_CHUNK;
    mdbg(PSACC_LOG_PROC, "%s: new total %d\n", __func__, availProcs);

    return 1;
}

static ProcSnapshot_t *getProc(void)
{
    ProcSnapshot_t *p;

    if (list_empty(&procFreeList)) {
	mdbg(PSACC_LOG_PROC, "%s: no more elements\n", __func__);
	if (!incFreeList()) {
	    mlog("%s: no memory\n", __func__);
	    return NULL;
	}
    }

    /* get list's first usable element */
    p = list_entry(procFreeList.next, ProcSnapshot_t, next);
    if (p->state != PROC_UNUSED) {
	mlog("%s: %s proc snapshot. Never be here.\n", __func__,
	     (p->state == PROC_USED) ? "USED" : "DRAINED");
	return NULL;
    }

    list_del(&p->next);

    INIT_LIST_HEAD(&p->next);
    p->state = PROC_USED;

    usedProcs++;

    return p;
}

static void putProc(ProcSnapshot_t *p)
{
    p->state = PROC_UNUSED;
    list_add_tail(&p->next, &procFreeList);

    usedProcs--;
}

/**
 * @brief Free a chunk of proc snapshot structures
 *
 * Free the chunk of proc snapshot structures @a chunk. For that, all
 * empty proc snapshot structures from this chunk are removed from @ref
 * procFreeList and marked as drained. All proc snapshot structures still
 * in use are replaced by using other free proc snapshot structure from
 * @ref procFreeList.
 *
 * Once all proc snapshot structures of the chunk are empty, the whole
 * chunk is free()ed.
 *
 * @param chunk The chunk of proc snapshot structures to free
 *
 * @return No return value
 */
static void freeChunk(proc_chunk_t *chunk)
{
    unsigned int i;

    if (!chunk) return;

    /* First round: remove proc snapshot structs from procFreeList */
    for (i=0; i<PROC_CHUNK; i++) {
	if (chunk->procs[i].state == PROC_UNUSED) {
	    list_del(&chunk->procs[i].next);
	    chunk->procs[i].state = PROC_DRAINED;
	}
    }

    /* Second round: now copy and release all used proc snapshot structs */
    for (i=0; i<PROC_CHUNK; i++) {
	ProcSnapshot_t *old = &chunk->procs[i], *new;

	if (old->state == PROC_DRAINED) continue;

	new = getProc();
	if (!new) {
	    mlog("%s: new is NULL\n", __func__);
	    return;
	}

	/* copy proc snapshot struct's content */
	new->uid = old->uid;
	new->pid = old->pid;
	new->ppid = old->ppid;
	new->pgrp = old->pgrp;
	new->session = old->session;
	new->cutime = old->cutime;
	new->cstime = old->cstime;
	new->threads = old->threads;
	new->vmem = old->vmem;
	new->mem = old->mem;
	new->majflt = old->majflt;
	new->cpu = old->cpu;

	/* tweak the list */
	__list_add(&new->next, old->next.prev, old->next.next);

	old->state = PROC_DRAINED;

	usedProcs--;
    }

    /* Now that the chunk is completely empty, free() it */
    list_del(&chunk->next);
    free(chunk);
    availProcs -= PROC_CHUNK;
    mdbg(PSACC_LOG_PROC, "%s: new total %d\n", __func__, availProcs);
}

static bool gcRequired(void)
{
    mdbg(PSACC_LOG_PROC, "%s()\n", __func__);

    return ((int)usedProcs < ((int)availProcs - PROC_CHUNK)/2);
}

static void proc_gc(void)
{
    list_t *c, *tmp;
    unsigned int i;
    bool first = true;

    mdbg(PSACC_LOG_PROC, "%s()\n", __func__);

    list_for_each_safe(c, tmp, &chunkList) {
	proc_chunk_t *chunk = list_entry(c, proc_chunk_t, next);
	int unused = 0;

	if (!gcRequired()) break;

	/* always keep the first one */
	if (first) {
	    first = false;
	    continue;
	}

	for (i=0; i<PROC_CHUNK; i++) {
	    if (chunk->procs[i].state != PROC_USED) unused++;
	}

	if (unused > PROC_CHUNK/2) freeChunk(chunk);
    }
}

static int clearMem(void *dummy)
{
    list_t *c, *tmp;

    list_for_each_safe(c, tmp, &chunkList) {
	proc_chunk_t *chunk = list_entry(c, proc_chunk_t, next);

	list_del(&chunk->next);
	free(chunk);
    }
    usedProcs = availProcs = 0;
    INIT_LIST_HEAD(&procList);
    INIT_LIST_HEAD(&procFreeList);

    return 0;
}

/* ---------------------------------------------------------------------- */

#define PROC_CPU_INFO	"/proc/cpuinfo"
#define SYS_CPU_FREQ	"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq"

static int *cpuFreq = NULL;

static int cpuCount = 0;

static bool cpuGovEnabled = false;

static int getCPUCount(void)
{
    int count = 0;
    char buf[256];
    FILE *fd = fopen(PROC_CPU_INFO, "r");

    if (!fd) {
	mlog("%s: open '%s' failed\n", __func__, PROC_CPU_INFO);
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
    char buf[256], *tmp, *sfreq;
    struct stat sbuf;
    int freq, i;
    int localcount = 0;

    cpuFreq = umalloc(sizeof(*cpuFreq) * cpuCount);

    snprintf(buf, sizeof(buf), SYS_CPU_FREQ, 0);

    if (stat(buf, &sbuf) == -1) {
	FILE *fd = fopen(PROC_CPU_INFO, "r");
	if (!fd) {
	    cpuCount = 0;
	    ufree(cpuFreq);
	    return;
	}

	while (fgets(buf, sizeof(buf), fd)) {
	  if (strncmp (buf, "processor", 8))
	    localcount++;
	  if (!strncmp(buf, "cpu MHz", 7) || !strncmp(buf, "cpu GHz", 7)) {
	    break;
	  }
	}
	
	/* did we find anything? */
	if (feof (fd)) { /* no, we didn't */
	  break;
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

	if (sscanf(sfreq, "%d", &freq) != 1) {
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
    } else { /* stat == 0, we have a scaling governor */
	cpuGovEnabled = true;
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
	    mwarn(errno, "%s: fopen(%s) failed : ", __func__, buf);
	    continue;
	}

	if (fscanf(fd, "%d", &cpuFreq[i]) != 1) {
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
    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
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
    mdbg(PSACC_LOG_SIGNAL, "%s(child %d pgroup %d sig %d)\n", __func__,
	 child, pgroup, sig);

    if (pgroup > 0) killpg(pgroup, sig);
    kill(child, sig);
}

int signalChildren(pid_t mypid, pid_t child, pid_t pgrp, int sig)
{
    int sendCount = 0;
    list_t *p;

    /* never send signal to myself */
    if (child == mypid) return 0;

    list_for_each(p, &procList) {
	ProcSnapshot_t *childProc = list_entry(p, ProcSnapshot_t, next);
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
    list_t *p;

    /* don't kill zombies */
    if (session < 1) return 0;

    /* we need up2date information */
    updateProcSnapshot();

    list_for_each(p, &procList) {
	ProcSnapshot_t *childProc = list_entry(p, ProcSnapshot_t, next);
	pid_t child = childProc->pid;

	if (childProc->session == session) {
	    if (child != mypid && child > 0) {
		sendCount += signalChildren(mypid, child, childProc->pgrp, sig);
	    }
	}
    }

    return sendCount;
}

void findDaemonProcs(uid_t uid, bool kill, bool warn)
{
    pid_t mypid = getpid();
    list_t *p;

    /* we need up2date information */
    updateProcSnapshot();

    list_for_each(p, &procList) {
	ProcSnapshot_t *childProc = list_entry(p, ProcSnapshot_t, next);

	if (childProc->uid == uid && childProc->ppid == 1) {
	    if (warn) mlog("found %sdaemon process: pid %i uid %i\n",
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
	mdbg(PSACC_LOG_PROC, "%s: child %i not found\n", __func__, child);
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
		    "%*"PRIu64" "    /* syscr */
		    "%*[^:]: "
		    "%*"PRIu64" "    /* syscw */
		    "%*[^:]: "
		    "%"PRIu64" "     /* read_bytes */
		    "%*[^:]: "
		    "%"PRIu64" ";    /* write_bytes */

    memset(io, 0, sizeof(*io));

    snprintf(fileName, sizeof(fileName), "/proc/%i/io", pid);
    if (stat(fileName, &sbuf) == -1) return false;

    fd = fopen(fileName,"r");
    if (!fd) {
	mlog("%s: open '%s' failed\n", __func__, fileName);
	return false;
    }

    do {
	ret = fscanf(fd, io_format, &io->diskRead, &io->diskWrite,
		     &io->readBytes, &io->writeBytes);
	eno = errno;
    } while (ret == -1 && eno == EINTR);
    fclose(fd);
    if (ret != 4) {
	mwarn(eno, "%s: fscanf() returns %d", __func__, ret);
	return false;
    }

    mdbg(PSACC_LOG_PROC, "%s: io stat: diskRead %zu diskWrite %zu readBytes %zu"
	 " writeBytes %zu\n", __func__, io->diskRead, io->diskWrite,
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
	mlog("%s: open '%s' failed\n", __func__, fileName);
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
	mwarn(eno, "%s: fscanf(%d) returns %d", __func__, pid, ret);
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
	mlog("%s: open '%s' failed\n", __func__, fileName);
	return false;
    }

    do {
	ret = fscanf(fd, "%u", &pS->loginUid);
	eno = errno;
    } while (ret == -1 && eno == EINTR);
    fclose(fd);
    if (ret != 1) {
	mwarn(eno, "%s: fscanf() returns %d", __func__, ret);
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
    ProcSnapshot_t *proc = getProc();

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
    list_t *p, *tmp;
    list_for_each_safe(p, tmp, &procList) {
	ProcSnapshot_t *proc = list_entry(p, ProcSnapshot_t, next);
	list_del(&proc->next);
	putProc(proc);
    }
}

#define MAX_USER 300

void getSessionInfo(int *count, char *buf, size_t size, int *userCount)
{
    uid_t users[MAX_USER];
    list_t *p;

    buf[0] = '\0';
    *count = 0;
    *userCount = 0;

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
    list_t *p;
    list_for_each(p, &procList) {
	ProcSnapshot_t *childProc = list_entry(p, ProcSnapshot_t, next);

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
    DIR *dir = opendir("/proc/");
    struct dirent *dent;
    ProcStat_t pS;
    bool ignoreRoot = getConfValueI(&config, "IGNORE_ROOT_PROCESSES");

    /* clear all previous proc entrys */
    clearAllProcSnapshots();

    if (!dir) {
	mlog("%s: open /proc failed\n", __func__);
	return;
    }

    if (cpuGovEnabled) updateCpuFreq();

    rewinddir(dir);
    while ((dent = readdir(dir))) {
	pid_t pid = atoi(dent->d_name);
	if (pid <= 0) {
	    mdbg(PSACC_LOG_PROC, "%s: pid %i too small for d_name '%s'\n",
		 __func__, pid, dent->d_name);
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

void initProc(void)
{
    cpuCount = getCPUCount();
    if (cpuCount) initCpuFreq();
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
