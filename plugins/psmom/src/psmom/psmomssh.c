/*
 * ParaStation
 *
 * Copyright (C) 2011-2013 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "pluginmalloc.h"
#include "psmompsaccfunc.h"
#include "psmomjob.h"
#include "psmomlog.h"
#include "psmomkvs.h"

#include "psmomssh.h"


void initSSHList()
{
    INIT_LIST_HEAD(&SSHList.list);
}

SSHSession_t *addSSHSession(char *user, char *rhost, pid_t sshPid, pid_t sshSid)
{
    SSHSession_t *ssh;

    ssh = (SSHSession_t *) umalloc(sizeof(SSHSession_t));
    ssh->user = ustrdup(user);
    ssh->rhost = ustrdup(rhost);
    ssh->pid = sshPid;
    ssh->sid = sshSid;
    ssh->start_time = time(NULL);

    stat_SSHLogins++;

    list_add_tail(&(ssh->list), &SSHList.list);

    return ssh;
}

SSHSession_t *findSSHSession(char *user)
{
    list_t *pos, *tmp;
    SSHSession_t *ssh;

    if (list_empty(&SSHList.list)) return NULL;

    list_for_each_safe(pos, tmp, &SSHList.list) {
	if ((ssh = list_entry(pos, SSHSession_t, list)) == NULL) {
	    return NULL;
	}
	if (user) {
	    if (!strcmp(ssh->user, user)) {
		return ssh;
	    }
	}
    }
    return NULL;
}

void delSSHSessions(char *user)
{
    SSHSession_t *ssh;

    while ((ssh = findSSHSession(user))) {

	/* Kill all SSH processes. We can't kill the complete session or pgroup,
	 * since on CENTOS the SSH forwarders will have the same one as the root
	 * SSH process (#1730)
	 *
	 * not always working with Suse (#1795)
	 *
	psAccountSignalAllChildren(getpid(), ssh->pid, -1, SIGTERM);
	psAccountSignalAllChildren(getpid(), ssh->pid, -1, SIGKILL);
	*/

	/* kill ssh session */
	psAccountsendSignal2Session(ssh->sid, SIGTERM);
	psAccountsendSignal2Session(ssh->sid, SIGKILL);


	if (ssh->user) ufree(ssh->user);
	if (ssh->rhost) ufree(ssh->rhost);

	list_del(&ssh->list);
	ufree(ssh);
    }
}

void clearSSHList()
{
    list_t *pos, *tmp;
    SSHSession_t *ssh;

    if (list_empty(&SSHList.list)) return;

    list_for_each_safe(pos, tmp, &SSHList.list) {
	if ((ssh = list_entry(pos, SSHSession_t, list)) == NULL) {
	    return;
	}
	delSSHSessions(ssh->user);
    }
    return;
}

SSHSession_t *findSSHSessionforPID(pid_t pid)
{
    list_t *pos, *tmp;
    SSHSession_t *ssh;

    if (list_empty(&SSHList.list)) return NULL;

    list_for_each_safe(pos, tmp, &SSHList.list) {
	if ((ssh = list_entry(pos, SSHSession_t, list)) == NULL) return NULL;

	/* checking for pgroup or session does not make sense for SSH logins */
	if ((ssh->pid == pid || psAccountisChildofParent(ssh->pid, pid))) {
	    return ssh;
	}
    }

    return NULL;
}
