/*
 *               ParaStation3
 * psidutil.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.c,v 1.56 2003/04/07 10:00:53 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidutil.c,v 1.56 2003/04/07 10:00:53 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <net/if.h>
#include <signal.h>

#include "errlog.h"
#include "psprotocol.h"

#include "config_parsing.h"
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"

#include "pslic.h"

/* magic license check */
#include "../license/pslic_hidden.h"

#include "psidutil.h"

static char errtxt[256];

/* Wrapper functions for logging */
void PSID_initLog(int usesyslog, FILE *logfile)
{
    if (!usesyslog && logfile) {
	int fno = fileno(logfile);

	if (fno!=STDERR_FILENO) {
	    dup2(fno, STDERR_FILENO);
	    close(fno);
	}
    }

    initErrLog(usesyslog ? NULL : "PSID", usesyslog);
}

int PSID_getDebugLevel(void)
{
    return getErrLogLevel();
}

void PSID_setDebugLevel(int level)
{
    setErrLogLevel(level);
}

void PSID_errlog(char *s, int level)
{
    errlog(s, level);
}

void PSID_errexit(char *s, int errorno)
{
    errexit(s, errorno);
}

void PSID_blockSig(int block, int sig)
{
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL)) {
	PSID_errlog("blockSig(): sigprocmask()", 0);
    }
}

static size_t writeall(int fd, const void *buf, size_t count)
{
    int len;
    size_t c = count;

    while (c > 0) {
        len = write(fd, buf, c);
        if (len < 0) {
            if ((errno == EINTR) || (errno == EAGAIN))
                continue;
	    else
                return -1;
        }
        c -= len;
        (char*)buf += len;
    }

    return count;
}

static size_t readall(int fd, void *buf, size_t count)
{
    int len;
    size_t c = count;

    while (c > 0) {
        len = read(fd, buf, c);
        if (len <= 0) {
            if (len < 0) {
                if ((errno == EINTR) || (errno == EAGAIN))
                    continue;
                else
		    return -1;
            } else {
                return count-c;
            }
        }
        c -= len;
        (char*)buf += len;
    }

    return count;
}

char scriptOut[1024];

static int callScript(int hw, char *script)
{
    int fds[2];
    int ret, result;
    pid_t pid;

    /* Don't SEGFAULT */
    if (!script) {
        snprintf(scriptOut, sizeof(scriptOut), "%s: script=NULL\n", __func__);
        return -1;
    }

    /* create a control channel in order to observe the script */
    if (pipe(fds)<0) {
        char *errstr = strerror(errno);
        snprintf(scriptOut, sizeof(scriptOut), "%s: pipe(): %s\n",
                 __func__, errstr ? errstr : "UNKNOWN");

        return -1;
    }

    if (!(pid=fork())) {
        /* This part calls the script and returns results to the parent */
        int systemfds[2], status, i;
        char *command;
        char buf[20];

        close(fds[0]);

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

	    command = malloc(strlen(dir) + 1 + strlen(script) + 1);

	    strcpy(command, dir);
	    strcat(command, "/");
	    strcat(command, script);
	} else {
	    command = strdup(script);
	}

        /* redirect stdout and stderr */
        pipe(systemfds);
        dup2(systemfds[1], STDOUT_FILENO);
        dup2(systemfds[1], STDERR_FILENO);
	close(systemfds[1]);

        ret = system(command);

        /* Send results to controlling daemon */
        if (ret < 0) {
            writeall(fds[1], &ret, sizeof(ret));

            snprintf(scriptOut, sizeof(scriptOut),
		     "%s: system(%s) failed : %s",
                     __func__, command, strerror(errno));
            writeall(fds[1], scriptOut, strlen(scriptOut)+1);
        } else {
            int status = WEXITSTATUS(ret);

            writeall(fds[1], &status, sizeof(status));

	    /* forward the script message */
	    close(STDOUT_FILENO); /* Squeeze the pipe */
	    close(STDERR_FILENO);
	    ret = readall(systemfds[0], scriptOut, sizeof(scriptOut)-1);

	    writeall(fds[1], scriptOut, ret);

	    scriptOut[0] = '\0';
	    writeall(fds[1], scriptOut, 1);
        }

        close(systemfds[0]);
        free(command);
        exit(0);
    }

    /* This part receives results from the script */

    /* save errno in case of error */
    ret = errno;

    close(fds[1]);
        
    /* check if fork() was successful */
    if (pid == -1) {
        char *errstr = strerror(ret);

        close(fds[0]);

        snprintf(scriptOut, sizeof(scriptOut), "%s: fork(): %s\n",
                 __func__, errstr ? errstr : "UNKNOWN");

        return -1;
    }

    ret = readall(fds[0], &result, sizeof(result));
    if (!ret) {
        /* control channel closed without telling result of system() call. */
        snprintf(scriptOut, sizeof(scriptOut), "%s: no answer\n", __func__);
        return -1;
    } else if (ret<0) {
        snprintf(scriptOut, sizeof(scriptOut), "%s: read() failed : %s",
                 __func__, strerror(errno));
        return -1;
    }

    ret = readall(fds[0], scriptOut, sizeof(scriptOut));
    if (!ret) {
	/* control channel was closed during message. */
	snprintf(scriptOut, sizeof(scriptOut),
		 "%s: pipe closed unexpectedly\n", __func__);
        return -1;
    } else if (ret<0) {
	snprintf(scriptOut, sizeof(scriptOut), "%s: read() failed : %s",
		 __func__, strerror(errno));
        return -1;
    }

    scriptOut[sizeof(scriptOut)-1] = '\0';
    close(fds[0]);

    return result;
}

void PSID_startHW(int hw)
{
    if (PSnodes_getHWType(PSC_getMyID()) & (1<<hw)) {
	char *script = HW_getScript(hw, HW_STARTER);

	if (script) {
	    int res = callScript(hw, script);

	    if (res) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s(): callScript(%s, %s) returned %d: %s",
			 __func__, HW_name(hw), script, res, scriptOut);
		PSID_errlog(errtxt, 0);
	    } else {
		unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

		snprintf(errtxt, sizeof(errtxt),
			 "%s(): callScript(%s, %s): success",
			 __func__, HW_name(hw), script);
		PSID_errlog(errtxt, 10);

		PSnodes_setHWStatus(PSC_getMyID(), status | (1<<hw));

	    }
	} else {
	    /* No script, assume HW runs already */
	    unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	    snprintf(errtxt, sizeof(errtxt),
		     "%s(): %s already up", __func__, HW_name(hw));
	    PSID_errlog(errtxt, 10);

	    PSnodes_setHWStatus(PSC_getMyID(), status | (1<<hw));
	}
    }
}

void PSID_startAllHW(void)
{
    int hw;

    for (hw=0; hw<HW_num(); hw++) {
	PSID_startHW(hw);
    }
}

void PSID_stopHW(int hw)
{
    if (PSnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) {
	char *script = HW_getScript(hw, HW_STOPPER);

	if (script) {
	    int res = callScript(hw, script);

	    if (res) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s(): callScript(%s, %s) returned %d: %s",
			 __func__, HW_name(hw), script, res, scriptOut);
		PSID_errlog(errtxt, 0);
	    } else {
		unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

		snprintf(errtxt, sizeof(errtxt),
			 "%s(): callScript(%s, %s): success",
			 __func__, HW_name(hw), script);
		PSID_errlog(errtxt, 10);

		PSnodes_setHWStatus(PSC_getMyID(), status & ~(1<<hw));

	    }
	} else {
	    /* No script, assume HW does not run any more */
	    unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	    snprintf(errtxt, sizeof(errtxt),
		     "%s(): %s assume down", __func__, HW_name(hw));
	    PSID_errlog(errtxt, 10);

	    PSnodes_setHWStatus(PSC_getMyID(), status & ~(1<<hw));
	}
    }
}

void PSID_stopAllHW(void)
{
    int hw;

    for (hw=HW_num()-1; hw>=0; hw--) {
	PSID_stopHW(hw);
    }
}

void PSID_getCounter(int hw, char *buf, size_t size, int header)
{
    if (!buf) return;

    if (PSnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) {
	char *script = HW_getScript(hw, header ? HW_HEADERLINE : HW_COUNTER);

	if (script) {
	    int res = callScript(hw, script);

	    if (res) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s(): callScript(%s, %s) returned %d: %s",
			 __func__, HW_name(hw), script, res, scriptOut);
		PSID_errlog(errtxt, 0);
		strncpy(buf, errtxt, size);
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "%s(): callScript(%s, %s): success",
			 __func__, HW_name(hw), script);
		PSID_errlog(errtxt, 10);

		strncpy(buf, scriptOut, size);
	    }
	} else {
	    /* No script, cannot get counter */
	    snprintf(errtxt, sizeof(errtxt),
		     "%s(): no %s-script for %s available",
		     __func__, header ? "header" : "counter", HW_name(hw));
	    PSID_errlog(errtxt, 1);

	    strncpy(buf, errtxt, size);
	}
    } else {
	/* No HW, cannot get counter */
	snprintf(errtxt, sizeof(errtxt), "%s(): no %s hardware available",
		 __func__, HW_name(hw));
	PSID_errlog(errtxt, 0);

	strncpy(buf, errtxt, size);
    }
}

void PSID_setParam(int hw, long type, long value)
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
	    }
	}
    }

    if (script && option) {
	char command[128];

	snprintf(command, sizeof(command), "%s %s %ld", script, option, value);
	callScript(hw, command);
    }
}

long PSID_getParam(int hw, long type)
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
	    }
	}
    }

    if (script && option) {
	char command[128], *end;
	long val;

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

/* Reading and basic handling of the configuration */

void PSID_readConfigFile(int usesyslog, char *configfile)
{
    struct in_addr *sin_addr;

    int numNICs = 2;
    int skfd, n;
    struct ifconf ifc;
    struct ifreq *ifr;

    /* Parse the configfile */
    if (parseConfig(usesyslog, PSID_getDebugLevel(), configfile)<0) {
	snprintf(errtxt, sizeof(errtxt), "Parsing of <%s> failed.",
		 configfile);
	PSID_errlog(errtxt, 0);
	exit(1);
    }

    /* Set correct debugging level if given in config-file */
    if (ConfigLogLevel && !PSID_getDebugLevel()) {
	PSID_setDebugLevel(ConfigLogLevel);
	PSC_setDebugLevel(ConfigLogLevel);
    }

    /* Try to find out if node is configured */
    /* Get any socket */
    skfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (skfd<0) {
	PSID_errexit("Unable to obtain socket", errno);
    }
    PSID_errlog("Get list of NICs", 10);
    /* Get list of NICs */
    ifc.ifc_buf = NULL;
    do {
	numNICs *= 2; /* double the number of expected NICs */
	ifc.ifc_len = numNICs * sizeof(struct ifreq);
	ifc.ifc_buf = (char *)realloc(ifc.ifc_buf, ifc.ifc_len);
	if (!ifc.ifc_buf) {
	    PSID_errlog("realloc failed", 0);
	    exit(1);
	}

	if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	    PSID_errexit("Unable to obtain network configuration", errno);
	}
    } while (ifc.ifc_len == numNICs * (int)sizeof(struct ifreq));
    /* Test the IP-addresses assigned to this NICs */
    ifr = ifc.ifc_req;
    for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
	if ((ifr->ifr_addr.sa_family == AF_INET)
#ifdef __osf__
	    /* Tru64 return AF_UNSPEC for all interfaces */
	    ||(ifr->ifr_addr.sa_family == AF_UNSPEC)
#endif
	    ) {

	    sin_addr = &((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;

	    snprintf(errtxt, sizeof(errtxt),
		     "Testing address %s", inet_ntoa(*sin_addr));
	    PSID_errlog(errtxt, 10);
	    if ((MyPsiId=PSnodes_lookupHost(sin_addr->s_addr))!=-1) {
		/* node is configured */
		snprintf(errtxt, sizeof(errtxt),
			 "Node found to have ID %d", MyPsiId);
		PSID_errlog(errtxt, 10);
		break;
	    }
	}
	ifr++;
    }
    /* Clean up */
    free(ifc.ifc_buf);
    close(skfd);

    if (MyPsiId == -1) {
	PSID_errlog("Node not configured", 0);
	exit(1);
    }

    PSC_setNrOfNodes(PSnodes_getNum());
    PSC_setMyID(MyPsiId);
    PSC_setDaemonFlag(1); /* To get the correct result from PSC_getMyTID() */
}

int PSID_startLicServer(unsigned int addr)
{
    int sock;
    struct sockaddr_in sa;

    snprintf(errtxt, sizeof(errtxt), "%s(%s)",
	     __func__, inet_ntoa(* (struct in_addr *) &addr));
    PSID_errlog(errtxt, 10);

    /*
     * start the ParaStation license daemon (psld) via inetd
     */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    memset(&sa, 0, sizeof(sa)); 
    sa.sin_family = AF_INET; 
    sa.sin_addr.s_addr = addr;
    sa.sin_port = htons(PSC_getServicePort("psld", 887));
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) { 
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt),
		 "%s: Connect to port %d for start with inetd failed: %s",
		 __func__, ntohs(sa.sin_port), errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);

	shutdown(sock, SHUT_RDWR);
	close(sock);
	return 0;
    }
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 1;
}
