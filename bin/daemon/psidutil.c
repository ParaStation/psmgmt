/*
 *               ParaStation
 * psidutil.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.c,v 1.68 2003/12/11 20:28:01 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidutil.c,v 1.68 2003/12/11 20:28:01 eicker Exp $";
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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "errlog.h"
#include "psprotocol.h"

#include "config_parsing.h"
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"

#include "psidutil.h"

static char errtxt[256];

config_t *config = NULL;

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
	snprintf(errtxt, sizeof(errtxt), "%s: sigprocmask()", __func__);
	PSID_errlog(errtxt, 0);
    }
}

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
    size_t c = count;

    while (c > 0) {
        len = read(fd, buf, c);
	if (len < 0) {
	    if ((errno == EINTR) || (errno == EAGAIN))
		continue;
	    else
		return -1;
	} else if (len == 0) {
	    return count-c;
	}
        c -= len;
        (char*)buf += len;
    }

    return count;
}

char scriptOut[1024];

/**
 * @brief @todo
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

	    command = malloc(strlen(dir) + 1 + strlen(script) + 1);

	    strcpy(command, dir);
	    strcat(command, "/");
	    strcat(command, script);
	} else {
	    command = strdup(script);
	}

        /* redirect stdout and stderr */
        dup2(iofds[1], STDOUT_FILENO);
        dup2(iofds[1], STDERR_FILENO);
	close(iofds[1]);

	{
	    char *dir = PSC_lookupInstalldir();

	    if (dir) chdir(dir);
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
	snprintf(errtxt, sizeof(errtxt),
		 "%s: hw = %d out of range", __func__, hw);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (script) {
	int res = callScript(hw, script);

	if (res) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: callScript(%s, %s) returned %d: %s",
		     __func__, HW_name(hw), script, res, scriptOut);
	    PSID_errlog(errtxt, 0);
	} else {
	    unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	    snprintf(errtxt, sizeof(errtxt), "%s: callScript(%s, %s): success",
		     __func__, HW_name(hw), script);
	    PSID_errlog(errtxt, 10);

	    PSnodes_setHWStatus(PSC_getMyID(), status | (1<<hw));

	}
    } else {
	/* No script, assume HW runs already */
	unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	snprintf(errtxt, sizeof(errtxt), "%s: assume %s already up",
		 __func__, HW_name(hw));
	PSID_errlog(errtxt, 10);

	PSnodes_setHWStatus(PSC_getMyID(), status | (1<<hw));
    }
}

void PSID_startAllHW(void)
{
    int hw;
    for (hw=0; hw<HW_num(); hw++) {
	if (PSnodes_getHWType(PSC_getMyID()) & (1<<hw)) PSID_startHW(hw);
    }
}

void PSID_stopHW(int hw)
{
    char *script = HW_getScript(hw, HW_STOPPER);

    if (hw<0 || hw>HW_num()) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: hw = %d out of range", __func__, hw);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (script) {
	int res = callScript(hw, script);

	if (res) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: callScript(%s, %s) returned %d: %s",
		     __func__, HW_name(hw), script, res, scriptOut);
	    PSID_errlog(errtxt, 0);
	} else {
	    unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	    snprintf(errtxt, sizeof(errtxt), "%s: callScript(%s, %s): success",
		     __func__, HW_name(hw), script);
	    PSID_errlog(errtxt, 10);

	    PSnodes_setHWStatus(PSC_getMyID(), status & ~(1<<hw));

	}
    } else {
	/* No script, assume HW does not run any more */
	unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	snprintf(errtxt, sizeof(errtxt),
		 "%s: assume %s already down", __func__, HW_name(hw));
	PSID_errlog(errtxt, 10);

	PSnodes_setHWStatus(PSC_getMyID(), status & ~(1<<hw));
    }
}

void PSID_stopAllHW(void)
{
    int hw;
    for (hw=HW_num()-1; hw>=0; hw--) {
	if (PSnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) PSID_stopHW(hw);
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
			 "%s: callScript(%s, %s) returned %d: %s",
			 __func__, HW_name(hw), script, res, scriptOut);
		PSID_errlog(errtxt, 0);
		strncpy(buf, errtxt, size);
	    } else {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: callScript(%s, %s): success",
			 __func__, HW_name(hw), script);
		PSID_errlog(errtxt, 10);

		strncpy(buf, scriptOut, size);
	    }
	} else {
	    /* No script, cannot get counter */
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: no %s-script for %s available",
		     __func__, header ? "header" : "counter", HW_name(hw));
	    PSID_errlog(errtxt, 1);

	    strncpy(buf, errtxt, size);
	}
    } else {
	/* No HW, cannot get counter */
	snprintf(errtxt, sizeof(errtxt), "%s: no %s hardware available",
		 __func__, HW_name(hw));
	PSID_errlog(errtxt, 0);

	strncpy(buf, errtxt, size);
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
 * @brief Determine local node ID
 *
 * Determine the local ParaStation ID. This is done by investigating
 * the IP address of each local ethernet device and trying to resolve
 * these to a valid ParaStation ID. Thus the hostnames within the
 * ParaStation configuration file have to be resolved to correct IP
 * addresses.
 *
 * @return Upon success, i.e. if the local ParaStation ID could be
 * determined, this ID is returned. Otherwise -1 is returned.
 */
static int getOwnID(void)
{
    struct in_addr *sin_addr;

    int numNICs = 1;
    int skfd, n, ownID = -1;
    struct ifconf ifc;
    struct ifreq *ifr;

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
	    if ((ownID=PSnodes_lookupHost(sin_addr->s_addr))!=-1) {
		/* node is configured */
		snprintf(errtxt, sizeof(errtxt),
			 "Node found to have ID %d", ownID);
		PSID_errlog(errtxt, 10);
		break;
	    }
	}
	ifr++;
    }
    /* Clean up */
    free(ifc.ifc_buf);
    close(skfd);

    return ownID;
}

/* Reading and basic handling of the configuration */
void PSID_readConfigFile(int usesyslog, char *configfile)
{
    int ownID;

    /* Parse the configfile */
    config = parseConfig(usesyslog, PSID_getDebugLevel(), configfile);
    if (! config) {
	snprintf(errtxt, sizeof(errtxt), "Parsing of <%s> failed.",
		 configfile);
	PSID_errlog(errtxt, 0);
	exit(1);
    }
    config->useSyslog = usesyslog;

    /* Set correct debugging level if given in config-file */
    if (config->logLevel && !PSID_getDebugLevel()) {
	PSID_setDebugLevel(config->logLevel);
	PSC_setDebugLevel(config->logLevel);
    }

    /* Try to find out if node is configured */
    ownID = getOwnID();
    if (ownID == -1) {
	PSID_errlog("Node not configured", 0);
	exit(1);
    }

    PSC_setNrOfNodes(PSnodes_getNum());
    PSC_setMyID(ownID);
    PSC_setDaemonFlag(1); /* To get the correct result from PSC_getMyTID() */
}

long PSID_getVirtCPUs(void)
{
    return sysconf(_SC_NPROCESSORS_CONF);
}

long PSID_getPhysCPUs(void)
{
    int buf[8];
    char filename[80] = { "/dev/cpu/0/cpuid" };
    int fd, got, i;

    int intelMagic[3] = { 0x756e6547, 0x6c65746e, 0x49656e69 };
    long virtCPUs = PSID_getVirtCPUs(), physCPUs = 0;
    int virtCount, virtMask, APIC_ID;

    snprintf(errtxt, sizeof(errtxt), "%s: %ld virtual CPUs found.",
	     __func__, virtCPUs);
    PSID_errlog(errtxt, 5);

    fd = open(filename, 0);
    if (fd==-1) {
	if (errno == ENODEV) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No CPUID support.",
		     __func__);
	} else {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "%s: Unable to open '%s': %s\n",
		     __func__, filename, errstr ? errstr : "UNKNOWN");
	}
#ifdef __i386__
	PSID_errlog(errtxt, 0);
#endif
	return virtCPUs;
    }

    got = read(fd, buf, sizeof(buf));
    if (got != sizeof(buf)) {
	snprintf(errtxt, sizeof(errtxt), "%s: Got only %d/%ld bytes.",
		 __func__, got, (long)sizeof(buf));
	PSID_errlog(errtxt, 0);
	return virtCPUs;
    }

    for (i=0; i<3; i++) {
	if (buf[i+1] != intelMagic[i]) {
	    snprintf(errtxt, sizeof(errtxt), "%s: No Intel CPU.", __func__);
	    PSID_errlog(errtxt, 5);
	    return virtCPUs;
	}
    }
    snprintf(errtxt, sizeof(errtxt), "%s: Intel CPU.", __func__);
    PSID_errlog(errtxt, 5);

    if (! (buf[7] & 0x10000000)) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: No Hyper-Threading Technology.", __func__);
	PSID_errlog(errtxt, 5);
	return virtCPUs;
    }
    snprintf(errtxt, sizeof(errtxt),
	     "%s: With Hyper-Threading Technology.", __func__);
    PSID_errlog(errtxt, 5);
    virtCount =  (buf[5] & 0x00ff0000) >> 16;
    snprintf(errtxt, sizeof(errtxt),
	     "%s: CPU supports %d virtual CPUs\n", __func__, virtCount);
    PSID_errlog(errtxt, 5);
    virtMask = virtCount-1;

    for (i=0; i<virtCPUs; i++) {
	snprintf(filename, sizeof(filename), "/dev/cpu/%d/cpuid", i);
	fd = open(filename, 0);
	if (fd==-1) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "%s: Unable to open '%s': %s\n",
		     __func__, filename, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	    return virtCPUs;
	}
	got = read(fd, buf, sizeof(buf));
	if (got != sizeof(buf)) {
	    snprintf(errtxt, sizeof(errtxt), "%s: Got only %d/%ld bytes.",
		     __func__, got, (long)sizeof(buf));
	    PSID_errlog(errtxt, 0);
	    return virtCPUs;
	}

	APIC_ID = (buf[5] & 0xff000000) >> 24;
	snprintf(errtxt, sizeof(errtxt), "%s: APIC ID %d -> %s CPU", __func__,
		 APIC_ID, APIC_ID & virtMask ? "virtual" : "physical");
	PSID_errlog(errtxt, 5);

	if ( ! (APIC_ID & virtMask)) physCPUs++;
    }

    return physCPUs;
}
