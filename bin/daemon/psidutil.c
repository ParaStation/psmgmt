/*
 *               ParaStation3
 * psidutil.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.c,v 1.26 2002/02/15 19:35:25 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidutil.c,v 1.26 2002/02/15 19:35:25 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <pshal.h>
#include <psm_mcpif.h>

#include "license.h"

#include "psi.h"
#include "psilog.h"
#include "parse.h"
#include "logger.h"
#include "cardconfig.h"

#include "psidutil.h"

int PSID_CardPresent;

struct PSID_host_t *PSID_hosts[256];
unsigned int *PSID_hostaddresses = NULL;
char *PSID_hoststatus = NULL;

static char errtxt[256];

void PSID_ReConfig(int nodeid, int nrofnodes, char *licensekey, char *module,
		   char *routingfile)
{
    int ret;
    card_init_t card_info;

    PSI_myid = nodeid;
    PSI_nrofnodes = nrofnodes;

    if (! PSID_CardPresent) {
	return;
    }

    SYSLOG(1,(LOG_ERR, "PSID_ReConfig: %d '%s' '%s' '%s'"
	      " small packets %d, ResendTimeout %d\n",
	      nodeid, licensekey, module, routingfile,
	      ConfigSmallPacketSize,ConfigRTO));

    card_info.node_id = nodeid;
    card_info.licensekey = licensekey;
    card_info.module = module;
    card_info.options = NULL;
    card_info.routing_file = routingfile;

    card_cleanup();
    ret = card_init(&card_info);
    if (ret) {
	PSID_CardPresent = 0;	
	return;
    }

    if(ConfigSmallPacketSize != -1){
	PSHALSYS_SetSmallPacketSize(ConfigSmallPacketSize);
    }

    if(ConfigRTO != -1){
	PSHALSYS_SetMCPParam(MCP_PARAM_RTO, ConfigRTO);
    }

    if(ConfigHNPend != -1){
	PSHALSYS_SetMCPParam(MCP_PARAM_HNPEND, ConfigHNPend);
    }

    if(ConfigAckPend != -1){
	PSHALSYS_SetMCPParam(MCP_PARAM_ACKPEND, ConfigAckPend);
    }

    return;
}

void PSID_CardStop(void)
{
    if (PSID_CardPresent) {
	card_cleanup();
    }
}

/***************************************************************************
 *      PSID_checklicense()
 *
 */
int PSID_checklicense(unsigned int myIP)
{
    /* check the license key at Node 0 */
/*      unsigned int IP;  */
    long nodes;
    unsigned long end=0;
    unsigned long start=0;
/*      long version; */
    time_t now;

/*      IpNodesEndFromLicense(ConfigLicensekey, &IP, &nodes, &start, &end, */
/*  			  &version); */

    now = time(NULL);
    if(now-start<0){
	/* License is no more valid */
	SYSLOG(0,(LOG_ERR,"PSID_checklicense(): Your clock is running wrong"));
	exit(-1);
    }
    if(start+end==0){
	/* License is no more valid */
	SYSLOG(0,(LOG_ERR,"PSID_checklicense(): Licensekey invalid."));
	exit(-1);
    }
    if(end<now){
	/* License is no more valid */
	SYSLOG(0,(LOG_ERR,"PSID_checklicense(): License is out of date."
		  "(end %lx now %lx) ", end, now));
	exit(-1);
    }
    SYSLOG(9,(LOG_ERR,"nodes %ld PSI_nrofnodes %d", nodes, PSI_nrofnodes));
    if((nodes < PSI_nrofnodes) || (PSI_nrofnodes<0)){
	/* License is no more valid */
	SYSLOG(0,(LOG_ERR,"License is not valid for this number of nodes."));
	exit(-1);
    }

    return 1;
}

/***************************************************************************
 *	PSID_readconfigfile()
 *
 */

int PSID_readconfigfile(void)
{
    struct hostent *mhost;
    char myname[256];
    struct in_addr sin_addr;
    char* errstr;

    int i;

    gethostname(myname,sizeof(myname));

    if(! (mhost = gethostbyname(myname))){
	endhostent(); 
	perror("Unable to lookup hostname");
	errstr=strerror(errno);
	SYSLOG(0,(LOG_ERR,
		  "PSID_readconfigfile():Unable to lookup hostname: [%d] %s",
		  errno, errstr ? errstr : "UNKNOWN errno"));
	exit(-1);
    }
    memcpy(&sin_addr, mhost->h_addr, mhost->h_length); 
    endhostent(); 

    if (parseConfig(1)<0)
	return -1;

    PSI_nrofnodes = NrOfNodes;

    if (PSI_nrofnodes > 4) {
	/*
	 * Check the license key
	 * Clusters smaller than 4 nodes are free
	 */
	// PSID_checklicense(sin_addr.s_addr);
    }

    PSID_hoststatus = (char *) malloc(NrOfNodes * sizeof(char));
    PSID_hostaddresses =
	(unsigned int *) malloc(NrOfNodes * sizeof(*PSID_hostaddresses));
    /*
     * check the PSID specific entries in the configfile
     * if (parameters !=-1)  use value of parameter
     * else if (PSIconfigvalue !=-1)  use value of ConfigValue
     * else  use value of #define
     */
    for (i=0; i<PSI_nrofnodes; i++) {
	if (!PSID_inserthost(psihosttable[i].inet,i)) {
	    /* error: the host could not be inserted 
	       in the hostlist */
	    snprintf(errtxt, sizeof(errtxt),
		    "PSID_readconfigfile: host (address[%x],id[%d])"
		    " could not be inserted in the hostlist.\n",
		    psihosttable[i].inet, i);
	    PSI_logerror(errtxt);
	}
    }

    if (PSID_host(sin_addr.s_addr)==-1) {
	PSID_CardPresent = 0;
	SYSLOG(1,(LOG_ERR,"No card present\n"));
	return PSID_CardPresent;
    }

    PSID_CardPresent = 1;
    SYSLOG(1,(LOG_ERR,"starting up the card\n"));
    /*
     * check if I can reserve the card for me 
     * if the card is busy, the OS PSHAL_Startup will exit(0);
     */
    SYSLOG(9,(LOG_ERR,"PSID_readconfigfile():doing PSID_ReConfing..."));
    // PSHAL_StartUp(1);
    PSID_ReConfig(MyPsiId, NrOfNodes, ConfigLicensekey, ConfigModule,
		  ConfigRoutefile);
    SYSLOG(9,(LOG_ERR,"PSID_readconfigfile():PSID_ReConfig ok."));

    return PSID_CardPresent;
}

/***************************************************************************
 *       PSI_startlicensserver()
 *
 *       starts the licenseserver via the inetd
 */
int PSID_startlicenseserver(unsigned int hostaddr)
{
    int sock;
    struct servent *service;
    struct sockaddr_in sa;
#if defined(DEBUG)
    if(PSP_DEBUGADMIN & (PSI_debugmask )){
	snprintf(errtxt, sizeof(errtxt), "PSID_startlicenseserver(%ulx)\n",
		 ntohl(hostaddr));
	PSI_logerror(errtxt);
    }
#endif
    /*
     * start the PSI Daemon via inetd
     */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if ((service = getservbyname("psld","tcp")) == NULL){ 
	snprintf(errtxt, sizeof(errtxt), "PSID_startlicenseserver():"
		 " can't get \"psld\" service entry\n"); 
	fprintf(stderr, errtxt);
	PSI_logerror(errtxt);
	shutdown(sock,2);
	close(sock);
	return 0; 
    }
    memset(&sa, 0, sizeof(sa)); 
    sa.sin_family = AF_INET; 
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port = service->s_port;
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0){ 
	perror("PSID_startlicenseserver():"
	       " Connect to port for start with inetd failed."); 
	shutdown(sock,2);
	close(sock);
	return 0;
    }
    shutdown(sock,2);
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
    for (cnt=0;cnt<5;cnt++){
	ret = execv(path,argv);
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

#if defined(DEBUG)||defined(PSID)
    if(PSP_DEBUGTASK & PSI_debugmask){
	snprintf(errtxt, sizeof(errtxt), "PSID_taskspawn(task: ");
	/** \todo this may cause segfaults !! Norbert */
	PStask_sprintf(errtxt+strlen(errtxt), task);
	/** \todo this is not correct since sizeof(errtxt)-strlen(errtxt) may be
	    negative !! Norbert */
	snprintf(errtxt + strlen(errtxt), sizeof(errtxt)-strlen(errtxt), ")\n");
	PSI_logerror(PSI_txt);
    }
#endif

    /*
     * create a control channel
     * for observing the successful exec call
     */
    if(pipe(fds)<0){
	char* errstr;
	errstr = strerror(errno);
	syslog(LOG_ERR, "PSID_taskspawn(pipe): [#%d] %s ", errno,
	       errstr ? errstr : "UNKNOWN");
	perror("pipe");
    }
    fcntl(fds[1],F_SETFD,FD_CLOEXEC);

    /*
     * fork the new process
     */
    if ((pid = fork())==0){
	/* child process */

	/*
	 * change the group id to the appropriate group
	 */
	if(setgid(task->gid)<0){
	    char* errstr;
	    errstr = strerror(errno);

	    syslog(LOG_ERR, "PSID_taskspawn(setgid): [%d] %s", errno,
		   errstr ? errstr : "UNKNOWN");
	    perror("setgid");
	    buf = errno;
	    write(fds[1], &buf, sizeof(buf));
	    exit(0);
	}

	/*
	 * change the user id to the appropriate user
	 */
	if(setuid(task->uid)<0){
	    char* errstr;
	    errstr = strerror(errno);

	    syslog(LOG_ERR, "PSID_taskspawn(setuid): [%d] %s", errno,
		   errstr ? errstr : "UNKNOWN");
	    perror("setuid");
	    buf = errno;
	    write(fds[1], &buf, sizeof(buf));
	    exit(0);
	}

	/*
	 * change to the appropriate directory
	 */
	if(chdir(task->workingdir)<0){
	    char* errstr;
	    errstr = strerror(errno);
	    syslog(LOG_ERR, "PSID_taskspawn(chdir): %d %s :%s", errno,
		   errstr ? errstr : "UNKNOWN",
		   task->workingdir ? task->workingdir : "");
	    perror("chdir");
	    buf = errno;
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
	    snprintf(pid_str,sizeof(pid_str)+1,"%d",getpid());
	    setenv("PSI_PID",pid_str,1);
	}
	if (stat(task->argv[0], &sb) == -1
	    || ((sb.st_mode & S_IFMT) != S_IFREG)
	    || !(sb.st_mode & S_IEXEC)){
	    char* errstr;
	    errstr=strerror(errno);
	    syslog(LOG_ERR,"PSID_taskspawn(stat): [%d] %s :%s  %s %s",
		   errno, errstr ? errstr : "UNKNOWN",
		   task->argv[0] ? task->argv[0] : "",
		   ((sb.st_mode & S_IFMT) != S_IFREG) ? "S_IFREG error" : "S_IFREG ok",
		   (sb.st_mode & S_IEXEC) ? "S_IEXEC set" : "S_IEXEC error");
	    buf = errno;
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

	/* we don't need them any more */
	close(stdin_fileno_backup);
	close(stdout_fileno_backup);
	close(stderr_fileno_backup);

	/*
	 * execute the image
	 */
	if (PSID_execv(task->argv[0],&(task->argv[0]))<0){
	    char* errstr;
	    errstr = strerror(errno);
	    openlog("psid spawned process", LOG_PID|LOG_CONS, LOG_DAEMON);
	    PSI_setoption(PSP_OSYSLOG, 1);
	    syslog(LOG_ERR, "PSID_taskspawn(execv): [%d] %s",
		   errno, errstr ? errstr : "UNKNOWN");
	    perror("exec");
	}
	/*
	 * never reached, if execv succesful
	 */
	/*
	 * send the parent a sign that the exec wasn't successful
	 * fds[0] would have been closed on successful exec.
	 */
	buf = errno;
	write(fds[1],&buf,sizeof(buf));
	exit(0);
    }
    /*
     * this is the parent process
     */
    /*
     * check if fork() was successful
     */
    if (pid ==-1){
	char *errstr;
	errstr = strerror(errno);

	close(fds[0]);
	close(fds[1]);
	syslog(LOG_ERR, "PSID_taskspawn(fork): [%d] %s", errno,
	       errstr ? errstr : "UNKNOWN");
	perror("fork()");
	task->error = -errno;
	ret = -errno;
    }else{
	/*
	 * check for a sign of the child
	 */
	if(PSP_DEBUGTASK & PSI_debugmask){
	    snprintf(errtxt, sizeof(errtxt),
		     "I'm the parent. I'm waiting for my child (%d)\n", pid);
	    PSI_logerror(errtxt);
	}

	close(fds[1]);

    restart:
	if ((ret=read(fds[0], &buf, sizeof(buf))) < 0) {
	    if (errno == EINTR) {
		goto restart;
	    }
	}

	if(ret == 0){
	    /*
	     * the control channel was closed in case of a successful execv
	     */
	    ret = 0;
	    task->error = 0;
	    task->tid = PSI_gettid(-1,pid);
	    task->nodeno = PSI_getnode(-1);
#if defined(DEBUG)||defined(PSID)
	    if(PSP_DEBUGTASK & PSI_debugmask){
		snprintf(errtxt, sizeof(errtxt), "child execute was successful\n");
		PSI_logerror(errtxt);
	    }
#endif
	}else{
	    char *errstr;

	    /*
	     * the child sent us a sign that the execv wasn't successful
	     */
	    ret = buf;
	    errstr = strerror(ret);
#if defined(DEBUG)||defined(PSID)
	    /*	    if(PSP_DEBUGTASK & PSI_debugmask)
	     */
	    {
		snprintf(errtxt, sizeof(errtxt),
			 "child execute failed error(%d):%s\n", ret,
			 errstr ? errstr : "UNKNOWN");
		PSI_logerror(errtxt);
	    }
#endif
	}
	close(fds[0]);
    }
    return ret;
}

int PSID_inserthost(unsigned int addr, unsigned short psino)
{
    unsigned int hostno;
    struct PSID_host_t *host;

    hostno = ntohl(addr) & 0xff;

    for (host = PSID_hosts[hostno];  host; host = host->next){
	if (host->saddr == addr){
	    host->psino = psino;
	    return 1;
	}
    }
    if ((host = (struct PSID_host_t*) malloc(sizeof(struct PSID_host_t)))){
	host->saddr = addr;
	host->psino = psino;
	host->next = PSID_hosts[hostno];
	PSID_hosts[hostno] = host;
	PSID_hostaddresses[psino] = addr;
#ifdef DEBUG
	if((PSP_DEBUGADMIN|PSP_DEBUGSTARTUP) & PSI_debugmask){
	    snprintf(errtxt, sizeof(errtxt), "PSID_inserthost(): the host (address[%x],"
		    "cardid[%d]) is inserted in the hostlist.\n", addr, psino);
	    PSI_logerror(errtxt);
	}
#endif
	return 1;
    }

    return 0;
}

/***************************************************************************
 *      PSID_host()
 *
 *      RETURN  psi node number of the host with INET id addr, 
 *              -1 if addr is not registered.
 */
int PSID_host(unsigned int addr)
{
    unsigned int hostno;
    struct PSID_host_t *host;
#if defined(DEBUG)
    if(PSP_DEBUGHOST & PSI_debugmask ){
	snprintf(errtxt, sizeof(errtxt), "PSID_host(%x) \n", addr);
	PSI_logerror(errtxt);
    }
#endif
    /* loopback address */
    if ((ntohl(addr) >> 24 ) == 0x7F)
	return PSI_myid;

    /* other addresses */
    hostno = ntohl(addr) & 0xFF;
    for (host = PSID_hosts[hostno]; host; host = host->next){
	if (host->saddr == addr)
	    return host->psino ;
    }

    return -1;
}

/***************************************************************************
 *      PSID_hostaddress()
 *
 *      LIMIT   the id must be valid.
 *      RETURN  the INET id addr for the host with psi node no id, 
 *              -1 if addr is not registered.
 */
unsigned int PSID_hostaddress(unsigned short id)
{
#if defined(DEBUG)
    if(PSP_DEBUGHOST & PSI_debugmask){
	snprintf(errtxt, sizeof(errtxt), "PSID_hostaddress(%d) = %x\n",
		 id, (int) PSID_hostaddresses[id]);
	PSI_logerror(errtxt);
    }
#endif

    return PSID_hostaddresses[id];
}
