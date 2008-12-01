/*
 *               ParaStation
 *
 * Copyright (C) 2006-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "hardware.h"

#include "pscommon.h"
#include "psprotocol.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidcomm.h"

#include "psidhw.h"

/**
 * @brief Write complete buffer.
 *
 * Write the complete buffer @a buf of size @a count to the file
 * descriptor @a fd. Even if one or more trials to write to @a fd
 * fails due to e.g. timeouts, further writing attempts are made until
 * either a fatal error occurred or the whole buffer is sent.
 *
 * @param fd The file descriptor to send the buffer to.
 *
 * @param buf The buffer to send.
 *
 * @param count The number of bytes within @a buf to send.
 *
 * @return Upon success the number of bytes sent is returned,
 * i.e. usually this is @a count. Otherwise -1 is returned.
 */
static size_t writeall(int fd, const void *buf, size_t count)
{
    int len;
    char *cbuf = (char *)buf;
    size_t c = count;

    while (c > 0) {
        len = write(fd, cbuf, c);
        if (len < 0) {
            if ((errno == EINTR) || (errno == EAGAIN))
                continue;
	    else
                return -1;
        }
        c -= len;
        cbuf += len;
    }

    return count;
}

/**
 * @brief Read complete buffer.
 *
 * Read the complete buffer @a buf of size @a count from the file
 * descriptor @a fd. Even if one or more trials to read to @a fd fails
 * due to e.g. timeouts, further reading attempts are made until
 * either a fatal error occurred, an EOF is received or the whole
 * buffer is read.
 *
 * @param fd The file descriptor to read the buffer from.
 *
 * @param buf The buffer to read.
 *
 * @param count The maximum number of bytes to read.
 *
 * @return Upon success the number of bytes read is returned,
 * i.e. usually this is @a count if no EOF occurred. Otherwise -1 is
 * returned.
 */
static size_t readall(int fd, void *buf, size_t count)
{
    int len;
    char *cbuf = (char *)buf;
    size_t c = count;

    while (c > 0) {
        len = read(fd, cbuf, c);
	if (len < 0) {
	    if ((errno == EINTR) || (errno == EAGAIN))
		continue;
	    else
		return -1;
	} else if (len == 0) {
	    return count-c;
	}
        c -= len;
        cbuf += len;
    }

    return count;
}

static char scriptOut[1024];  /**< String for output from @ref callScript() */


/**
 * @brief Call a script.
 *
 * Call the script named @a script using the environment defined for
 * the hardware @a hw. In order to do so, a new process is fork(2)ed
 * which then will run the script using the system(3) call.
 *
 * The output of the script will be collected and stored within the
 * string @ref scriptOut.
 *
 * @param hw The hardware whose environment should be used when
 * calling the script.
 *
 * @param script The actual script to call.
 *
 * @return If an error occures, -1 is returned. Otherwise the script's
 * return value is returned, which also might be -1.
 */
static int callScript(int hw, char *script)
{
    int controlfds[2], iofds[2];
    int ret, result;
    pid_t pid;

    /* Don't SEGFAULT */
    if (!script) {
        snprintf(scriptOut, sizeof(scriptOut), "%s: script=NULL\n", __func__);
        return -1;
    }

    /* create a control channel in order to observe the script */
    if (pipe(controlfds)<0) {
        char *errstr = strerror(errno);
        snprintf(scriptOut, sizeof(scriptOut), "%s: pipe(): %s\n",
                 __func__, errstr ? errstr : "UNKNOWN");

        return -1;
    }

    /* create a io channel in order to get script's output */
    if (pipe(iofds)<0) {
        char *errstr = strerror(errno);
        snprintf(scriptOut, sizeof(scriptOut), "%s: pipe(): %s\n",
                 __func__, errstr ? errstr : "UNKNOWN");

        return -1;
    }

    if (!(pid=fork())) {
        /* This part calls the script and returns results to the parent */
        int i, fd;
        char *command;
        char buf[20];

	for (fd=0; fd<getdtablesize(); fd++) {
	    if (fd != controlfds[1] && fd != iofds[1]) close(fd);
	}

        /* Put the hardware's environment into the real one */
	for (i=0; i<HW_getEnvSize(hw); i++) {
	    putenv(HW_dumpEnv(hw, i));
	}

        snprintf(buf, sizeof(buf), "%d", PSC_getMyID());
        setenv("PS_ID", buf, 1);

        setenv("PS_INSTALLDIR", PSC_lookupInstalldir(), 1);

	while (*script==' ' || *script=='\t') script++;
	if (*script != '/') {
	    char *dir = PSC_lookupInstalldir();

	    if (!dir) dir = "";

	    command = PSC_concat(dir, "/", script, NULL);
	} else {
	    command = strdup(script);
	}

        /* redirect stdout and stderr */
        dup2(iofds[1], STDOUT_FILENO);
        dup2(iofds[1], STDERR_FILENO);
	close(iofds[1]);

	{
	    char *dir = PSC_lookupInstalldir();

	    if (dir && (chdir(dir)<0)) {
		fprintf(stderr, "%s: cannot change to directory '%s'",
			__func__, dir);
		exit(0);
	    }
	}

        ret = system(command);

        /* Send results to controlling daemon */
        if (ret < 0) {
            fprintf(stderr, "%s: system(%s) failed : %s",
		    __func__, command, strerror(errno));
        } else {
            ret = WEXITSTATUS(ret);
        }
	writeall(controlfds[1], &ret, sizeof(ret));

        free(command);
        exit(0);
    }

    /* This part receives results from the script */

    /* save errno in case of error */
    ret = errno;

    close(controlfds[1]);
    close(iofds[1]);

    /* check if fork() was successful */
    if (pid == -1) {
        char *errstr = strerror(ret);

        close(controlfds[0]);
        close(iofds[0]);

        snprintf(scriptOut, sizeof(scriptOut), "%s: fork(): %s\n",
                 __func__, errstr ? errstr : "UNKNOWN");

        return -1;
    }

    ret = readall(iofds[0], scriptOut, sizeof(scriptOut));
    /* Discard further output */
    close(iofds[0]);

    if (ret == sizeof(scriptOut)) {
	strcpy(&scriptOut[sizeof(scriptOut)-4], "...");
    } else if (ret<0) {
        snprintf(scriptOut, sizeof(scriptOut),
		 "%s: read(iofd) failed : %s", __func__, strerror(errno));
        return -1;
    } else {
	scriptOut[ret]='\0';
    }

    ret = readall(controlfds[0], &result, sizeof(result));
    close(controlfds[0]);

    if (!ret) {
        /* control channel closed without telling result of system() call. */
        snprintf(scriptOut, sizeof(scriptOut), "%s: no answer\n", __func__);
        return -1;
    } else if (ret<0) {
        snprintf(scriptOut, sizeof(scriptOut),
		 "%s: read(controlfd) failed : %s", __func__, strerror(errno));
        return -1;
    }

    return result;
}

void PSID_startHW(int hw)
{
    char *script = HW_getScript(hw, HW_STARTER);

    if (hw<0 || hw>HW_num()) {
	PSID_log(-1, "%s: hw = %d out of range\n", __func__, hw);
	return;
    }

    if (script) {
	int res = callScript(hw, script);

	if (res) {
	    PSID_log(-1, "%s: callScript(%s, %s) returned %d: %s\n",
		     __func__, HW_name(hw), script, res, scriptOut);
	} else {
	    unsigned int status = PSIDnodes_getHWStatus(PSC_getMyID());

	    PSID_log(PSID_LOG_HW, "%s: callScript(%s, %s): success\n",
		     __func__, HW_name(hw), script);

	    PSIDnodes_setHWStatus(PSC_getMyID(), status | (1<<hw));

	}
    } else {
	/* No script, assume HW runs already */
	unsigned int status = PSIDnodes_getHWStatus(PSC_getMyID());

	PSID_log(PSID_LOG_HW, "%s: assume %s already up\n",
		 __func__, HW_name(hw));

	PSIDnodes_setHWStatus(PSC_getMyID(), status | (1<<hw));
    }
}

void PSID_startAllHW(void)
{
    int hw;
    for (hw=0; hw<HW_num(); hw++) {
	if (PSIDnodes_getHWType(PSC_getMyID()) & (1<<hw)) PSID_startHW(hw);
    }
}

void PSID_stopHW(int hw)
{
    char *script = HW_getScript(hw, HW_STOPPER);

    if (hw<0 || hw>HW_num()) {
	PSID_log(-1, "%s: hw = %d out of range\n", __func__, hw);
	return;
    }

    if (script) {
	int res = callScript(hw, script);

	if (res) {
	    PSID_log(-1, "%s: callScript(%s, %s) returned %d: %s\n",
		     __func__, HW_name(hw), script, res, scriptOut);
	} else {
	    unsigned int status = PSIDnodes_getHWStatus(PSC_getMyID());

	    PSID_log(PSID_LOG_HW, "%s: callScript(%s, %s): success\n",
		     __func__, HW_name(hw), script);

	    PSIDnodes_setHWStatus(PSC_getMyID(), status & ~(1<<hw));

	}
    } else {
	/* No script, assume HW does not run any more */
	unsigned int status = PSIDnodes_getHWStatus(PSC_getMyID());

	PSID_log(PSID_LOG_HW, "%s: assume %s already down\n",
		 __func__, HW_name(hw));

	PSIDnodes_setHWStatus(PSC_getMyID(), status & ~(1<<hw));
    }
}

void PSID_stopAllHW(void)
{
    int hw;
    for (hw=HW_num()-1; hw>=0; hw--) {
	if (PSIDnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) PSID_stopHW(hw);
    }
}

void PSID_getCounter(int hw, char *buf, size_t size, int header)
{
    if (!buf) return;

    if (PSIDnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) {
	char *script = HW_getScript(hw, header ? HW_HEADERLINE : HW_COUNTER);

	if (script) {
	    int res = callScript(hw, script);

	    if (res) {
		PSID_log(-1, "%s: callScript(%s, %s) returned %d: %s\n",
			 __func__, HW_name(hw), script, res, scriptOut);
		snprintf(buf, size, "%s: callScript(%s, %s) returned %d: %s",
			 __func__, HW_name(hw), script, res, scriptOut);
	    } else {
		PSID_log(PSID_LOG_HW, "%s: callScript(%s, %s): success\n",
			 __func__, HW_name(hw), script);
		strncpy(buf, scriptOut, size);
	    }
	} else {
	    /* No script, cannot get counter */
	    PSID_log(PSID_LOG_HW, "%s: no %s-script for %s available\n",
		     __func__, header ? "header" : "counter", HW_name(hw));
	    snprintf(buf, size, "%s: no %s-script for %s available",
		     __func__, header ? "header" : "counter", HW_name(hw));
	}
    } else {
	/* No HW, cannot get counter */
	PSID_log(-1, "%s: no %s hardware available\n", __func__, HW_name(hw));
	snprintf(buf, size, "%s: no %s hardware available",
		 __func__, HW_name(hw));
    }
}

void PSID_setParam(int hw, PSP_Option_t type, PSP_Optval_t value)
{
    char *script = NULL, *option = NULL;

    if (hw == -1) return;

    if (hw == HW_index("myrinet")) {
	script = HW_getScript(hw, HW_SETUP);
	if (script) {
	    switch (type) {
	    case PSP_OP_PSM_SPS:
		option = "-p 0";
		break;
	    case PSP_OP_PSM_RTO:
		option = "-p 1";
		break;
	    case PSP_OP_PSM_HNPEND:
		option = "-p 4";
		break;
	    case PSP_OP_PSM_ACKPEND:
		option = "-p 5";
		break;
	    default:
		break;
	    }
	}
    }

    if (script && option) {
	char command[128];

	snprintf(command, sizeof(command), "%s %s %d", script, option, value);
	callScript(hw, command);
    }
}

PSP_Optval_t PSID_getParam(int hw, PSP_Option_t type)
{
    char *script = NULL, *option = NULL;

    if (hw == -1) return -1;

    if (hw == HW_index("myrinet")) {
	script = HW_getScript(hw, HW_SETUP);
	if (script) {
	    switch (type) {
	    case PSP_OP_PSM_SPS:
		option=" -qp | grep SPS | tr -s ' ' | cut -d ' ' -f4";
		break;
	    case PSP_OP_PSM_RTO:
		option=" -qp | grep RTO | tr -s ' ' | cut -d ' ' -f4";
		break;
	    case PSP_OP_PSM_HNPEND:
		option=" -qp | grep HNPEND | tr -s ' ' | cut -d ' ' -f4";
		break;
	    case PSP_OP_PSM_ACKPEND:
		option=" -qp | grep ACKPEND | tr -s ' ' | cut -d ' ' -f4";
		break;
	    default:
		break;
	    }
	}
    }

    if (script && option) {
	char command[128], *end;
	PSP_Optval_t val;

	snprintf(command, sizeof(command), "%s %s", script, option);
	callScript(hw, command);

	val = strtol(scriptOut, &end, 10);
	while (*end == ' ' || *end == '\t' || *end == '\n') end++;
	if (*end != '\0') val = -1;

	return val;
    } else {
	return -1;
    }
}

/**
 * @brief Inform nodes
 *
 * Inform all other nodes on the local situation concerning the
 * communication hardware. Therefor a messages describing the
 * installed on the local node is broadcasted to all other nodes that
 * are currently up.
 *
 * @return No return value.
 */
static void informOtherNodes(void)
{
    DDOptionMsg_t msg = (DDOptionMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg) },
	.count = 1,
	.opt = {(DDOption_t) {
	    .option =  PSP_OP_HWSTATUS,
	    .value =  PSIDnodes_getHWStatus(PSC_getMyID()) }
	}};

    if (broadcastMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: broadcastMsg()", __func__);
    }
}

/**
 * @brief Handle PSP_CD_HWSTART message
 *
 * Handle the message @a msg of type PSP_CD_HWSTART.
 *
 * Start the communication hardware as described within @a msg. If
 * starting succeeded and the corresponding hardware was down before,
 * all other nodes are informed on the change hardware situation on
 * the local node.
 *
 * @param msg Pointer to message to handle.
 *
 * @return No return value.
 */
static void msg_HWSTART(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_HW, "%s: requester %s\n",
	     __func__, PSC_printTID(msg->header.sender));

    if (msg->header.dest == PSC_getMyTID()) {
	int hw = *(int *)msg->buf;
	int oldStat = PSIDnodes_getHWStatus(PSC_getMyID());

	if (hw == -1) {
	    PSID_startAllHW();
	} else {
	    PSID_startHW(hw);
	}
	if (oldStat != PSIDnodes_getHWStatus(PSC_getMyID()))
	    informOtherNodes();
    } else {
	sendMsg(msg);
    }
}

/**
 * @brief Handle PSP_CD_HWSTOP message
 *
 * Handle the message @a msg of type PSP_CD_HWSTOP.
 *
 * Stop the communication hardware as described within @a msg. If
 * stopping succeeded and the corresponding hardware was up before,
 * all other nodes are informed on the change hardware situation on
 * the local node.
 *
 * @param msg Pointer to message to handle.
 *
 * @return No return value.
 */
static void msg_HWSTOP(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_HW, "%s: requester %s\n",
	     __func__, PSC_printTID(msg->header.sender));

    if (msg->header.dest == PSC_getMyTID()) {
	int hw = *(int *)msg->buf;
	int oldStat = PSIDnodes_getHWStatus(PSC_getMyID());

	if (hw == -1) {
	    PSID_stopAllHW();
	} else {
	    PSID_stopHW(hw);
	}
	if (oldStat != PSIDnodes_getHWStatus(PSC_getMyID()))
	    informOtherNodes();
    } else {
	sendMsg(msg);
    }
}

void initHW(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_HWSTART, msg_HWSTART);
    PSID_registerMsg(PSP_CD_HWSTOP,msg_HWSTOP );
}
