/*
 * ParaStation
 *
 * Copyright (C) 2010 - 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
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

#include "pluginmalloc.h"
#include "psaccountlog.h"
#include "psaccountlog.h"
#include "psaccountclient.h"

#include "psaccountproc.h"

#define PROC_CPU_INFO	"/proc/cpuinfo"
#define SYS_CPU_FREQ	"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq"

typedef enum {
    INFO_MEM,
    INFO_VMEM,
} ProcInfoTypes;

int *cpuFreq = NULL;

int cpuCount = 0;

static int cpuGovEnabled = 0;

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

    cpuFreq = umalloc(sizeof(int) * cpuCount);

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
	cpuGovEnabled = 0;
    } else {
	cpuGovEnabled = 1;
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

void clearCpuFreq()
{
    if (cpuCount) {
	ufree(cpuFreq);
    }
}

void initProc()
{
    INIT_LIST_HEAD(&ProcList.list);
    INIT_LIST_HEAD(&SessionList.list);

    if ((cpuCount = getCPUCount()) > 0) {
	initCpuFreq();
    }
}

Proc_Snapshot_t *findProcSnapshot(pid_t pid)
{
    struct list_head *pos;
    Proc_Snapshot_t *proc;

    list_for_each(pos, &ProcList.list) {
	if (!(proc = list_entry(pos, Proc_Snapshot_t, list))) break;
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

int sendSignal2AllChildren(pid_t mypid, pid_t child, pid_t pgroup, int sig)
{
    list_t *pos, *tmp;
    Proc_Snapshot_t *Childproc;
    int sendCount = 0;

    /* never send signal to myself */
    if (child == mypid) return 0;

    if (list_empty(&ProcList.list)) {
	doKill(child, pgroup, sig);
        return 1;
    }

    list_for_each_safe(pos, tmp, &ProcList.list) {
        if (!(Childproc = list_entry(pos, Proc_Snapshot_t, list))) break;

	if (pgroup > 0) {
	    if (Childproc->ppid == child) {
		sendCount += sendSignal2AllChildren(mypid, Childproc->pid,
							Childproc->pgroup, sig);
	    }
	} else {
	    if (Childproc->ppid == child) {
		sendCount += sendSignal2AllChildren(mypid, Childproc->pid,
							pgroup, sig);
	    }
	}
    }
    doKill(child, pgroup, sig);
    sendCount++;

    return sendCount;
}

int sendSignal2Session(pid_t session, int sig)
{
    struct list_head *pos;
    Proc_Snapshot_t *Childproc;
    pid_t mypid = getpid();
    pid_t child;
    int sendCount = 0;

    /* don't kill zombies */
    if (session < 1) return 0;

    /* we need up2date information */
    updateProcSnapshot(0);

    if (!list_empty(&ProcList.list)) {
	list_for_each(pos, &ProcList.list) {
	    if (!(Childproc = list_entry(pos, Proc_Snapshot_t, list))) {
		return 0;
	    }

	    child = Childproc->pid;
	    if (Childproc->session == session) {
		if (child != mypid && child > 0) {
		    /*
		    mlog("%s: send signal '%i' to pid '%i' pgroup '%i' "
			"sid '%i' and all its children\n",
			__func__, sig, child, Childproc->pgroup,
			Childproc->session);
		    */
		    sendCount += sendSignal2AllChildren(mypid, child,
							Childproc->pgroup, sig);
		}
	    }
	}
    }

    return sendCount;
}

void findDaemonProcesses(uid_t userId, int kill, int warn)
{
    struct list_head *pos;
    Proc_Snapshot_t *Childproc;
    pid_t mypid = getpid();
    char killMsg[] = "and killing ";

    /* we need up2date information */
    updateProcSnapshot(0);

    if (!list_empty(&ProcList.list)) {
	list_for_each(pos, &ProcList.list) {
	    if ((Childproc = list_entry(pos, Proc_Snapshot_t, list)) == NULL) {
		return;
	    }

	    if (Childproc->uid == userId && Childproc->ppid == 1) {
		if (warn) {
		    mlog("found %sdaemon process: pid '%i' uid '%i'\n",
			    kill ? killMsg : "", Childproc->pid, userId);
		}
		if (kill) {
		    sendSignal2AllChildren(mypid, Childproc->pid,
					    Childproc->pgroup, SIGKILL);
		}
	    }
	}
    }
}

static int isChildofParentSnap(pid_t parent, pid_t child)
{
    Proc_Snapshot_t *procChild;

    if (child == 0) return 0;

    if (!(procChild = findProcSnapshot(child))) {
	//mlog("%s: child %i  not found in snapshot\n", __func__, child);
	return 0;
    }

    if (procChild->ppid == parent) {
	return 1;
    }
    //mlog("%s: child(%i)->parent %i not parent %i\n", __func__,
    //		procChild->pid, procChild->ppid, parent);
    return isChildofParent(parent, procChild->ppid);
}

int readProcIO(pid_t pid, ProcIO_t *io)
{
    FILE *fd;
    char buf[200];
    struct stat sbuf;
    int ret;

    memset(io, 0, sizeof(ProcIO_t));

    /* format string of /proc/pid/io */
    static char io_format[] =
		    "%*[^:]: "
		    "%"PRIu64" "     /* rchar */
		    "%*[^:]: "
		    "%"PRIu64" "     /* wchar */
		    "%*[^:]: "
		    "%*"PRIu64" "    /* syscr */
		    "%*[^:]: "
		    "%*"PRIu64" "    /* syscw */
		    "%*[^:]: "
		    "%"PRIu64" "     /* read_bytes */
		    "%*[^:]: "
		    "%"PRIu64" ";    /* write_bytes */

    snprintf(buf, sizeof(buf), "/proc/%i/io", pid);

    if ((stat(buf, &sbuf)) == -1) return 0;

    if (!(fd = fopen(buf,"r"))) {
	mlog("%s: open '%s' failed\n", __func__, buf);
	return 0;
    }

    if ((ret = fscanf(fd, io_format, &io->rChar, &io->wChar, &io->readBytes,
			&io->writeBytes)) != 4) {
	fclose(fd);
	return 0;
    }

    /*
    mlog("%s: io stat: rChar '%zu' wChar '%zu' read_byes '%zu' writeBytes "
	    "'%zu'\n", __func__, io->rChar, io->wChar, io->readBytes,
	    io->writeBytes);
    */
    fclose(fd);
    return 1;
}

int readProcStatInfo(pid_t pid, ProcStat_t *pS)
{
    FILE *fd;
    char buf[200];
    struct stat sbuf;

    /* format string of /proc/pid/stat */
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

    if ((stat(buf, &sbuf)) == -1) return 0;
    pS->uid = sbuf.st_uid;

    if (!(fd = fopen(buf,"r"))) {
	mlog("%s: open '%s' failed\n", __func__, buf);
	return 0;
    }

    pS->state[0] = '\0';
    if ((fscanf(fd, stat_format, pS->state, &pS->ppid, &pS->pgroup,
		&pS->session, &pS->majflt, &pS->cmajflt, &pS->utime,
		&pS->stime, &pS->cutime, &pS->cstime, &pS->threads,
		&pS->vmem, &pS->mem, &pS->cpu)) != 14) {

	fclose(fd);
	return 0;
    }

    fclose(fd);
    return 1;
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
static int isChildofParentProc(pid_t parent, pid_t child)
{
    ProcStat_t pS;

    if (child <= 1) return 0;

    if (!(readProcStatInfo(child, &pS))) {
	return 0;
    }

    if (pS.ppid == parent) return 1;

    return isChildofParentProc(parent, pS.ppid);
}

int isChildofParent(pid_t parent, pid_t child)
{
    if (!list_empty(&ProcList.list)) {
	return isChildofParentSnap(parent, child);
    }

    return isChildofParentProc(parent,child);
}

/**
 * @brief Add a new proc snapshot.
 *
 * @return Returns the created proc snapshot.
 */
static Proc_Snapshot_t *addProc(pid_t pid, ProcStat_t *pS, char *cmdline)
{
    Proc_Snapshot_t *proc;

    proc = (Proc_Snapshot_t *) umalloc(sizeof(Proc_Snapshot_t));
    proc->uid = pS->uid;
    proc->pid = pid;
    proc->ppid = pS->ppid;
    proc->pgroup = pS->pgroup;
    proc->session = pS->session;
    proc->cutime = pS->utime + pS->cutime;
    proc->cstime = pS->stime + pS->cstime;
    proc->majflt = pS->majflt + pS->cmajflt;
    proc->threads = pS->threads;
    proc->mem = pS->mem;
    proc->vmem = pS->vmem;
    proc->cmdline = (cmdline) ? ustrdup(cmdline) : NULL;
    proc->cpu = pS->cpu;

    list_add_tail(&(proc->list), &ProcList.list);
    return proc;
}

/**
 * @brief Find a session info with the sid.
 *
 * @param session The sid of the session info to find.
 *
 * @return On success the found session info is returned,
 * otherwise NULL is returned.
 */
static Session_Info_t *findSession(pid_t session)
{
    struct list_head *pos;
    Session_Info_t *info;

    list_for_each(pos, &SessionList.list) {
	if (!(info = list_entry(pos, Session_Info_t, list))) break;
	if (info->session == session) return info;
    }
    return NULL;
}

/**
 * @brief Add a session info.
 *
 * Add a new session info. If a info with the same session ID
 * is found then no new session will be added.
 *
 * @return No return value.
 */
static void addSession(pid_t session, uid_t uid)
{
    Session_Info_t *info;

    if (uid == 0) return;
    if ((findSession(session)) != NULL) {
	return;
    }

    info = (Session_Info_t *) umalloc(sizeof(Session_Info_t));
    info->session = session;
    info->uid = uid;

    list_add_tail(&(info->list), &SessionList.list);
}

/**
 * @brief Delete all session infos.
 *
 * @return No return value.
 */
static void clearSessions()
{
    list_t *pos, *tmp;
    Session_Info_t *info;

    list_for_each_safe(pos, tmp, &SessionList.list) {
	if (!(info = list_entry(pos, Session_Info_t, list))) break;
	list_del(&info->list);
	ufree(info);
    }
}

void clearAllProcSnapshots()
{
    list_t *pos, *tmp;
    Proc_Snapshot_t *proc;

    clearSessions();

    list_for_each_safe(pos, tmp, &ProcList.list) {
	if (!(proc = list_entry(pos, Proc_Snapshot_t, list))) break;
	if (proc->cmdline) ufree(proc->cmdline);

	list_del(&proc->list);
	ufree(proc);
    }
}

void getSessionInformation(int *count, char *buf, size_t bufsize,
			    int *userCount)
{
    #define MAX_USER	300
    struct list_head *pos;
    Session_Info_t *info;
    char strSession[50];
    int i, ucount, ufound;
    uid_t users[MAX_USER];

    buf[0] = '\0';
    *count = 0;
    *userCount = ucount = 0;
    if (list_empty(&SessionList.list)) return;

    for (i=0; i<MAX_USER; i++) {
	users[i] = 0;
    }

    list_for_each(pos, &SessionList.list) {
	if (!(info = list_entry(pos, Session_Info_t, list))) return;

	if (info->uid == 0 || info->session == 0) continue;
	*count += 1;
	snprintf(strSession, sizeof(strSession), "%i ", info->session);
	strncat(buf, strSession, bufsize - strlen(buf));

	ufound = 0;
	for (i=0; i<MAX_USER; i++) {
	    if (users[i] == info->uid) {
		ufound = 1;
		break;
	    }
	}
	if (!ufound) users[ucount++] = info->uid;
    }
    buf[bufsize] = '\0';
    if (strlen(buf) > 0) buf[strlen(buf) - 1] = '\0';
    *userCount = ucount;
}

/**
 * @brief Get all memory for a proc snapshot.
 *
 * @proc The proc snapshot to calculate the information for.
 *
 * @flag_vmem If set to 1 the virtual memory is calulated, otherwise the
 * pyhsical memory is calculated.
 *
 * @return Returns the calculated memory information.
 */
static void getAllClientInfo(Proc_Snapshot_t *res, pid_t pid)
{
    struct list_head *pos;
    Proc_Snapshot_t *Childproc;

    list_for_each(pos, &ProcList.list) {
	if (!(Childproc = list_entry(pos, Proc_Snapshot_t, list))) return;

	if (Childproc->ppid == pid) {
	    res->mem += Childproc->mem;
	    res->vmem += Childproc->vmem;
	    res->cutime += Childproc->cutime;
	    res->cstime += Childproc->cstime;

	    mdbg(PSACC_LOG_PROC, "%s: cmd:%s pid:%i ppid:%i cutime:%lu "
		 "cstime:%lu mem:%lu vmem:%lu\n", __func__, Childproc->cmdline,
		 Childproc->pid, Childproc->ppid,
		 Childproc->cutime, Childproc->cstime,
		 Childproc->mem, Childproc->vmem);
	    getAllClientInfo(res, Childproc->pid);
	}
    }
}

Proc_Snapshot_t *getAllChildrenData(pid_t pid)
{
    Proc_Snapshot_t *proc;

    proc = (Proc_Snapshot_t *) umalloc(sizeof(Proc_Snapshot_t));
    proc->pid = pid;
    proc->ppid = 0;
    proc->session = 0;
    proc->mem = 0;
    proc->vmem = 0;
    proc->cutime = 0;
    proc->cstime = 0;
    proc->majflt = 0;
    proc->cpu = 0;

    getAllClientInfo(proc, pid);

    mdbg(PSACC_LOG_PROC, "%s: pid:%i mem:%lu vmem:%lu cutime:%lu cstime:%lu\n",
	    __func__, pid, proc->mem, proc->vmem, proc->cutime, proc->cstime);
    return proc;
}

void updateProcSnapshot(int extended)
{
    FILE *fd;
    DIR *dir;
    struct dirent *dent;
    pid_t pid = -1;
    char buf[201];
    ProcStat_t pS;
    int blocked = 0;

    /* clear all previous proc entrys */
    clearAllProcSnapshots();

    blocked = blockSigChild(1);
    if (!(dir = opendir("/proc/"))) {
	mlog("%s: open /proc failed\n", __func__);
	return;
    }
    if (blocked) blockSigChild(0);

    if (cpuGovEnabled) {
	updateCpuFreq();
    }

    rewinddir(dir);
    while ((dent = readdir(dir)) != NULL) {
	if ((pid = atoi(dent->d_name)) <= 0) {
	    mdbg(PSACC_LOG_PROC, "%s: pid '%i' too small for d_name '%s'\n",
		__func__, pid, dent->d_name);
	    continue;
	}

	if (!(readProcStatInfo(pid, &pS))) continue;

	/*
	mlog("pid '%i' state:%s ppid '%i' pgroup: %i session '%i' cutime: '%lu'"
		"cstime: '%lu' threads '%lu' vmem: '%lu' mem: '%lu'\n", pid,
		state, ppid, pgroup, session, cutime, cstime, threads, vmem,
		mem);
	*/

	if (extended) {
	    snprintf(buf, sizeof(buf), "/proc/%i/cmdline", pid);
	    if ((fd = fopen(buf,"r")) == NULL) {
		mlog("%s: open '%s' failed\n", __func__, buf);
		continue;
	    }
	    if ((fscanf(fd, "%200s", buf)) != 1) {
		snprintf(buf, sizeof(buf), "cmd not available");
	    }
	    fclose(fd);
	    addProc(pid, &pS, buf);
	    addSession(pS.session, pS.uid);
	    mdbg(PSACC_LOG_PROC, "%s: pid:%i ppid:%i session:%i, threads:%lu "
		"mem:%lu vmem:%lu cmd:%s\n", __func__, pid, pS.ppid, pS.session,
		pS.threads, pS.mem, pS.vmem, buf);
	    continue;
	}
	addProc(pid, &pS, NULL);
	addSession(pS.session, pS.uid);
    }
    closedir(dir);
}
