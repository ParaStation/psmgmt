/*
 *               ParaStation3
 * parse.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: parse.c,v 1.10 2002/01/16 17:07:30 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: parse.c,v 1.10 2002/01/16 17:07:30 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "psi.h"

#include "parse.h"

void yyparse(void);

char acthostname[80];
int nodesfound=0;
extern FILE *yyin;
extern int lineno;

char *Configfile = NULL;

char ConfigLicensekey[100];
char ConfigModule[100];
char ConfigRoutefile[100];
char ConfigInstDir[255];
int MyPsiId=-1;
unsigned int MyId=-1;

int NrOfNodes = -1;

int ConfigSmallPacketSize=-1;
int ConfigResendTimeout=-1;
int ConfigRLimitDataSize=-1;
int ConfigSyslogLevel=10;          /* default max. syslog level */
int ConfigSyslog=LOG_DAEMON;
int ConfigMgroup=237;
long ConfigPsidSelectTime=-1;
long ConfigDeclareDeadInterval=-1;

struct psihosttable *psihosttable = NULL;
char **hosttable = NULL;           /* to store hostnames */

static int usesyslog=0;
static char errtxt[255];

#define ERR_OUT(msg) if(usesyslog)syslog(LOG_ERR,msg);else fprintf(stderr,msg);


unsigned int GetIP(char *s)
{
    struct hostent *host;
    unsigned int id;
    char *msg;

    if((host = gethostbyname(s))==0){
	switch (h_errno) {
	case HOST_NOT_FOUND:
	    msg = "[HOST_NOT_FOUND]";
	    break;
	case NO_ADDRESS:
	    msg = "[No Internet address found]";
	    break;
	case NO_RECOVERY:
	    msg = "[NO_RECOVERY]";
	    break;
	case TRY_AGAIN:
	    msg = "[TRY_AGAIN]";
	    break;
	default:
	    msg = "[unknown error]";
	}
	snprintf(errtxt, sizeof(errtxt),
		 "FAILURE: Unable to lookup host <%s> %s", s, msg);
	ERR_OUT(errtxt);
	exit(-1);
    }
    memcpy(&id, host->h_addr_list[0], host->h_length);

    return id;
}

void setnrofnodes(int n)
{
    int i;

    if (NrOfNodes!=-1){ /* NrOfNodes already defined */
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR(Line %d): You have to define NrOfNodes only once\n",
		 lineno);
	ERR_OUT(errtxt);
	exit(-1);
    }
    NrOfNodes = n;

    hosttable = (char **) malloc((NrOfNodes+1)*sizeof(char *));

    psihosttable =
	(struct psihosttable *) malloc((NrOfNodes+1)
				       * sizeof(struct psihosttable));
    for(i=0; i<=NrOfNodes; i++){
        psihosttable[i].found = 0;
        psihosttable[i].inet = 0;
        psihosttable[i].name = NULL;
    }
}

int lookupHost(char *s)
{
    register int i;

    for(i=0;i<nodesfound;i++){
	if(!strcmp(hosttable[i],s)) return 1;
    }
    return 0;
}

void installhost(char *s,int n)
{
    int localid;
    int licserver;

    localid = GetIP(s);

    if (NrOfNodes==-1){ /* NrOfNodes not defined */
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR(Line %d): You have to define NrOfNodes before hosts\n",
		 lineno);
	ERR_OUT(errtxt);
	exit(-1);
    }
    if ((n>NrOfNodes) || (n<0)){ /* PSI-Id out of Range */
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR: PSI-Id <%d> out of range (NrOfNodes=%d)\n", n,
		 NrOfNodes);
	ERR_OUT(errtxt);
	exit(-1);
    }

    licserver=(n==NrOfNodes);

    if (!licserver && lookupHost(s)){ /* duplicated hostname */
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR: duplicated hostname <%s> in config file\n", s);
	ERR_OUT(errtxt);
	exit(-1);
    }
    if (psihosttable[n].found){ /* duplicated PSI-ID */
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR: duplicated ID <%d> for host <%s>"
		 " and <%s> in config file\n", n, s, psihosttable[n].name);
	ERR_OUT(errtxt);
	exit(-1);
    }
/* sprintf(errtxt,"Installing host[%d] %s MyPsiID=%x\n",n,s,MyPsiId); */
/* ERR_OUT(errtxt); */
    /* install hostname */
    hosttable[nodesfound] = (char *)malloc(strlen(s)+1);
    strcpy(hosttable[nodesfound],s);
    psihosttable[n].found = 1; /* true */
    psihosttable[n].inet = localid;
    psihosttable[n].name = hosttable[nodesfound];
    if(!licserver)nodesfound++;
    if (nodesfound > NrOfNodes){ /* more hosts than nodes ??? */
	ERR_OUT("ERROR: NrOfNodes does not match number of hosts in list\n");
	exit(-1);
    }
    if (localid==MyId && MyPsiId==-1) MyPsiId=n;
/*   if(licserver) printf("LicServer is %s (ID=%d)\n",s,n); */
    return;
}

int parse_config(int syslogreq)
{
    char myname[255], *temp, emptyfilename[] = "--------";
    char ext[] = "/config/psm.config";
    int found;
    FILE *cfd;
    struct stat sbuf;

    strcpy(ConfigInstDir, emptyfilename);
    strcpy(ConfigModule, emptyfilename);
    strcpy(ConfigRoutefile, emptyfilename);

    usesyslog=syslogreq;

    /*
     * Set MyId to my own ID (needed for installhost())
     */
    gethostname(myname,255);
    MyId = GetIP(myname);
    if(!Configfile){
	char *tmpnam;
	tmpnam = PSI_LookupInstalldir();
	Configfile = (char *) malloc(strlen(tmpnam)+strlen(ext)+1);
	strcpy(Configfile, tmpnam);
	strcat(Configfile, ext);
    }

    if ( (cfd = fopen(Configfile,"r"))!=0){
	/* file found */
	snprintf(errtxt, sizeof(errtxt),
		 "Using <%s> as configuration file\n", Configfile);
	ERR_OUT(errtxt);
    }else{
	snprintf(errtxt, sizeof(errtxt),
		 "Unable to locate configuration file [%s]\n", Configfile);
	ERR_OUT(errtxt);
	return(-1);
    }

    /*
     * Start the parser
     */

    yyin = cfd;
    yyparse();

    fclose(cfd);

    /*
     * Sanity Checks
     */

    if (NrOfNodes==-1){
	ERR_OUT("ERROR: NrOfNodes not defined\n");
	exit(-1);
    }

    if(NrOfNodes>nodesfound){ /* hosts missing in hostlist */
	ERR_OUT("ERROR: Number of hosts in hostlist less than NrOfNodes\n");
	exit(-1);
    }

    if (strcmp(ConfigInstDir, emptyfilename)){
	/* ConfigInstDir set. Use this as Instdir */
	PSI_SetInstalldir(ConfigInstDir);
	if(strcmp(ConfigInstDir, PSI_LookupInstalldir())){
	    ERR_OUT("ERROR: InstDir defined but not correct:");
	    ERR_OUT(ConfigInstDir);
	    ERR_OUT(PSI_LookupInstalldir());
	    exit(-1);
	}
    }

    if (!strcmp(ConfigModule, emptyfilename)){
	ERR_OUT("ERROR: Module not defined\n");
	exit(-1);
    }

    found=0;
    if(ConfigModule[0] == '/'){
	if(stat(ConfigModule, &sbuf) != -1)
	    found=1;
    }else{
	temp = (char *) malloc(strlen(PSI_LookupInstalldir())
				+ strlen(ConfigModule) + 15);
	strcpy(temp, PSI_LookupInstalldir());
	strcat(temp, "/");
	strcat(temp, ConfigModule);
	if(stat(temp, &sbuf) != -1){
	    strcpy(ConfigModule, temp);
	    found=1;
	}else{
	    strcpy(temp, PSI_LookupInstalldir());
	    strcat(temp, "/bin/modules/");
	    strcat(temp, ConfigModule);
	    if(stat(temp, &sbuf) != -1){
		strcpy(ConfigModule, temp);
		found=1;
	    }
	}
	free(temp);
    }
    if(!found){
	ERR_OUT("ERROR: Module not found\n");
	exit(-1);
    }

    if (!strcmp(ConfigRoutefile, emptyfilename)){
	ERR_OUT("ERROR: Routefile not defined\n");
	exit(-1);
    }
    found=0;
    if(ConfigRoutefile[0] == '/'){
	if(stat(ConfigRoutefile, &sbuf) != -1)
	    found=1;
    }else{
	temp = (char *) malloc(strlen(PSI_LookupInstalldir())
			       + strlen(ConfigRoutefile) + 15);
	strcpy(temp, PSI_LookupInstalldir());
	strcat(temp, "/");
	strcat(temp, ConfigRoutefile);
	if(stat(temp, &sbuf) != -1){
	    strcpy(ConfigRoutefile, temp);
	    found=1;
	}else{
	    strcpy(temp, PSI_LookupInstalldir());
	    strcat(temp, "/config/");
	    strcat(temp, ConfigRoutefile);
	    if(stat(temp, &sbuf) != -1){
		strcpy(ConfigRoutefile, temp);
		found=1;
	    }
	}
	free(temp);
    }
    if(!found){
	ERR_OUT("ERROR: Routefile not found\n");
	exit(-1);
    }

    if(!psihosttable[NrOfNodes].found){ /* Check LicServer Setting */
	/*
	 * Set node 0 as default server
	 */
	psihosttable[NrOfNodes].found = 1;
	psihosttable[NrOfNodes].inet = psihosttable[0].inet;
	psihosttable[NrOfNodes].name = psihosttable[0].name;
	snprintf(errtxt, sizeof(errtxt),
		"Using %s (ID=%d) as Licenseserver\n",
		psihosttable[NrOfNodes].name,NrOfNodes);
	ERR_OUT(errtxt);
    }

    return 0;
}
