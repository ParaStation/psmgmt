/*
 * ParaStation License Deamon
 *
 * (C) 2000 ParTec AG Karlsruhe
 *     written by Dr. Thomas M. Warschko
 *
 * 23-03-00 V1.0: initial implementation
 *
 *
 *
 *
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include <netdb.h>

#include "rdp.h"
#include "../psid/parse.h"

int syslogerror = 0;    /* flag if syslog is used */

char statarray[256];
int display=0;

void initstat(void)
{
    int i;
    for(i=0;i<256;i++) statarray[i]='_';
}

void printstatus(int nodes)
{
    static int loop=0;
    int i;
    RDP_ConInfo info;

    for(i=0;i<=nodes;i++){
	RDP_GetInfo(i,&info);
	if(info.misscounter==0){
	    statarray[i]='*';
	} else {
	    statarray[i]='X';
	    if(info.misscounter<10) statarray[i]='x';
	}
    }
    if(display){
	printf("%6d:%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c"
	       "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c"
	       "%c%c%c%c%c%c%c\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
	       "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
	       "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
	       "\b\b\b\b",
	       loop,
	       statarray[0], statarray[1], statarray[2], statarray[3],
	       statarray[4], statarray[5], statarray[6], statarray[7],
	       statarray[8], statarray[9], statarray[10],statarray[11],
	       statarray[12],statarray[13],statarray[14],statarray[15],
	       statarray[16],statarray[17],statarray[18],statarray[19],
	       statarray[20],statarray[21],statarray[22],statarray[23],
	       statarray[24],statarray[25],statarray[26],statarray[27],
	       statarray[28],statarray[29],statarray[30],statarray[31],
	       statarray[32],statarray[33],statarray[34],statarray[35],
	       statarray[36],statarray[37],statarray[38],statarray[39],
	       statarray[40],statarray[41],statarray[42],statarray[43],
	       statarray[44],statarray[45],statarray[46],statarray[47],
	       statarray[48],statarray[49],statarray[50],statarray[51],
	       statarray[52],statarray[53],statarray[54],statarray[55],
	       statarray[56],statarray[57],statarray[58],statarray[59],
	       statarray[60],statarray[61],statarray[62],statarray[63],
	       statarray[64]);
	fflush(stdout);
    }

    loop++;
    return;
}

/*
 * The following procedures are usually defined in config/routing.c but 
 * NOT needed by the license server (thus overwritten by dummies)
 */
int get_max_configno(void){ return 100; }
int check_config(int a, int b){ return 0; }

extern int NrOfNodes;
extern int ConfigMgroup;
extern int ConfigSyslog;
extern char ConfigLicensekey[];

static char errtxt[255];

#define ERR_OUT(msg)	if(usesyslog)syslog(LOG_ERR,"PSLD: %s\n",msg);\
			else fprintf(stderr,"%s\n",msg);

void IpNodesEndFromLicense(char* licensekey, unsigned int* IP, long* nodes,
			   unsigned long* start, unsigned long* end,
			   long* version);

unsigned int LicIP;

typedef struct iflist_t{
    char name [20];
    int len,family;
    unsigned int ipaddr;
    unsigned char mac_addr[6];
} iflist_t;
static iflist_t iflist[20];
static int if_found=0;


int check_machine(int usesyslog, int *interface)
{
    char host[80];
    int numreqs = 30;
    struct ifconf ifc;
    struct ifreq ifrb;
    struct ifreq *ifr;
    int n, i,ipfound,netfound;
    int skfd;

    skfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);  /* allocate a socket */
    if(skfd<0){
	sprintf(errtxt,"Unable to obtain socket");
	ERR_OUT(errtxt);
	return 1;
    }

    ifc.ifc_buf = NULL;
    ifc.ifc_len = sizeof(struct ifreq) * numreqs;
    ifc.ifc_buf = malloc(ifc.ifc_len);
    if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
	sprintf(errtxt,"Unable to obtain network configuration");
	ERR_OUT(errtxt);
	return 1;
    }

    ifr = ifc.ifc_req;
    for (n = 0, i=0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
	if(ifr->ifr_dstaddr.sa_family == PF_INET){
	    strcpy(iflist[i].name ,ifr->ifr_name);
	    strcpy(ifrb.ifr_name ,ifr->ifr_name);
	    iflist[i].ipaddr = *(unsigned int*)&ifr->ifr_dstaddr.sa_data[2];
#ifdef __linux
	    if (ioctl(skfd, SIOCGIFHWADDR, &ifrb) < 0) {
		sprintf(errtxt,
			"Unable to obtain interface address for interface %s",
			ifr->ifr_name);
		ERR_OUT(errtxt);
		return 1;
	    }else{
		bcopy(ifrb.ifr_hwaddr.sa_data, iflist[i].mac_addr, 6);
	    }
#else
	    bzero(iflist[i].mac_addr, 6);
#endif
	    sprintf(errtxt,"Interface found: %s, IP=%8x,"
		    " addr=%02x:%02x:%02x:%02x:%02x:%02x",
		    iflist[i].name, iflist[i].ipaddr,
		    iflist[i].mac_addr[0], iflist[i].mac_addr[1],
		    iflist[i].mac_addr[2], iflist[i].mac_addr[3],
		    iflist[i].mac_addr[4], iflist[i].mac_addr[5]);
	    ERR_OUT(errtxt);
	    i++;
	}
	ifr++;
    }
    if_found = i;

    LicIP = psihosttable[NrOfNodes].inet;
    sprintf(errtxt,"LicIP is %x [%d interfaces]",LicIP,if_found);
    ERR_OUT(errtxt);

    ipfound=0;
    netfound=0;
    i=0;
    while(i<if_found){
	struct in_addr iaddr1, iaddr2;
	if(!ipfound) ipfound = (LicIP == iflist[i].ipaddr);
	iaddr1.s_addr = iflist[i].ipaddr;
	iaddr2.s_addr = psihosttable[0].inet;
/* printf("checking %x [%x] vs. [%x] %x\n",iflist[i].ipaddr,inet_netof(iaddr1), */
/* 	psihosttable[0].inet,inet_netof(iaddr2)); */
	if (!netfound && inet_netof(iaddr1) == inet_netof(iaddr2)){
	    sprintf(errtxt,"Using %x as multicast interface",iflist[i].ipaddr);
	    ERR_OUT(errtxt);
	    netfound=1;
	    *interface=i;
	}
	i++;
    }

    gethostname(host,80);
    if(!ipfound){
	sprintf(errtxt,
		"Machine %s not configured as LicenseServer [Server is %s]", 
		host, psihosttable[NrOfNodes].name);
	ERR_OUT(errtxt);
	return 1;
    }

    return 0;
}

int check_license(int usesyslog)
{
    char host[80];
    unsigned int IP;
    long nodes;
    unsigned long start=0;
    unsigned long end=0;
    long version;
    unsigned long now;  
    int ipfound,i;

    IpNodesEndFromLicense(ConfigLicensekey, &IP, &nodes, &start, &end,
			  &version);
    now = time(NULL);

    sprintf(errtxt,"LIC-INFO: IP=%x, node=%ld, start=%lx, now=%lx, end=%lx,"
	    " version=%ld\n",
	    IP,nodes,start,now,end,version);
/*   ERR_OUT(errtxt); */

    if(NrOfNodes<=4) return 1; /* 4 nodes are for free */

    if(start+end == 0){	/* Illegal Key (wrong checksum) */
	sprintf(errtxt,"Invalid License Key");
	ERR_OUT(errtxt);
	return 0;
    }

    if(now<start) {   /* License is no more valid */
	sprintf(errtxt,"License out of date: check clock setting");
	ERR_OUT(errtxt);
	return 0;
    }
    if(end<now) {   /* License is no more valid */
	sprintf(errtxt,"License out of date (end=%lx, now=%lx)",end,now);
	ERR_OUT(errtxt);
	return 0;
    }
    if(nodes < NrOfNodes){ /* more nodes than in license */
	sprintf(errtxt,"License not valid for this number of nodes");
	ERR_OUT(errtxt);
	return 0;
    }

    ipfound=0,i=0;
    while(i<if_found && !ipfound){
	ipfound = (IP == iflist[i].ipaddr);
	i++;
    }

    gethostname(host,80);
    if(!ipfound){
	sprintf(errtxt,
		"LicenseKey does not match current LicenseServer [%s:%s]", 
		host, psihosttable[NrOfNodes].name);
	ERR_OUT(errtxt);
	return 1;
    }

    return 1;
}

#define PIDFILE "/var/run/psld.pid"

int check_lock(int usesyslog)
{
    FILE *f;
    int fd;
    int fpid=-1,mypid=-1;

    mypid=getpid();
    if (!(f=fopen(PIDFILE,"r"))){
	fpid=0;
    }else{
	fscanf(f,"%d", &fpid);
	fclose(f);
    }

/*   sprintf(errtxt, "FPID is %d, MYPID is %d", fpid, mypid); */
/*   ERR_OUT(errtxt); */

    /* Amazing ! _I_ am already holding the pid file... */
    if (fpid == mypid) return mypid;

    /*
     * The 'standard' method of doing this is to try and do a 'fake' kill
     * of the process.  If an ESRCH error is returned the process cannot
     * be found -- GW
     */
    /* But... errno is usually changed only on error.. */
    if (fpid){
	if (kill(fpid, 0)==-1){
	    if (errno == ESRCH){ /* old pid file */
		sprintf(errtxt, "old PID File");
	    }else{
		sprintf(errtxt, "strange error");
		return 0; /* psld already running */
	    }
	}else{
	    sprintf(errtxt, "process still running");
	    return 0; /* psld already running */
	}
    }

/*   ERR_OUT(errtxt); */

    if(((fd = open(PIDFILE, O_RDWR|O_CREAT, 0644)) == -1)
       || ((f = fdopen(fd, "r+")) == NULL) ){
	sprintf(errtxt, "Can't open or create %s.\n", PIDFILE);
	ERR_OUT(errtxt);
	return 0;
    }else{
	fprintf(f,"%d\n", mypid);
	fclose(f);
    }

    return mypid;
}

void sighandler(int sig)
{
    switch (sig){
    default:
	unlink(PIDFILE);
	exit(0);
	break;
    }
}

char *PSHAL_LookupInstalldir(void);

int main(int argc, char *argv[])
{
    int c,errflg=0,dofork=1;
    int msock,usesyslog=1;
    int interface;
    struct timeval tv;

#define DARGS "Ddf:"

    optarg = NULL;
    while (!errflg && ((c = getopt(argc,argv, DARGS)) != -1)){
	switch (c) {
	case 'd':
	    display=1;
	    dofork=0;
	    break;
	case 'D':
	    RDP_SetDBGLevel(10);
	    display=0;
	    dofork=0;
	    usesyslog=0;
	    break;
	case 'f' :
	    Configfile = strdup( optarg );
	    break;
	default:
	    errflg++;
	    break;
	}
    }

    openlog("psld",LOG_PID,LOG_DAEMON);

    if(errflg){
	sprintf(errtxt,"usage: %s [-Dd] [-f configfile]\n",argv[0]);
	ERR_OUT(errtxt);
	exit(-1);
    }

    if(dofork){ /* Start as daemon */
	switch(c = fork()){
	case -1: 
	    sprintf(errtxt,"unable to fork server process\n");
	    ERR_OUT(errtxt);
	    return(-1);
	    break;
	case 0: /* I'm the child (and running further) */
	    break;
	default: /* I'm the parent and exiting */
	    return(0);
	    break;
	}
    }

    if(!check_lock(usesyslog)){
	sprintf(errtxt,"PSLD already running\n");
	ERR_OUT(errtxt);
	exit(-1);
    }

    if(parse_config(usesyslog)<0){
	return(-1);
    }

    if(check_machine(usesyslog,&interface)){
	closelog();
	return(-1);
    }

    closelog();
    openlog("psld",LOG_PID,ConfigSyslog);

    signal(SIGHUP,sighandler);
    signal(SIGTERM,sighandler);
    signal(SIGINT,sighandler);

//    if(check_license(usesyslog)){

	RDP_SetLogMsg(1);
	msock = RDPMCASTinit(NrOfNodes, ConfigMgroup, iflist[interface].name,
			     iflist[interface].ipaddr, usesyslog,NULL);

	initstat();
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	while(1){
	    Mselect(0,NULL,NULL,NULL,&tv);
	    if(display)printstatus(NrOfNodes);
	}
//  }

    return 0;
}
