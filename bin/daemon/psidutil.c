/*
 *               ParaStation3
 * psidutil.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.c,v 1.33 2002/07/03 21:10:06 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidutil.c,v 1.33 2002/07/03 21:10:06 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <pshal.h>
#include <psm_mcpif.h>

#include "errlog.h"

#include "license.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "logger.h"
#include "cardconfig.h"
#include "config_parsing.h"

#include "psidutil.h"

int PSID_HWstatus;

static char errtxt[256];

/* Wrapper functions for logging */
void PSID_initLog(int usesyslog, FILE *logfile)
{
    if (!usesyslog && logfile) {
	int fno = fileno(logfile);

	if (fno!=STDERR_FILENO) {
	    dup2(fno, STDERR_FILENO);
	    fclose(logfile);
	}
    }

    initErrLog("PSID", usesyslog);
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

static card_init_t card_info;

void PSID_ReConfig(char *licensekey, char *module, char *routingfile)
{
    int ret;
    char licdot[10];

    PSID_HWstatus = 0;

    if (nodes[PSC_getMyID()].hwType & PSP_HW_MYRINET) {
	strncpy(licdot, licensekey ? licensekey : "none", sizeof(licdot));
	licdot[4] = licdot[5] = licdot[6] = '.';
	licdot[7] = 0;

	snprintf(errtxt, sizeof(errtxt), "PSID_ReConfig: '%s' '%s' '%s'"
		 " small packets %d, ResendTimeout %d", licdot, module,
		 routingfile, ConfigSmallPacketSize, ConfigRTO);
	PSID_errlog(errtxt, 1);

	card_info.node_id = PSC_getMyID();
	card_info.licensekey = licensekey;
	card_info.module = module;
	card_info.options = NULL;
	card_info.routing_file = routingfile;

	ret = card_cleanup(&card_info);
	if (ret) {
	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_ReConfig: cardcleanup(): %s", card_errstr());
	    PSID_errlog(errtxt, 0);
	} else {
	    PSID_errlog("PSID_ReConfig: cardcleanup(): success", 10);
	}

	ret = card_init(&card_info);
	if (ret) {
	    snprintf(errtxt, sizeof(errtxt), "PSID_ReConfig: %s",
		     card_errstr());
	    PSID_errlog(errtxt, 0);
	} else {
	    PSID_errlog("PSID_ReConfig: cardinit(): success", 10);

	    PSID_HWstatus |= PSP_HW_MYRINET;

	    if (ConfigSmallPacketSize != -1) {
		PSHALSYS_SetSmallPacketSize(ConfigSmallPacketSize);
	    }

	    if (ConfigRTO != -1) {
		PSHALSYS_SetMCPParam(MCP_PARAM_RTO, ConfigRTO);
	    }

	    if (ConfigHNPend != -1) {
		PSHALSYS_SetMCPParam(MCP_PARAM_HNPEND, ConfigHNPend);
	    }

	    if (ConfigAckPend != -1) {
		PSHALSYS_SetMCPParam(MCP_PARAM_ACKPEND, ConfigAckPend);
	    }
	}
    }

    if (nodes[PSC_getMyID()].hwType & PSP_HW_ETHERNET) {
	/* Nothing to do, ethernet will work allways */
	PSID_HWstatus |= PSP_HW_ETHERNET;
    }

    if (nodes[PSC_getMyID()].hwType & PSP_HW_GIGAETHERNET) {
	PSID_errlog("'gigaethernet not implemented yet", 0);
    }

    return;
}

void PSID_CardStop(void)
{
    int ret;

    if (PSID_HWstatus & PSP_HW_MYRINET) {
	ret = card_cleanup(&card_info);
	if (ret) {
	    snprintf(errtxt, sizeof(errtxt),
		     "PSID_CardStop(): cardcleanup(): %s", card_errstr());
	    PSID_errlog(errtxt, 0);
	} else {
	    PSID_errlog("PSID_CardStop(): cardcleanup(): success", 10);

	    PSID_HWstatus &= ~PSP_HW_MYRINET;
	}
    }

    if (PSID_HWstatus & PSP_HW_ETHERNET) {
	/* Nothing to do, ethernet will work allways */
	PSID_HWstatus &= ~PSP_HW_ETHERNET;
    }

    if (PSID_HWstatus & PSP_HW_GIGAETHERNET) {
	PSID_errlog("'gigaethernet not implemented yet", 0);
    }
}

/***************************************************************************
 *	PSID_readconfigfile()
 *
 */

void PSID_readconfigfile(int usesyslog)
{
    struct hostent *mhost;
    char myname[256];
    struct in_addr *sin_addr;

    int numNICs = 2;
    int skfd, n;
    struct ifconf ifc;
    struct ifreq *ifr;

    /* Parse the configfile */
    if (parseConfig(usesyslog, PSID_getDebugLevel())<0) {
	snprintf(errtxt, sizeof(errtxt), "Parsing of <%s> failed.",
		 Configfile);
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
	ifc.ifc_buf = realloc(ifc.ifc_buf, ifc.ifc_len);
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
	    if ((MyPsiId=parser_lookupHost(sin_addr->s_addr))!=-1) {
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
	snprintf(errtxt, sizeof(errtxt), "Node '%s' not configured", myname);
	PSID_errlog(errtxt, 0);
	exit(1);
    }

    PSC_setNrOfNodes(NrOfNodes);
    PSC_setMyID(MyPsiId);
    PSC_setDaemonFlag(1); /* To get the correct result from PSC_getMyTID() */

    if (licNode.addr == INADDR_ANY) { /* Check LicServer Setting */
	/*
	 * Set node 0 as default server
	 */
	licNode.addr = nodes[0].addr;
	snprintf(errtxt, sizeof(errtxt),
		 "Using %s (ID=0) as Licenseserver",
		 inet_ntoa(* (struct in_addr *) &licNode.addr));
	PSID_errlog(errtxt, 1);
    }

    if (PSC_getNrOfNodes() > 4) {
	/*
	 * Check the license key
	 * Clusters smaller than 4 nodes are free
	 */
	// PSID_checklicense(sin_addr.s_addr);
    }

    PSID_errlog("starting up the card", 1);
    /*
     * check if I can reserve the card for me 
     * if the card is busy, the OS PSHAL_Startup will exit(0);
     */
    PSID_errlog("PSID_readconfigfile(): calling PSID_ReConfig()", 9);
    // PSHAL_StartUp(1);
    PSID_ReConfig(ConfigLicensekey, ConfigModule, ConfigRoutefile);
    PSID_errlog("PSID_readconfigfile(): PSID_ReConfig ok.", 9);
}

/***************************************************************************
 *       PSI_startlicensserver()
 *
 *       starts the licenseserver via the inetd
 */
int PSID_startlicenseserver(unsigned int addr)
{
    int sock;
    struct sockaddr_in sa;

    snprintf(errtxt, sizeof(errtxt), "PSID_startlicenseserver <%s>",
	     inet_ntoa(* (struct in_addr *) &addr));
    PSID_errlog(errtxt, 10);

    /*
     * start the PSI Daemon via inetd
     */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    memset(&sa, 0, sizeof(sa)); 
    sa.sin_family = AF_INET; 
    sa.sin_addr.s_addr = addr;
    sa.sin_port = htons(PSC_getServicePort("psld", 887));
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) { 
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt), "PSID_startlicenseserver():"
		 " Connect to port %d for start with inetd failed: %s",
		 ntohs(sa.sin_port), errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);

	shutdown(sock, SHUT_RDWR);
	close(sock);
	return 0;
    }
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 1;
}



/*----------------------------------------------------------------------*/
/*
 * PSID_execv
 *
 *  frontend to syscall execv. Retry exec on failure after a short delay 
 *  RETURN: like the syscall execv
 */
int PSID_execv( const char *path, char *const argv[])
{
    int ret;
    int cnt;

    /* Try 5 times with delay 400ms = 2 sec overall */
    for (cnt=0; cnt<5; cnt++) {
	ret = execv(path, argv);
	usleep(1000 * 400);
    }

    return ret;
}

/*----------------------------------------------------------------------*/
/*
 * PStask_spawn
 *
 *  executes the argv[0] with parameters argv[1]..argv[argc-1]
 *  in working directory workingdir with userid uid
 *  RETURN: 0 on success with childpid set to the pid of the new process
 *          errno  when an error occurs
 */
int PSID_taskspawn(PStask_t* task)
{
    int fds[2];    /* pipe fd for communication between parent and child */
    int pid;       /* pid of the child */
    int i;
    int buf;   /* buffer for communication between child and parent */
    int ret;       /* return value */
    struct stat sb;

    if (PSID_getDebugLevel() >= 10) {
	snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn() task: ");
	PStask_snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
			task);
	PSID_errlog(errtxt, 10);
    }

    /*
     * create a control channel
     * for observing the successful exec call
     */
    if (pipe(fds)<0) {
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn(): pipe: %s ",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);
    }
    fcntl(fds[1],F_SETFD,FD_CLOEXEC);

    /*
     * fork the new process
     */
    if ((pid = fork())==0) {
	/* child process */

	/*
	 * change the group id to the appropriate group
	 */
	if (setgid(task->gid)<0) {
	    char *errstr = strerror(errno);
	    buf = errno;

	    snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn(): setgid: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(fds[1], &buf, sizeof(buf));
	    exit(0);
	}

	/*
	 * change the user id to the appropriate user
	 */
	if (setuid(task->uid)<0) {
	    char *errstr = strerror(errno);
	    buf = errno;

	    snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn() setuid: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(fds[1], &buf, sizeof(buf));
	    exit(0);
	}

	/*
	 * change to the appropriate directory
	 */
	if (chdir(task->workingdir)<0) {
	    char *errstr = strerror(errno);
	    buf = errno;

	    snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn(): chdir(%s): %s",
		     task->workingdir ? task->workingdir : "",
 		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(fds[1], &buf, sizeof(buf));
	    exit(0);
	}

	/*
	 * set the environment variable
	 */
	{
	    char *envvar;
	    envvar = malloc(strlen(task->workingdir) + strlen("PWD=") + 1);
	    sprintf(envvar, "PWD=%s", task->workingdir);
	    /* Don't free envvar, since it becomes part of the environment! */
	    putenv(envvar);

	    if (task->environ) {
		for (i=0; task->environ[i]; i++) {
		    putenv(strdup(task->environ[i]));
		}
	    }
	}
	{
	    /*
	     * store client PID in environment
	     */
	    char pid_str[20];
	    snprintf(pid_str, sizeof(pid_str), "%d", getpid());
	    setenv("PSI_PID", pid_str, 1);
	}
	if (stat(task->argv[0], &sb) == -1) {
	    char *errstr = strerror(errno);
	    buf = errno;

	    snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn(): stat(%s): %s",
		     task->argv[0] ? task->argv[0] : "",
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);

	    write(fds[1], &buf, sizeof(buf));
	    exit(0);
	}

	if (((sb.st_mode & S_IFMT) != S_IFREG) || !(sb.st_mode & S_IEXEC)) {
	    buf = 1;
	    snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn(): stat(): %s",
		     ((sb.st_mode & S_IFMT) != S_IFREG) ? "S_IFREG error" :
		     (sb.st_mode & S_IEXEC) ? "" : "S_IEXEC error");
	    PSID_errlog(errtxt, 0);

	    write(fds[1], &buf, sizeof(buf));
	    exit(0);
	}

	/*
	 * close all file descriptors
	 * except my control channel to my parent
	 */
	for (i=getdtablesize()-1; i>2; i--)
	    if (i != fds[1]) close(i);

	/*
	 * Start the forwarder and redirect stdout/stderr
	 */
	LOGGERspawnforwarder(task->loggernode, task->loggerport, task->rank,
			     task->rank == 0);

	/*
	 * execute the image
	 */
	if (PSID_execv(task->argv[0],&(task->argv[0]))<0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn() execv: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	}
	/*
	 * never reached, if execv succesful
	 */
	/*
	 * send the parent a sign that the exec wasn't successful
	 * fds[0] would have been closed on successful exec.
	 */
	buf = errno;
	write(fds[1], &buf, sizeof(buf));
	exit(0);
    }
    /*
     * this is the parent process
     */
    /*
     * check if fork() was successful
     */
    if (pid ==-1) {
	char *errstr = strerror(errno);

	close(fds[0]);
	close(fds[1]);

	snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn() fork: %s",
		 errstr ? errstr : "UNKNOWN");
	PSID_errlog(errtxt, 0);

	task->error = -errno;
	ret = -errno;
    } else {
	/*
	 * check for a sign of the child
	 */
	snprintf(errtxt, sizeof(errtxt),
		 "I'm the parent. I'm waiting for my child (%d)", pid);
	PSID_errlog(errtxt, 10);

	close(fds[1]);

    restart:
	if ((ret=read(fds[0], &buf, sizeof(buf))) < 0) {
	    if (errno == EINTR) {
		goto restart;
	    }
	}

	if (ret == 0) {
	    /*
	     * the control channel was closed in case of a successful execv
	     */
	    ret = 0;
	    task->error = 0;
	    task->tid = PSC_getTID(-1,pid);
	    task->nodeno = PSC_getID(-1);

	    PSID_errlog("child execute was successful", 10);
	} else {
	    /*
	     * the child sent us a sign that the execv wasn't successful
	     */
	    char *errstr = strerror(ret=buf);

	    snprintf(errtxt, sizeof(errtxt), "child execute failed: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSID_errlog(errtxt, 0);
	}
	close(fds[0]);
    }

    return ret;
}
