/*
 *               ParaStation
 *
 * Copyright (C) 2011-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "pspamcommon.h"

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

/** comma-sep. list of users always allowed to connect; set via PAM options*/
static char *authUsers = NULL;

/** verbosity level of this module; set via PAM options*/
static int verbose = 0;

/** flag to suppress ; set via PAM options*/
static int quiet = 0;

/** handle to various PAM functionality */
static pam_handle_t *pamH = NULL;

#define elog(...) pam_syslog(pamH, LOG_ERR, __VA_ARGS__)
#define ilog(...) pam_syslog(pamH, LOG_INFO, __VA_ARGS__)

/**
 * @brief Parse PAM module's options
 *
 * Parse @a argc options stored in the array of strings @a argv. Each
 * string is expected to be of the form <key>=<value>.
 *
 * @param argc Number of options in @a argv
 *
 * @param argv Array of strings holding the options in a <key>=<value>
 * format.
 *
 * @return On success PAM_SUCCESS is returned. Or PAM_SERVICE_ERR or
 * PAM_BUF_ERR in case of an error.
 */
static int parseModuleOptions(int argc, const char **argv)
{
    int i;
    for(i=0; i < argc; i++) {
	const char *value = strchr(argv[i], '=');
	char *key;

	/* split into value/key pair */
	if (!value) {
	    elog("invalid module option: '%s'", argv[i]);
	    return PAM_SERVICE_ERR;
	}
	key = strndup(argv[i], value - argv[i]);
	if (!key) {
	    elog("insufficient memory for module option: '%s'", argv[i]);
	    return PAM_SERVICE_ERR;
	}
	value++;

	if (verbose > 3) {
	    ilog("got module option key '%s' value '%s'", key, value);
	}

	/* handle options */
	if (!strcmp(key, "auth_users")) {
	    authUsers = strdup(value);
	    if (!authUsers) {
		free(key);
		return PAM_BUF_ERR;
	    }
	} else if (!strcmp(key, "verbose")) {
	    if (sscanf(value, "%i", &verbose) != 1) {
		elog("invalid verbose option: '%s'", value);
	    }
	} else if (!strcmp(key, "quiet")) {
	    if (sscanf(value, "%i", &quiet) != 1) {
		elog("invalid quiet option: '%s'", value);
	    }
	} else {
	    elog("ignore unknown module option: '%s' - '%s'", key, value);
	}
	free(key);
    }
    return PAM_SUCCESS;
}

static bool isAuthorizedUser(const char *username)
{
    char *next, *toksave;
    const char delimiter[] = ",";

    if (!username || !authUsers) return false;

    next = strtok_r(authUsers, delimiter, &toksave);

    while (next) {
	if (!strcmp(username, next)) return true;
	next = strtok_r(NULL, delimiter, &toksave);
    }

    return false;
}

/**
 * @brief Create a connection
 *
 * Create a connection to the UNIX socket @a sockname and return the
 * created socket.
 *
 * @param sockname Name of the UNIX socket to get connected to
 *
 * @return The connected socket or -1 in case of an error
 */
static int openConnection(char *sockname)
{
    struct sockaddr_un sa;
    int sock = socket(PF_UNIX, SOCK_STREAM, 0);

    if (!sock) return -1;

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (sockname[0] == '\0') {
	sa.sun_path[0] = '\0';
	strncpy(sa.sun_path+1, sockname+1, sizeof(sa.sun_path)-1);
    } else {
	strncpy(sa.sun_path, sockname, sizeof(sa.sun_path));
    }

    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
	close(sock);
	return -1;
    }

    return sock;
}

/**
 * @brief Send data to pspam plugin
 *
 * Write up to @a toWrite bytes from the buffer @a buf to the socket
 * @a sock connecting to the pspam plugin.
 *
 * @param sock Socket connected to the pspam plugin
 *
 * @param buf Buffer holding the data to send
 *
 * @param toWrite Number of bytes to write
 *
 * @return Number of bytes written or -1 on error
 */
static int writeToPspam(int sock, void *buf, size_t toWrite)
{
    int ret = doWriteP(sock, buf, toWrite);

    if (ret < 0) elog("%s(%d): %s", __func__, sock, strerror(errno));
    return ret;
}

/**
 * @brief Read data from pspam plugin
 *
 * Read data from the socket @a sock connecting to the pspam plugin
 * and store the data to the buffer @a buf of size @a len.
 *
 * @param sock Socket connected to the pspam plugin
 *
 * @param buf Buffer holding the data upon return
 *
 * @param toWrite Size of the buffer @a buf
 *
 * @return On success, i.e. if the whole message was read, true is
 * returned. Otherwise false is returned.
 */
static bool readFromPspam(int sock, void *buf, size_t len)
{
    ssize_t read = doRead(sock, buf, len);
    if (read != (ssize_t)len) {
	if (read < 0) {
	    elog("error while doRead(buf): %s", strerror(errno));
	} else {
	    elog("insufficient data (%zi/%zi)", read, len);
	}
	return false;
    }

    return true;
}

/**
 * @brief Check for allowance to access the node at pspam plugin
 *
 * Check for the allowance of the user @a user coming from the remote
 * host @a rhost to access the local node. For this the local pspam
 * plugin will be accesses in order figure out if the user currently
 * has a running batch-job on the node or is marked as an adminuser.
 *
 * Access might be denied (i.e. PSPAM_RES_DENY is returned) if the
 * communication to the pspam plugin fails or some problem in the
 * answer is detected. Otherwise pspam plugin's decision is returned.
 *
 * @param uName Username of the user to check
 *
 * @param rhost Hostname the user is coming from
 *
 * @return pspam plugin's decision is returned unless the
 * communication to the plugin failed. In the latter case
 * PSPAM_RES_DENY is returned (which might be a proper answer of the
 * pspam plugin, too).
 */
static PSPAMResult_t checkPsPamAllowance(const char *uName, const char *rhost)
{
    int sock = openConnection(pspamSocketName);
    PSPAMResult_t res;
    PS_DataBuffer_t data = { .buf = NULL};
    int written;

    if (sock == -1) {
	elog("connection to local plugin failed: %s", strerror(errno));
	return PSPAM_RES_DENY;
    }

    /* add length placeholder */
    addInt32ToMsg(0, &data);
    /* add command */
    addInt32ToMsg(PSPAM_CMD_SESS_OPEN, &data);
    /* add ssh pid */
    addPidToMsg(getpid(), &data);
    /* add ssh sid */
    addPidToMsg(getsid(0), &data);
    /* add username */
    addStringToMsg(uName, &data);
    /* add remote host */
    addStringToMsg(rhost, &data);

    /* add correct msg len (without length) at placeholder */
    *(int32_t *)data.buf = data.bufUsed - sizeof(int32_t);

    written = writeToPspam(sock, data.buf, data.bufUsed);
    free(data.buf);
    if (written != (int)data.bufUsed) {
	elog("sending pspam auth request failed");
	return PSPAM_RES_DENY;
    }

    if (verbose > 2) {
	ilog("sending req(%i): %s@%s pid %u", sock, uName, rhost, getpid());
    }

    if (!readFromPspam(sock, &res, sizeof(res))) {
	close(sock);
	return PSPAM_RES_DENY;
    }
    close(sock);

    if (verbose > 2) {
	ilog("reply(%i): %s@%s res %u", sock, uName, rhost, res);
    }
    return res;
}

/**
 * @brief Check for allowance to access the node
 *
 * Check for the allowance of the user @a user coming from the remote
 * host @a rhost to access the local node. Allowance is granted either
 * via a white-list of usernames passed as an option to the PAM module
 * or by the local pspam plugin which might be contacted.
 *
 * @param uName Username of the user to check
 *
 * @param rhost Hostname the user is coming from
 *
 * @return PAM_SUCCESS is returned in order to grant access. Otherwise
 * PAM_AUTH_ERR is returned.
 */
static int checkAllowance(const char *uName, const char *rhost)
{
    PSPAMResult_t res;

    /* check if the user is in the authorized user list */
    if (isAuthorizedUser(uName)) {
	if (verbose > 1) {
	    ilog("grant access to %s@%s (authorized list)", uName, rhost);
	}
	return PAM_SUCCESS;
    }

    /* ask pspam if user has a batch job running and is allowed to connect */
    res = checkPsPamAllowance(uName, rhost);

    switch (res) {
    case PSPAM_RES_BATCH:
    case PSPAM_RES_ADMIN_USER:
	if (verbose > 1) {
	    ilog("grant access to %s@%s (%s)", uName, rhost,
		 (res == PSPAM_RES_BATCH) ? "batch job" : "ps admin user");
	}
	return PAM_SUCCESS;
    case PSPAM_RES_DENY:
    case PSPAM_RES_PROLOG:
	if (verbose > 0) {
	    ilog("deny access to %s@%s (%s)", uName, rhost,
		 (res == PSPAM_RES_PROLOG) ? "prologue" : "no job");
	}
    }

    if (!quiet) {
	char hName[HOSTNAME_LEN];

	if (gethostname(hName, sizeof(hName)) == -1) {
	    strncpy(hName, "this node", sizeof(hName));
	}

	switch (res) {
	case PSPAM_RES_DENY:
	    pam_prompt(pamH, PAM_TEXT_INFO, NULL, "\npspam: user '%s' without"
		       " running jobs on %s, access denied.\n",	uName, hName);
	    break;
	case PSPAM_RES_PROLOG:
	    pam_prompt(pamH, PAM_TEXT_INFO, NULL, "\npspam: prologue running on"
		       " %s, access denied.\n", hName);
	    break;
	default:
	    pam_prompt(pamH, PAM_TEXT_INFO, NULL, "\npspam: access denied.\n");
	}
    }

    return PAM_AUTH_ERR;
}

/**
 * @brief Tell pspam plugin about exiting session
 *
 * Tell the pspam plugin about exiting the session of user @a uName
 * coming from remote host @a rhost.
 *
 * @param uName Username of the user to leave
 *
 * @param rhost Hostname the user was coming from
 *
 * @return No return value
 */
void informPlugin(const char *uName, const char *rhost)
{
    int sock = openConnection(pspamSocketName);
    PS_DataBuffer_t data = { .buf = NULL};
    int written;

    if (sock == -1) {
	elog("connection to local plugin failed: %s", strerror(errno));
	return;
    }

    /* add length placeholder */
    addInt32ToMsg(0, &data);
    /* add command */
    addInt32ToMsg(PSPAM_CMD_SESS_CLOSE, &data);
    /* add ssh pid */
    addPidToMsg(getpid(), &data);
    /* add username */
    addStringToMsg(uName, &data);

    /* add correct msg len (without length) at placeholder */
    *(int32_t *)data.buf = data.bufUsed - sizeof(int32_t);

    written = writeToPspam(sock, data.buf, data.bufUsed);
    free(data.buf);
    if (written != (int)data.bufUsed) {
	elog("sending pspam close request failed");
	return;
    }

    if (verbose > 2) {
	ilog("close: %s@%s pid %u", uName, rhost, getpid());
    }

}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    const char *uName, *rhost;
    int ret;

    pamH = pamh;

    /* parse module options */
    ret = parseModuleOptions(argc, argv);
    if (ret != PAM_SUCCESS) return ret;

    /* get session infos */
    pam_get_user(pamH, &uName, NULL);
    pam_get_item(pamH, PAM_RHOST, (const void **) &rhost);

    ret = checkAllowance(uName, rhost);

    /* free allocated memory */
    if (authUsers) free(authUsers);

    return ret;
}

PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    const char *uName, *rhost;
    int ret;

    pamH = pamh;

    /* parse module options */
    ret = parseModuleOptions(argc, argv);
    if (ret != PAM_SUCCESS) return ret;

    /* get session infos */
    pam_get_user(pamH, &uName, NULL);
    pam_get_item(pamH, PAM_RHOST, (const void **) &rhost);

    /* only tell pspam plugin if user is not in the authorized user list */
    if (!isAuthorizedUser(uName)) informPlugin(uName, rhost);

    /* free allocated memory */
    if (authUsers) free(authUsers);

    return PAM_SUCCESS;
}

PAM_EXTERN int
pam_sm_chauthtok(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    return PAM_IGNORE;
}
