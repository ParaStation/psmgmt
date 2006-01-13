/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
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
#include <sys/file.h>

#include "logging.h"
#include "psprotocol.h"

#include "config_parsing.h"
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"

#include "psidutil.h"

config_t *config = NULL;

logger_t *PSID_logger;

/* Wrapper functions for logging */
void PSID_initLog(FILE *logfile)
{
    PSID_logger = logger_init(logfile ? "PSID" : NULL, logfile);
}

int32_t PSID_getDebugMask(void)
{
    return logger_getMask(PSID_logger);
}

void PSID_setDebugMask(int32_t mask)
{
    logger_setMask(PSID_logger, mask);
}

void PSID_blockSig(int block, int sig)
{
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL)) {
	PSID_log(-1, "%s: sigprocmask()\n", __func__);
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
	PSID_log(-1, "%s: hw = %d out of range\n", __func__, hw);
	return;
    }

    if (script) {
	int res = callScript(hw, script);

	if (res) {
	    PSID_log(-1, "%s: callScript(%s, %s) returned %d: %s\n",
		     __func__, HW_name(hw), script, res, scriptOut);
	} else {
	    unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	    PSID_log(PSID_LOG_HW, "%s: callScript(%s, %s): success\n",
		     __func__, HW_name(hw), script);

	    PSnodes_setHWStatus(PSC_getMyID(), status | (1<<hw));

	}
    } else {
	/* No script, assume HW runs already */
	unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	PSID_log(PSID_LOG_HW, "%s: assume %s already up\n",
		 __func__, HW_name(hw));

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
	PSID_log(-1, "%s: hw = %d out of range\n", __func__, hw);
	return;
    }

    if (script) {
	int res = callScript(hw, script);

	if (res) {
	    PSID_log(-1, "%s: callScript(%s, %s) returned %d: %s\n",
		     __func__, HW_name(hw), script, res, scriptOut);
	} else {
	    unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	    PSID_log(PSID_LOG_HW, "%s: callScript(%s, %s): success\n",
		     __func__, HW_name(hw), script);

	    PSnodes_setHWStatus(PSC_getMyID(), status & ~(1<<hw));

	}
    } else {
	/* No script, assume HW does not run any more */
	unsigned int status = PSnodes_getHWStatus(PSC_getMyID());

	PSID_log(PSID_LOG_HW, "%s: assume %s already down\n",
		 __func__, HW_name(hw));

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
	PSID_exit(errno, "%s: socket()", __func__);
    }
    PSID_log(PSID_LOG_VERB, "%s: get list of NICs\n", __func__);
    /* Get list of NICs */
    ifc.ifc_buf = NULL;
    do {
	numNICs *= 2; /* double the number of expected NICs */
	ifc.ifc_len = numNICs * sizeof(struct ifreq);
	ifc.ifc_buf = (char *)realloc(ifc.ifc_buf, ifc.ifc_len);
	if (!ifc.ifc_buf) {
	    PSID_exit(errno, "%s: realloc()", __func__);
	}

	if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	    PSID_exit(errno, "%s: ioctl(SIOCGIFCONF)");
	}
    } while (ifc.ifc_len == numNICs * (int)sizeof(struct ifreq));
    /* Test the IP-addresses assigned to this NICs */
    ifr = ifc.ifc_req;
    for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
	if (ifr->ifr_addr.sa_family == AF_INET) {
	    sin_addr = &((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;

	    PSID_log(PSID_LOG_VERB, "%s: testing address %s\n",
		     __func__, inet_ntoa(*sin_addr));
	    if ((ownID=PSnodes_lookupHost(sin_addr->s_addr))!=-1) {
		/* node is configured */
		PSID_log(PSID_LOG_VERB, "%s: node has ID %d\n",
			 __func__, ownID);
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
void PSID_readConfigFile(FILE* logfile, char *configfile)
{
    int ownID;

    /* Parse the configfile */
    config = parseConfig(logfile, PSID_getDebugMask(), configfile);
    if (! config) {
	PSID_log(-1, "%s: parsing of <%s> failed\n", __func__, configfile);
	exit(1);
    }
    config->logfile = logfile;

    /* Set correct debugging mask if given in config-file */
    if (config->logMask && !PSID_getDebugMask()) {
	PSID_setDebugMask(config->logMask);
	PSC_setDebugMask(config->logMask);
    }

    /* Try to find out if node is configured */
    ownID = getOwnID();
    if (ownID == -1) {
	PSID_log(-1, "%s: Node not configured\n", __func__);
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

    PSID_log(PSID_LOG_VERB, "%s: got %ld virtual CPUs\n", __func__, virtCPUs);

    fd = open(filename, 0);
    if (fd==-1) {
#ifdef __i386__
	if (errno == ENODEV) {
	    PSID_log(-1, "%s: No CPUID support\n", __func__);
	} else {
	    PSID_warn(-1, errno, "%s: unable to open '%s'", __func__,filename);
	}
#endif
	return virtCPUs;
    }

    got = read(fd, buf, sizeof(buf));
    close(fd);
    if (got != sizeof(buf)) {
	PSID_log(-1, "%s: Got only %d/%ld bytes\n",
		 __func__, got, (long)sizeof(buf));
	return virtCPUs;
    }

    for (i=0; i<3; i++) {
	if (buf[i+1] != intelMagic[i]) {
	    PSID_log(PSID_LOG_VERB, "%s: No Intel CPU\n", __func__);
	    return virtCPUs;
	}
    }
    PSID_log(PSID_LOG_VERB, "%s: Intel CPU\n", __func__);

    if (! (buf[7] & 0x10000000)) {
	PSID_log(PSID_LOG_VERB, "%s: No Hyper-Threading Technology\n",
		 __func__);
	return virtCPUs;
    }
    PSID_log(PSID_LOG_VERB, "%s: With Hyper-Threading Technology\n", __func__);
    virtCount =  (buf[5] & 0x00ff0000) >> 16;
    PSID_log(PSID_LOG_VERB, "%s: CPU supports %d virtual CPUs\n",
	     __func__, virtCount);
    virtMask = virtCount-1;

    for (i=0; i<virtCPUs; i++) {
	snprintf(filename, sizeof(filename), "/dev/cpu/%d/cpuid", i);
	fd = open(filename, 0);
	if (fd==-1) {
	    PSID_warn(-1, errno, "%s: Unable to open '%s'",
		      __func__, filename);
	    return virtCPUs;
	}
	got = read(fd, buf, sizeof(buf));
	close(fd);
	if (got != sizeof(buf)) {
	    PSID_log(-1, "%s: Got only %d/%ld bytes\n",
		     __func__, got, (long)sizeof(buf));
	    return virtCPUs;
	}

	APIC_ID = (buf[5] & 0xff000000) >> 24;
	PSID_log(PSID_LOG_VERB, "%s: APIC ID %d -> %s CPU\n", __func__,
		 APIC_ID, APIC_ID & virtMask ? "virtual" : "physical");

	if ( ! (APIC_ID & virtMask)) physCPUs++;
    }

    return physCPUs;
}

#define LOCKFILENAME "/var/lock/subsys/parastation"

int PSID_lockFD = -1;

void PSID_getLock(void)
{
    PSID_lockFD = open(LOCKFILENAME, O_CREAT);
    if (PSID_lockFD<0) {
	PSID_warn(-1, errno, "%s: Unable to open lockfile '%s'",
		  __func__, LOCKFILENAME);
	exit (1);
    }

    if (chmod(LOCKFILENAME, S_IRWXU | S_IRGRP | S_IROTH)) {
	PSID_warn(-1, errno, "%s: chmod()", __func__);
	exit (1);
    }

    if (flock(PSID_lockFD, LOCK_EX | LOCK_NB)) {
	PSID_warn(-1, errno, "%s: Unable to get lock", __func__);
	exit (1);
    }
}
