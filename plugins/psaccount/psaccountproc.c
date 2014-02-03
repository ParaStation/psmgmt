/*
 * ParaStation
 *
 * Copyright (C) 2010-2012 ParTec Cluster Competence Center GmbH, Munich
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

#include "pluginmalloc.h"
#include "psaccountlog.h"
#include "psaccountlog.h"
#include "psaccountclient.h"

#include "psaccountproc.h"

typedef enum {
    INFO_MEM,
    INFO_VMEM,
} ProcInfoTypes;

void initProcList()
{
    INIT_LIST_HEAD(&ProcList.list);
    INIT_LIST_HEAD(&SessionList.list);
}

Proc_Snapshot_t *findProcSnapshot(pid_t pid)
{
    struct list_head *pos;
    Proc_Snapshot_t *proc;

    if (list_empty(&ProcList.list)) return NULL;

    list_for_each(pos, &ProcList.list) {
	if ((proc = list_entry(pos, Proc_Snapshot_t, list)) == NULL) {
	    return NULL;
	}
	if (proc->pid == pid) {
	    return proc;
	}
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
    struct list_head *pos;
    Proc_Snapshot_t *Childproc;
    int sendCount = 0;

    /* never send signal to myself */
    if (child == mypid) return 0;

    if (list_empty(&ProcList.list)) {
	doKill(child, pgroup, sig);
        return 1;
    }

    list_for_each(pos, &ProcList.list) {
        if ((Childproc = list_entry(pos, Proc_Snapshot_t, list)) == NULL) break;

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
	    if ((Childproc = list_entry(pos, Proc_Snapshot_t, list)) == NULL) {
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

int readProcStatInfo(pid_t pid, ProcStat_t *pS)
{
    // 14175 (vi) T 25119 14175 25119 34816 15418
    /** Format string of /proc/pid/stat */
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
		    "%*lu %*lu "    /* majflt cmajflt */
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
		    "%*d "	    /* processor  (kernel 2.2.8) */
		    "%*lu "	    /* rt_priority (kernel 2.5.19) */
		    "%*lu "	    /* policy (kernel 2.5.19) */
		    "%*llu";	    /* delayacct_blkio_ticks (kernel 2.6.18) */

    FILE *fd;
    char buf[200];
    struct stat sbuf;
    int res;

    snprintf(buf, sizeof(buf), "/proc/%i/stat", pid);

    if ((stat(buf, &sbuf)) == -1) {
	return 0;
    }
    pS->uid = sbuf.st_uid;

    if ((fd = fopen(buf,"r")) == NULL) {
	mlog("%s: open '%s' failed\n", __func__, buf);
	return 0;
    }

    pS->state[0] = '\0';
    if ((res = fscanf(fd, stat_format, pS->state, &pS->ppid, &pS->pgroup,
			&pS->session, &pS->ctime, &pS->stime, &pS->cutime,
			&pS->cstime, &pS->threads, &pS->vmem, &pS->mem)) != 11) {
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
    proc->cutime = pS->cutime;
    proc->cstime = pS->cstime;
    proc->threads = pS->threads;
    proc->mem = pS->mem;
    proc->vmem = pS->vmem;
    if (cmdline)  {
	proc->cmdline = ustrdup(cmdline);
    } else {
	proc->cmdline = NULL;
    }

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

    if (list_empty(&SessionList.list)) return NULL;

    list_for_each(pos, &SessionList.list) {
	if ((info = list_entry(pos, Session_Info_t, list)) == NULL) {
	    return NULL;
	}
	if (info->session == session) {
	    return info;
	}
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

    if (list_empty(&SessionList.list)) return;

    list_for_each_safe(pos, tmp, &SessionList.list) {
	if ((info = list_entry(pos, Session_Info_t, list)) == NULL) continue;
	list_del(&info->list);
	ufree(info);
    }
}

void clearAllProcSnapshots()
{
    list_t *pos, *tmp;
    Proc_Snapshot_t *proc;

    clearSessions();
    if (list_empty(&ProcList.list)) return;

    list_for_each_safe(pos, tmp, &ProcList.list) {
	if ((proc = list_entry(pos, Proc_Snapshot_t, list)) == NULL) continue;
	if (proc->cmdline) {
	    ufree(proc->cmdline);
	}

	list_del(&proc->list);
	ufree(proc);
    }
}

void getSessionInformation(int *count, char *buf, size_t bufsize, int *userCount)
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
	if ((info = list_entry(pos, Session_Info_t, list)) == NULL) {
	    return;
	}
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

    if (list_empty(&ProcList.list)) return;

    list_for_each(pos, &ProcList.list) {
	if (!(Childproc = list_entry(pos, Proc_Snapshot_t, list))) return;

	if (Childproc->ppid == pid) {
	    res->mem += Childproc->mem;
	    res->vmem += Childproc->vmem;
	    res->cutime += Childproc->cutime;
	    res->cstime += Childproc->cstime;

	    mdbg(LOG_PROC_DEBUG, "%s: cmd:%s pid:%i ppid:%i cutime:%lu "
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

    getAllClientInfo(proc, pid);

    mdbg(LOG_PROC_DEBUG, "%s: pid:%i mem:%lu vmem:%lu cutime:%lu cstime:%lu\n",
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

    /* clear all previous proc entrys */
    clearAllProcSnapshots();

    if (!(dir = opendir("/proc/"))) {
	mlog("%s: open /proc failed\n", __func__);
	return;
    }

    rewinddir(dir);
    while ((dent = readdir(dir)) != NULL){
	if ((pid = atoi(dent->d_name)) <= 0) {
	    mdbg(LOG_PROC_DEBUG, "%s: pid '%i' too small for d_name '%s'\n",
		__func__, pid, dent->d_name);
	    continue;
	}

	if (!(readProcStatInfo(pid, &pS))) {
	    continue;
	}

	pS.cutime += pS.ctime;
	pS.cstime += pS.stime;

	/*
	mlog("pid '%i' state:%s ppid '%i' pgroup: %i session '%i' cutime: '%lu'"
		"cstime: '%lu' threads '%lu' vmem: '%lu' mem: '%lu'\n", pid, state,
		ppid, pgroup, session, cutime, cstime, threads, vmem, mem);
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
	    mdbg(LOG_PROC_DEBUG, "%s: pid:%i ppid:%i session:%i, threads:%lu "
		"mem:%lu vmem:%lu cmd:%s\n", __func__, pid, pS.ppid, pS.session,
		pS.threads, pS.mem, pS.vmem, buf);
	    continue;
	}
	addProc(pid, &pS, NULL);
	addSession(pS.session, pS.uid);
    }
    closedir(dir);
}
