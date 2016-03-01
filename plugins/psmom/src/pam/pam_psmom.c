/*
 *               ParaStation
 *
 * Copyright (C) 2011 - 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "psmomdef.h"
#include "plugincomm.h"

/* needed for static modules but also recommended for shared modules */
#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT
#define PAM_SM_PASSWORD
#define PAM_SM_SESSION

#include <security/pam_modules.h>
#include <security/_pam_macros.h>
#include <security/pam_modutil.h>
#include <security/pam_ext.h>

#define UNUSED __attribute__ ((unused))

typedef enum {
    CMD_LOCAL_HELLO = 0,
    CMD_LOCAL_SIGNAL,
    CMD_LOCAL_DEBUG,
    CMD_LOCAL_FINISH,
    CMD_LOCAL_QSUB_OUT,
    CMD_LOCAL_CHILD_START,
    CMD_LOCAL_CHILD_EXIT,
    CMD_LOCAL_CLOSE,
    CMD_LOCAL_PAM_SSH_REQ
} LocalCommandType_t;

/** usernames which are always allowed to connect, seperated by ',' */
static char *authUsers = NULL;

static int verbose = 0;

static int quiet = 0;

static pam_handle_t *pamH = NULL;

#define elog(...)   pam_syslog(pamH, LOG_ERR, __VA_ARGS__)
#define ilog(...)   pam_syslog(pamH, LOG_INFO, __VA_ARGS__)

static void cleanup()
{
    if (authUsers) free(authUsers);
}

static int parse_modul_options(int argc, const char **argv)
{
    const char *item;
    char key[100], *value;
    int i;
    size_t len;

    for(i=0; i < argc; i++) {
	item = argv[i];
	len = strlen(item);

	/* split into value/key pair */
	if (!(value = strchr(item, '='))) {
	    elog("invalid module option: '%s'", item);
	    return PAM_SERVICE_ERR;
	}

	strncpy(key, item, sizeof(key));
	key[len - strlen(value)] = '\0';
	value++;

	if (verbose > 3) {
	    ilog("got module option key:%s value:%s", key, value);
	}

	/* handle options */
	if (!(strcmp(key, "auth_users"))) {
	    if (!(authUsers = strdup(value))) {
		return PAM_BUF_ERR;
	    }
	} else if (!(strcmp(key, "verbose"))) {
	    if ((sscanf(value, "%i", &verbose)) != 1) {
		elog("invalid verbose option: '%s' - '%s'", key, value);
	    }
	} else if (!(strcmp(key, "quiet"))) {
	    if ((sscanf(value, "%i", &quiet)) != 1) {
		elog("invalid quiet option: '%s' - '%s'", key, value);
	    }
	} else {
	    elog("ignoring unknown module option: '%s' - '%s'", key, value);
	}
    }
    return PAM_SUCCESS;
}

static int isAuthorizedUser(const char *username)
{
    char *next, *toksave;
    const char delimiter[] = ",";

    if (!username || !authUsers) return 0;

    next = strtok_r(authUsers, delimiter, &toksave);

    while (next) {
	if (!(strcmp(username, next))) return 1;
	next = strtok_r(NULL, delimiter, &toksave);
    }

    return 0;
}

static int openPsmomConnection()
{
    int sock;
    struct sockaddr_un sa;

    if (!(sock = socket(PF_UNIX, SOCK_STREAM, 0))) {
	return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, masterSocketName, sizeof(sa.sun_path));

    if ((connect(sock, (struct sockaddr*) &sa, sizeof(sa))) < 0) {
	return -1;
    }

    return sock;
}

static int doSend(int sock, char *msg, int offset, int len)
{
    int n, i;

    if (!len || !msg) return 0;

    for (n=offset, i=1; (n<len) && (i>0);) {
        i = send(sock, &msg[n], len-n, 0);
        if (i<=0) {
            switch (errno) {
            case EINTR:
                break;
            case EAGAIN:
                return n;
                break;
            default:
                {
                elog("(%s): on socket %i ", strerror(errno), sock);
            return i;
                }
            }
        } else
            n+=i;
    }
    return n;
}

static int writeToPsmom(int sock, void *buf, size_t towrite)
{
    ssize_t written = 0, len = towrite;
    int i;

    for (i=0; (written < len) && (i < UNIX_RESEND); i++) {
	if ((written = doSend(sock, buf, written, len)) < 0) break;
    }
    return written;
}

static int readFromPsmom(int sock, void *buffer, size_t len)
{
    ssize_t read;

    /* no data received from client */
    if (!(read = recv(sock, buffer, len, 0))) {
	elog("no data on psmom socket '%i'", sock);
	return -1;
    }

    /* socket error occured */
    if (read < 0) {
	if (errno == EINTR) {
	    return readFromPsmom(sock, buffer, len);
	}
	elog("error while reading from psmom : %s",
		    strerror(errno));
        return -1;
    }

    return read;
}

static int hasRunningBatchJob(const char *username, const char *rhost)
{
    char recvUser[200], recvRHost[200], cmd[20], buf[800];
    char *ptr;
    int32_t res = 0;
    int sock = 0, len;
    pid_t pid;
    PS_DataBuffer_t data = { .buf = NULL};

    if ((sock = openPsmomConnection()) == -1) {
	elog("connection to local psmom failed : %s",
		strerror(errno));
	return 0;
    }

    /* add cmd */
    snprintf(cmd, sizeof(cmd), "%+ld", (long)CMD_LOCAL_PAM_SSH_REQ);
    len = strlen(cmd);
    if ((writeToPsmom(sock, cmd, len)) != len) {
	elog("sending psmom auth request failed");
	return 0;
    }

    /* add ssh pid */
    pid = getpid();
    addPidToMsg(&pid, &data);

    /* add ssh sid */
    pid = getsid((pid_t)0);
    addPidToMsg(&pid, &data);

    /* add username */
    addStringToMsg(username, &data);

    /* add remote host */
    addStringToMsg(rhost, &data);

    if ((writeToPsmom(sock, data.buf, data.bufUsed)) != data.bufUsed) {
	elog("sending psmom auth request failed");
	return 0;
    }

    if (verbose > 2) {
	ilog("sending req(%i): '%s@%s' cmd '%s' pid '%u'", sock,
		username, rhost, cmd, getpid());
    }

    if (!(readFromPsmom(sock, buf, sizeof(buf)))) {
	close(sock);
	return 0;
    }

    ptr = buf;

    /* get result */
    getInt32FromMsgBuf(&ptr, &res);

    /* get username */
    getStringFromMsgBuf(&ptr, recvUser, sizeof(recvUser));

    if (!(!strcmp(recvUser, username))) {
	elog("received invalid reply: got user '%s' should be '%s'",
		recvUser, username);
	close(sock);
	return 0;
    }

    /* get remote host */
    getStringFromMsgBuf(&ptr, recvRHost, sizeof(recvRHost));

    if (!(!strcmp(recvRHost, rhost))) {
	elog("received invalid reply: got rhost '%s' should be '%s'",
		recvRHost, rhost);
	close(sock);
	return 0;
    }

    if (verbose > 2) {
	ilog("received reply(%i): '%s@%s' res '%u'", sock,
		recvUser, recvRHost, res);
    }

    close(sock);

    return res;
}

static int psmom_sm_open_session (int argc, const char **argv)
{
    const char *username;
    const char *rhost;
    int ret, res;

    /* parse module options */
    if ((ret = parse_modul_options(argc, argv)) != PAM_SUCCESS) {
	return ret;
    }

    /* get session infos */
    pam_get_user(pamH, &username, NULL);
    pam_get_item(pamH, PAM_RHOST, (const void **) &rhost);

    /* check if the user is in the authorized user list */
    if ((isAuthorizedUser(username))) {
	if (verbose > 1) {
	    ilog("allowing access for user '%s@%s', reason: authorized list",
		    username, rhost);
	}
	return PAM_SUCCESS;
    }

    /* ask psmom if user has a batch job running and is allowed to connect */
    if ((res = hasRunningBatchJob(username, rhost))) {
	if (verbose > 1) {
	    ilog("allowing access for user '%s@%s', reason: %s",
		    username, rhost,
		    (res == 1) ? "running batch job" : "ps admin user" );
	}
	return PAM_SUCCESS;
    }

    if (verbose > 0) {
	ilog("denying access for user '%s@%s'", username, rhost);
    }

    if (!quiet) {
	char hname[100];

	if ((gethostname(hname, sizeof(hname))) == -1) {
	    strncpy(hname, "this node", sizeof(hname));
	}

	pam_prompt(pamH, PAM_TEXT_INFO, NULL, "\npsmom: user '%s' has currently"
		    " no batch job(s) running on %s, access denied.\n",
		    username, hname);
    }

    return PAM_AUTH_ERR;
}

PAM_EXTERN int
pam_sm_setcred (pam_handle_t *pamh UNUSED, int flags UNUSED,
		int argc UNUSED, const char **argv UNUSED)
{
    return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_acct_mgmt (pam_handle_t *pamh UNUSED, int flags UNUSED,
		  int argc UNUSED, const char **argv UNUSED)
{
    return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_open_session (pam_handle_t *pamh, int flags,
		     int argc, const char **argv)
{
    int ret;

    pamH = pamh;
    ret = psmom_sm_open_session(argc, argv);

    /* free allocated memory */
    cleanup();

    return ret;
}

PAM_EXTERN int
pam_sm_close_session (pam_handle_t *pamh UNUSED, int flags UNUSED,
		  int argc UNUSED, const char **argv UNUSED)
{
    return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_chauthtok (pam_handle_t *pamh UNUSED, int flags UNUSED,
		  int argc UNUSED, const char **argv UNUSED)
{
    return PAM_IGNORE;
}
