#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <pshal.h>

#include "license.h"

#include "psi.h"
#include "psilog.h"
#include "parse.h"
#include "logger.h"

#include "psidutil.h"

int PSID_CardPresent;

struct PSID_host_t *PSID_hosts[256];
unsigned long *PSID_hostaddresses = NULL;
char *PSID_hoststatus = NULL;

void PSID_initCluster(int nodenr, int nrofnodes, int syslog)
{}

void PSID_resetMCP(int syslog)
{}

void PSID_setupRouting(char *routingfile)
{}

void PSID_ReConfig(int nodenr,int nrofnodes, char *routingfile)
{
    SYSLOG(1,(LOG_ERR, "PSHAL_ReConfig(%d,%d,%c)"
	      " small packets %d, ResendTimeout %d\n",
	      nodenr, nrofnodes, routingfile,
	      ConfigSmallPacketSize,ConfigResendTimeout));
    PSID_initCluster(nodenr,nrofnodes,1); 
    PSID_resetMCP(1); 

    PSID_setupRouting(routingfile);
    PSID_initCluster(nodenr,nrofnodes,1);

    PSI_myid = nodenr;
    PSI_nrofnodes = nrofnodes;

//    PSHALSYS_SetSmallPacketSize(ConfigSmallPacketSize);
//    PSHALSYS_SetResendTimeout(ConfigResendTimeout);

    return;
}

/***************************************************************************
 *      PSID_checklicense()
 *
 */
int PSID_checklicense(unsigned int myIP)
{
    /* check the license key at Node 0 */
    unsigned int IP; 
    long nodes;
    unsigned long end=0;
    unsigned long start=0;
    long version;
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
    char* errtxt;

    int i;

    gethostname(myname,sizeof(myname));

    if(! (mhost = gethostbyname(myname))){
	endhostent(); 
	perror("Unable to lookup hostname");
	errtxt=strerror(errno);
	SYSLOG(0,(LOG_ERR,
		  "PSID_readconfigfile():Unable to lookup hostname: [%d] %s",
		  errno, errtxt?errtxt:"UNKNOWN errno"));
	exit(-1);
    }
    bcopy((char *)mhost->h_addr, (char*)&sin_addr, mhost->h_length); 
    endhostent(); 

    if(parse_config(1)<0)
	return -1;

    SYSLOG(9,(LOG_ERR,"after parseconfig(%s)::(%d,%d,%s)\n",
	      myname, MyPsiId, NrOfNodes, ConfigRoutefile));

    PSI_nrofnodes = NrOfNodes;

    if(PSI_nrofnodes > 4){
	/*
	 * Check the license key
	 * Clusters smaller than 4 nodes are free
	 */
	// PSID_checklicense(sin_addr.s_addr);
    }

    PSID_hoststatus = (char *) malloc(NrOfNodes * sizeof(char));
    PSID_hostaddresses = (unsigned long *) malloc(NrOfNodes
						  * sizeof(unsigned long));
    /*
     * check the PSID specific entries in the configfile
     * if (parameters !=-1)  use value of parameter
     * else if (PSIconfigvalue !=-1)  use value of ConfigValue
     * else  use value of #define
     */
    for(i=0; i<PSI_nrofnodes; i++){
	if (!(PSID_inserthost(psihosttable[i].inet,i))){
	    /* error: the host could not be inserted 
	       in the hostlist */
	    sprintf(PSI_txt,"PSID_readconfigfile: host (address[%x],id[%d])"
		    " could not be inserted in the hostlist.\n",
		    psihosttable[i].inet,i);
	    PSI_logerror(PSI_txt);
	}
    }

    if (PSID_host(sin_addr.s_addr)==-1){
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
    PSID_ReConfig(MyPsiId, NrOfNodes, ConfigRoutefile);
    SYSLOG(9,(LOG_ERR,"PSID_readconfigfile():PSID_ReConfig ok."));

    return PSID_CardPresent;
}

/***************************************************************************
 *       PSI_startlicensserver()
 *
 *       starts the licenseserver via the inetd
 */
int 
PSID_startlicenseserver(u_long hostaddr)
{
    int sock;
    struct servent *service;
    struct sockaddr_in sa;
#if defined(DEBUG)
    if(PSP_DEBUGADMIN & (PSI_debugmask )){
	sprintf(PSI_txt,"PSI_startlicenseserver(%lx)\n", hostaddr);
	PSI_logerror(PSI_txt);
    }
#endif
    /*
     * start the PSI Daemon via inetd
     */
    sock = socket(AF_INET,SOCK_STREAM,0);

    if ((service = getservbyname("psld","tcp")) == NULL){ 
	sprintf(PSI_txt,
		"StartlicenseServer(): can't get \"psld\" service entry\n"); 
	fprintf(stderr, PSI_txt);
	PSI_logerror(PSI_txt);
	shutdown(sock,2);
	close(sock);
	return 0; 
    }
    bzero((char *)&sa, sizeof(sa)); 
    sa.sin_family = AF_INET; 
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port = service->s_port;
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0){ 
	perror("StartlicenseServer(): Connect to port for start with inetd failed."); 
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
	sprintf(PSI_txt,"PStask_spawn(task: ");
	PStask_sprintf(PSI_txt+strlen(PSI_txt),task);
	sprintf(PSI_txt+strlen(PSI_txt),")\n");
	PSI_logerror(PSI_txt);
    }
#endif

    /*
     * create a control channel
     * for observing the successful exec call
     */
    if(pipe(fds)<0){
	char* errtxt;
	errtxt=strerror(errno);
	syslog(LOG_ERR, "PStask_spawn(pipe): [#%d] %s ", errno,
	       errtxt?errtxt:"UNKNOWN");
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
	    char* errtxt;
	    errtxt=strerror(errno);

	    syslog(LOG_ERR, "PStask_spawn(setgid): [%d] %s", errno,
		   errtxt?errtxt:"UNKNOWN");
	    perror("setgid");
	    buf = errno;
	    write(fds[1],&buf,sizeof(buf));
	    exit(0);
	}

	/*
	 * change the user id to the appropriate user
	 */
	if(setuid(task->uid)<0){
	    char* errtxt;
	    errtxt=strerror(errno);

	    syslog(LOG_ERR, "PStask_spawn(setuid): [%d] %s", errno,
		   errtxt?errtxt:"UNKNOWN");
	    perror("setuid");
	    buf = errno;
	    write(fds[1],&buf,sizeof(buf));
	    exit(0);
	}

	/*
	 * change to the appropriate directory
	 */
	if(chdir(task->workingdir)<0){
	    char* errtxt;
	    errtxt=strerror(errno);
	    syslog(LOG_ERR, "PStask_spawn(chdir): %d %s :%s", errno,
		   errtxt?errtxt:"UNKNOWN",
		   task->workingdir?task->workingdir:"");
	    perror("chdir");
	    buf = errno;
	    write(fds[1],&buf,sizeof(buf));
	    exit(0);
	}
	/*
	 * set the environment variable
	 */
	{
	    char envvar[200];
	    sprintf(envvar,"PWD=%s",task->workingdir);
	    putenv(envvar);
	    for(i=0;i<task->environc;i++)
		if(task->environ[i])
		    putenv(task->environ[i]);
	}

	if (stat(task->argv[0], &sb) == -1
	    || ((sb.st_mode & S_IFMT) != S_IFREG)
	    || !(sb.st_mode & S_IEXEC)){
	    char* errtxt;
	    errtxt=strerror(errno);
	    syslog(LOG_ERR,"PStask_spawn(stat): [%d] %s :%s  %s %s",
		   errno, errtxt?errtxt:"UNKNOWN",
		   task->argv[0]?task->argv[0]:"",
		   ((sb.st_mode & S_IFMT) != S_IFREG)?"S_IFREG error":"S_IFREG ok",
		   (sb.st_mode & S_IEXEC)?"S_IEXEC set":"S_IEXEC error");
	    buf = errno;
	    write(fds[1],&buf,sizeof(buf));
	    exit(0);
	}

	if(LOGGERredirect_std(task->loggernode, task->loggerport, task)<0){
	    buf = errno;
	    write(fds[1],&buf,sizeof(buf));
	    exit(0);
	}
	/*
	 * close all file descriptors
	 * except my control channel to my parent
	 */
	for (i=getdtablesize()-1;i>2;i--)
	    if (i!=fds[1])
		close(i);

	/*
	 * execute the image
	 */

	if (execv(task->argv[0],&(task->argv[0]))<0){
	    char* errtxt;
	    errtxt=strerror(errno);
	    openlog("psid spawned process",LOG_PID|LOG_CONS,LOG_DAEMON);
	    PSI_setoption(PSP_OSYSLOG,1);
	    syslog(LOG_ERR,"PStask_spawn(execv): [%d] %s",
		   errno,errtxt?errtxt:"UNKNOWN");
	    perror("exec");
	}
	/*
	 * never reached, if execv succesful
	 */
	/*
	 * send the parent a sign that the exec wasn't successful
	 * fds[0] would have been closed on successful exec.
	 */
	buf= errno;
	write(fds[1],&buf,sizeof(buf));
	exit(0);
    }
    /*
     * this is the parent process
     */
    /*
     * check if sys_fork() was successful
     */
    if (pid ==-1){
	char* errtxt;
	errtxt=strerror(errno);

	close(fds[0]);
	close(fds[1]);
	syslog(LOG_ERR, "PStask_spawn(fork): [%d] %s", errno,
	       errtxt?errtxt:"UNKNOWN");
	perror("fork()");
	task->error = -errno;
	ret= -errno;
    }else{
	/*
	 * check for a sign of the child
	 */
	/*#if defined(DEBUG)||defined(PSID)*/
	if(PSP_DEBUGTASK & PSI_debugmask){
	    printf(PSI_txt, "I'm the parent. I'm waiting for my child (%d)\n",
		   pid);
	    PSI_logerror(PSI_txt);
	}
	/*#endif */

	close(fds[1]);
	if(read(fds[0],&buf,sizeof(buf))==0){
	    /*
	     * the control channel was closed in case of a successful execv
	     */
	    ret = 0;
	    task->error = 0;
	    task->tid = PSI_gettid(-1,pid);
	    task->nodeno = PSI_getnode(-1);
#if defined(DEBUG)||defined(PSID)
	    if(PSP_DEBUGTASK & PSI_debugmask){
		printf(PSI_txt,"child execute was successful\n");
		PSI_logerror(PSI_txt);
	    }
#endif
	}else{
	    char* errtxt;

	    /*
	     * the child sent us a sign that the execv wasn't successful
	     */
	    ret = buf;
	    errtxt=strerror(ret);
#if defined(DEBUG)||defined(PSID)
	    /*	    if(PSP_DEBUGTASK & PSI_debugmask)
	     */
	    {
		printf(PSI_txt, "child execute failed error(%d):%s\n", ret,
		       errtxt?errtxt:"UNKNOWN");
		PSI_logerror(PSI_txt);
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

#ifdef BIGENDIAN
    hostno = addr & 0xFF;
#else   
    hostno = addr>>24;
#endif
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
	    sprintf(PSI_txt,"PSID_inserthost(): the host (address[%x],"
		    "cardid[%d]) is inserted in the hostlist.\n", addr, psino);
	    PSI_logerror(PSI_txt);
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
	sprintf(PSI_txt,"PSID_host(%x) \n", addr);
	PSI_logerror(PSI_txt);
    }
#endif
    /* loopback address */
#ifdef BIGENDIAN
    if ((addr >> 24 ) == 0x7F)
#else
    if ((addr & 0xFF) == 0x7F)
#endif
	return PSI_myid;

    /* other addresses */
#ifdef BIGENDIAN
    hostno = addr & 0xFF;
#else
    hostno = addr >> 24;
#endif
    for (host = PSID_hosts[hostno]; host; host = host->next){
#if defined(DEBUG)
	if(PSP_DEBUGHOST & PSI_debugmask ){
	    sprintf(PSI_txt,"PSID_host(%x): 0x%lx \n", addr, host);
	    PSI_logerror(PSI_txt);
	}
#endif
	if (host->saddr == addr)
	    return host->psino ;
    }

    return -1;
}

/***************************************************************************
 *      SHM_hostaddress()
 *
 *      LIMIT   the id must be valid.
 *      RETURN  the INET id addr for the host with psi node no id, 
 *              -1 if addr is not registered.
 */
unsigned long PSID_hostaddress(unsigned short id)
{
#if defined(DEBUG)
    if(PSP_DEBUGHOST & PSI_debugmask){
	sprintf(PSI_txt,"PSID_hostaddress(%d) = %x\n",
		id, (int) PSID_hostaddresses[id]);
	PSI_logerror(PSI_txt);
    }
#endif

    return PSID_hostaddresses[id];
}
