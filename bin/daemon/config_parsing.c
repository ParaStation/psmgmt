/*
 *               ParaStation3
 * parser.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: config_parsing.c,v 1.1 2002/06/13 14:31:56 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: config_parsing.c,v 1.1 2002/06/13 14:31:56 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#include <netdb.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "psi.h"
#include "parser.h"
#include "psidutil.h"

#include "config_parsing.h"

int nodesfound = 0;

struct host_t *hosts[256];

struct node_t *nodes = NULL;

char *Configfile = NULL;

char *ConfigLicensekey = NULL;
char *ConfigModule = NULL;
char *ConfigRoutefile = NULL;
char *ConfigInstDir = NULL;
int MyPsiId = -1;

int NrOfNodes = -1;

int ConfigSmallPacketSize = -1;
int ConfigRTO = -1;
int ConfigHNPend = -1;
int ConfigAckPend = -1;

long ConfigSelectTime = -1;
long ConfigDeadInterval = -1;
int ConfigRDPPort = 886;
int ConfigMCastGroup = 237;
int ConfigMCastPort = 1889;

int ConfigRLimitCPUTime = -1;
int ConfigRLimitDataSize = -1;
int ConfigRLimitStackSize = -1;
int ConfigRLimitRSSSize = -1;

int ConfigSyslogLevel = 10;          /* default max. syslog level */
int ConfigSyslog = LOG_DAEMON;

static char errtxt[256];

static int usesyslog = 0;

/*----------------------------------------------------------------------*/
/* Helper function to manage nodes and hosts list */
static int allocHosts(int num)
{
    int i;

    if (nodes) free(nodes); /* @todo nodes allready allocated. Error? */

    nodes = malloc(sizeof(*nodes) * (num+1));

    if (!nodes) {
	snprintf(errtxt, sizeof(errtxt), "Cannot allocate memory for %d nodes",
		 num + 1);
	parser_comment(errtxt, 0);
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "Allocate memory for %d nodes", num + 1);
    parser_comment(errtxt, 10);

    for (i=0; i<=NrOfNodes; i++) {
        nodes[i].addr = INADDR_ANY;
	nodes[i].status = 0;
        nodes[i].hwtype = none;
	nodes[i].ip = 0;
	nodes[i].starter = 0;
        nodes[i].tasklist = NULL;
    }

    return 0;
}

static int installHost(unsigned int ipaddr, int id,
		       HWType hwtype, int ip, int starter)
{
    int licserver;
    unsigned int hostno;
    struct host_t *host;

    if (NrOfNodes==-1) { /* NrOfNodes not defined */
	parser_comment("You have to define NrOfNodes before any host", 0);
	return -1;
    }

    if ((id<0) || (id>NrOfNodes)) { /* id out of Range */
	snprintf(errtxt, sizeof(errtxt),
		 "node ID <%d> out of range (NrOfNodes = %d)", id, NrOfNodes);
	parser_comment(errtxt, 0);
	return -1;
    }

    licserver=(id==NrOfNodes);

    if (!licserver && parser_lookupHost(ipaddr)){ /* duplicated host */
	snprintf(errtxt, sizeof(errtxt),
		 "duplicated host <%s> in config file",
		 inet_ntoa(* (struct in_addr *) &ipaddr));
	parser_comment(errtxt, 0);
	return -1;
    }

    if (nodes[id].addr != INADDR_ANY) { /* duplicated PSI-ID */
	snprintf(errtxt, sizeof(errtxt),
		 "duplicated ID <%d> for hosts <%s> and <%s> in config file",
		 id, inet_ntoa(* (struct in_addr *) &ipaddr),
		 inet_ntoa(* (struct in_addr *) &nodes[id].addr));
	parser_comment(errtxt, 0);
	return -1;
    }

    /* install hostname */
    nodes[id].addr = ipaddr;
    nodes[id].hwtype = hwtype;
    nodes[id].ip = ip;
    nodes[id].starter = starter;

    if (!licserver) nodesfound++;

    if (nodesfound > NrOfNodes) { /* more hosts than nodes ??? */
	snprintf(errtxt, sizeof(errtxt),
		 "NrOfNodes = %d does not match number of hosts in list (%d)",
		 NrOfNodes, nodesfound);
	parser_comment(errtxt, 0);
	return -1;
    }

    hostno = ntohl(ipaddr) & 0xff;

    for (host = hosts[hostno]; host; host = host->next) {
	if (host->addr == ipaddr) { /* host already configured ?? */
	    snprintf(errtxt, sizeof(errtxt),
		     "Host <%s> already configured (id=%d)",
		     inet_ntoa(* (struct in_addr *) &ipaddr), host->id);
	    parser_comment(errtxt, 0);
	    return -1;
	}
    }

    if (!(host = (struct host_t*) malloc(sizeof(struct host_t)))) {
	return -1;
    }

    host->addr = ipaddr;
    host->id = id;
    host->next = hosts[hostno];
    hosts[hostno] = host;

    snprintf(errtxt, sizeof(errtxt), "installHost():"
	     " host <%s> is inserted in the hostlist with id=%d.",
	     inet_ntoa(* (struct in_addr *) &ipaddr), id);
    parser_comment(errtxt, 10);

    return 0;
}

int parser_lookupHost(unsigned int ipaddr)
{
    unsigned int hostno;
    struct host_t *host;

    /* loopback address */
    if ((ntohl(ipaddr) >> 24 ) == 0x7F)
	return PSI_myid;

    /* other addresses */
    hostno = ntohl(ipaddr) & 0xFF;
    for (host = hosts[hostno]; host; host = host->next) {
	if (host->addr == ipaddr) {
	    if (parser_getDebugLevel() >= 10) {
		snprintf(errtxt, sizeof(errtxt),
			 "PSID_lookupHost(): host <%s> has id=%d.",
			 inet_ntoa(* (struct in_addr *)ipaddr), host->id);
		parser_comment(errtxt, 10);
	    }
	    return host->id;
	}
    }

    return -1;
}

/*----------------------------------------------------------------------*/

/*
 * Worker routines to set various variables from the configuration file
 */
static int getInstDir(char *token)
{
    char *dname;
    struct stat fstat;

    dname = parser_getString();
    /* test if dir is a valid directory */
    if (stat(dname, &fstat)) {
	perror(dname);
	return -1;
    }

    if (!S_ISDIR(fstat.st_mode)) {
	printf("'%s' is not a directory", dname);
	return -1;
    }

    PSI_SetInstalldir(dname);
    if (strcmp(dname, PSI_LookupInstalldir())) {
	fprintf(stderr, "'%s' seems to be a invalid installdir\n", dname);
	return -1;
    }

    return 0;
}

static int getNumNodes(char *token)
{
    int i, ret;

    if (NrOfNodes != -1) {
	/* NrOfNodes already defined */
	parser_comment("You have to define NrOfNodes only once", 0);

	return -1;
    }

    ret = parser_getNumValue(parser_getString(),
			     &NrOfNodes, "number of nodes");

    if (ret) return ret;

    /* Allocate the corresponding hosttable */
    return allocHosts(NrOfNodes);
}

static int getModuleName(char *token)
{
    char *file;

    file = parser_getFilename(parser_getString(), PSI_LookupInstalldir(),
			      "/bin/module");

    if (!file) {
	printf("Invalid module name\n");
	return -1;
    }

    if (ConfigModule) free(ConfigModule);

    ConfigModule = strdup(file);

    return 0;
}

static int getRouteFile(char *token)
{
    char *file = parser_getFilename(parser_getString(), PSI_LookupInstalldir(),
				   "/config");

    if (!file) {
	printf("Invalid routefile\n");
	return -1;
    }

    if (ConfigRoutefile) free(ConfigRoutefile);

    ConfigRoutefile = strdup(file);

    return 0;
}

static int getLicServer(char *token)
{
    char *hname;
    unsigned int ipaddr;

    hname = parser_getString();

    if (!hname) {
	printf("Invalid license server\n");
	return -1;
    }

    ipaddr = parser_getHostname(hname);
    
    if (!ipaddr) {
	printf("Invalid license server\n");
	return -1;
    }

    installHost(ipaddr, NrOfNodes, none, 0, 0);

    return 0;
}

static int getLicKey(char *token)
{
    char *lickey = parser_getString();

    if (ConfigLicensekey) free(ConfigLicensekey);

    ConfigLicensekey = strdup(lickey);

    return 0;
}

static int getSmallPktSize(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &ConfigSmallPacketSize, "small packet size");
}

static int getResendTimeout(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &ConfigRTO, "resend timeout");
}

static int getHNPend(char *token)
{
    return parser_getNumValue(parser_getString(), &ConfigHNPend, "HNPend");
}

static int getAckPend(char *token)
{
    return parser_getNumValue(parser_getString(), &ConfigAckPend, "AckPend");
}

static int getMCastGroup(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &ConfigMCastGroup, "MCast group");
}

static int getMCastPort(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &ConfigMCastPort, "MCast port");
}

static int getRDPPort(char *token)
{
    return parser_getNumValue(parser_getString(), &ConfigRDPPort, "RDP port");
}

static int getSelectTime(char *token)
{
    int temp;

    return parser_getNumValue(parser_getString(), &temp, "select time");
    ConfigSelectTime = (long) temp;
}

static int getDeadInterval(char *token)
{
    int temp;

    return parser_getNumValue(parser_getString(), &temp, "dead interval");
    ConfigDeadInterval = (long) temp;
}

static int getLogLevel(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &ConfigSyslogLevel, "loglevel");
}

static int getLogDest(char *token)
{
    char skip_it[] = "log_";

    /* Get next token to parse */
    token = parser_getString();

    /* Ignore heading log_, so e.g. log_local0 and local0 are both valid */
    if (strncasecmp(token, skip_it, strlen(skip_it)) == 0) {
	token += strlen(skip_it);
    }

    if (strcasecmp(token, "daemon")==0) {
	ConfigSyslog = LOG_DAEMON;
    } else if (strcasecmp(token, "kernel")==0) {
	ConfigSyslog = LOG_KERN;
    } else if (strcasecmp(token, "kern")==0) {
	ConfigSyslog = LOG_KERN;
    } else if (strcasecmp(token, "local0")==0) {
	ConfigSyslog = LOG_LOCAL0;
    } else if (strcasecmp(token, "local1")==0) {
	ConfigSyslog = LOG_LOCAL1;
    } else if (strcasecmp(token, "local2")==0) {
	ConfigSyslog = LOG_LOCAL2;
    } else if (strcasecmp(token, "local3")==0) {
	ConfigSyslog = LOG_LOCAL3;
    } else if (strcasecmp(token, "local4")==0) {
	ConfigSyslog = LOG_LOCAL4;
    } else if (strcasecmp(token, "local5")==0) {
	ConfigSyslog = LOG_LOCAL5;
    } else if (strcasecmp(token, "local6")==0) {
	ConfigSyslog = LOG_LOCAL6;
    } else if (strcasecmp(token, "local7")==0) {
	ConfigSyslog = LOG_LOCAL7;
    } else {
	return parser_getNumValue(token, &ConfigSyslog, "log destination");
    }

    return 0;
}

/* ---------------------- Stuff for rlimit lines ------------------------ */

static int getNumValOrInfty(char *token, int *value, char *valname)
{
    char skip_it[] = "rlim_";

    if (strncasecmp(token, skip_it, sizeof(skip_it)) == 0) {
	token += sizeof(skip_it);
    }
   
    if (strcasecmp(token, "infinity")==0) {
	*value = RLIM_INFINITY;
    } else if (strcasecmp(token, "unlimited")==0) {
	*value = RLIM_INFINITY;
    } else {
	return parser_getNumValue(token, value, valname);
    }

    return 0;
}

static int getRLimitCPU(char *token)
{
    return getNumValOrInfty(parser_getString(),
			    &ConfigRLimitCPUTime, "RLimit CPUTime");
}

static int getRLimitData(char *token)
{
    return getNumValOrInfty(parser_getString(),
			    &ConfigRLimitDataSize, "RLimit DataSize");
}

static int getRLimitStack(char *token)
{
    return getNumValOrInfty(parser_getString(),
			    &ConfigRLimitStackSize, "RLimit StackSize");
}

static int getRLimitRSS(char *token)
{
    return getNumValOrInfty(parser_getString(),
			    &ConfigRLimitRSSSize, "RLimit RSSSize");
}

#define UP 17 /* Some magic value */

static int endRLimitEnv(char *token)
{
    return UP;
}

static keylist_t rlimitenv_list[] = {
    {"cputime", getRLimitCPU},
    {"datasize", getRLimitData},
    {"stacksize", getRLimitStack},
    {"rsssize", getRLimitRSS},
    {"}", endRLimitEnv},
    {"#", parser_getComment},
    {NULL, parser_error}
};

static parser_t rlimitenv_parser = {" \t\n", rlimitenv_list};

static int getRLimitEnv(char *token)
{
    return parser_parseOn(parser_getLine(), &rlimitenv_parser);
}

static keylist_t rlimit_list[] = {
    {"{", getRLimitEnv},
    {"cputime", getRLimitCPU},
    {"datasize", getRLimitData},
    {"stacksize", getRLimitStack},
    {"rsssize", getRLimitRSS},
    {"#", parser_getComment},
    {NULL, parser_error}
};

static parser_t rlimit_parser = {" \t\n", rlimit_list};

static int getRLimit(char *token)
{
    int ret;

    ret = parser_parseString(parser_getLine(), &rlimit_parser);

    if (ret == UP) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static HWType hwtype = none, node_hwtype;

static int getHW(char *token, HWType *hwtype)
{
    if (strcasecmp(token, "ethernet")==0) {
	*hwtype = ethernet;
    } else if (strcasecmp(token, "myrinet")==0) {
	*hwtype = myrinet;
    } else if (strcasecmp(token, "none")==0) {
	*hwtype = none;
    } else {
	*hwtype = none;

	snprintf(errtxt, sizeof(errtxt), "'%s' is not a valid HWType", token);
	printf("%s\n", errtxt);

	return -1;
    }

    return 0;
}

static int getHWLine(char *token)
{
    int ret;

    ret = getHW(parser_getString(), &hwtype);

    snprintf(errtxt, sizeof(errtxt), "Default HWType is now '%s'",
	     (hwtype==myrinet) ? "myrinet" :
	     (hwtype==ethernet) ? "ethernetnet" :
	     (hwtype==none) ? "none" : "UNKNOWN");
    printf("%s\n", errtxt);

    return ret;
}

static int canstart = 0, node_canstart;

static int getCSLine(char *token)
{
    return parser_getBool(parser_getString(), &canstart, "starter");
}

static int hasIP = 0, node_hasIP;

static int getHasIPLine(char *token)
{
    return parser_getBool(parser_getString(), &hasIP, "hasIP");
}


/* ---------------------------------------------------------------------- */

static int getNodeLine(char *token)
{
    unsigned int ipaddr, nodenum;
    int ret;
    char *hostname;

    node_hwtype = hwtype;
    node_canstart = canstart;
    node_hasIP = hasIP;

    ipaddr = parser_getHostname(token);

    if (ipaddr) {
	hostname = strdup(token);
    } else {
	return -1;
    }

    ret = parser_getNumValue(parser_getString(), &nodenum, "node number");

    if (ret) return ret;

    token = parser_getString();

    while (token) {
	if (strcasecmp(token, "hwtype")==0) {
	    ret = getHW(parser_getString(), &node_hwtype);
	} else if (strcasecmp(token, "starter")==0) {
	    ret = parser_getBool(parser_getString(),
				 &node_canstart, "starter");
	} else if (strcasecmp(token, "hasip")==0) {
	    ret = parser_getBool(parser_getString(), &node_hasIP, "hasIP");
	} else {
	    parser_error(token);
	}

	token = parser_getString(); /* next token */
    }

    if (parser_getDebugLevel()>=10) {
	snprintf(errtxt, sizeof(errtxt), "Register '%s' as %d with HW '%s'"
		 " IP%s supported, starting%s allowed.\n",
		 hostname, nodenum,
		 (node_hwtype==myrinet) ? "myrinet" :
		 (node_hwtype==ethernet) ? "ethernetnet" :
		 (node_hwtype==none) ? "none" : "UNKNOWN",
		 node_hasIP ? "" : " not",
		 node_canstart ? "" : " not");
	parser_comment(errtxt, 10);
    }

    installHost(ipaddr, nodenum, node_hwtype, node_hasIP, node_canstart);

    return 0;
}

static int endNodeEnv(char *token)
{
    return UP;
}

static keylist_t nodeenv_list[] = {
    {"}", endNodeEnv},
    {"#", parser_getComment},
    {NULL, getNodeLine}
};

static parser_t nodeenv_parser = {" \t\n", nodeenv_list};

static int getNodeEnv(char *token)
{
    return parser_parseOn(parser_getLine(), &nodeenv_parser);
}


static keylist_t node_list[] = {
    {"{", getNodeEnv},
    {"#", parser_getComment},
    {NULL, getNodeLine}
};

static parser_t node_parser = {" \t\n", node_list};


static int getNodes(char *token)
{
    int ret;

    ret = parser_parseString(parser_getLine(), &node_parser);

    if (ret == UP) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static keylist_t config_list[] = {
    {"installationdir", getInstDir},
    {"installdir", getInstDir},
    {"module", getModuleName},
    {"routingfile", getRouteFile},
    {"nrofnodes", getNumNodes},
    {"node", getNodes},
    {"nodes", getNodes},
    {"hwtype", getHWLine},
    {"starter", getCSLine},
    {"hasip", getHasIPLine},
    {"licenseserver", getLicServer},
    {"licserver", getLicServer},
    {"licensekey", getLicKey},
    {"lickey", getLicKey},
    {"smallpacketsize", getSmallPktSize},
    {"resendtimeout", getResendTimeout},
    {"hnpend", getHNPend},
    {"ackpend", getAckPend},
    {"mcastgroup", getMCastGroup},
    {"mcastport", getMCastPort},
    {"rdpport", getRDPPort},
    {"selecttime", getSelectTime},
    {"deadinterval", getDeadInterval},
    {"rlimit", getRLimit},
    {"loglevel", getLogLevel},
    {"logdestination", getLogDest},
    {"logdest", getLogDest},
    {"#", parser_getComment},
    {NULL, parser_error}
};

static parser_t config_parser = {" \t\n", config_list};

int parseConfig(int syslogreq)
{
    char myname[255], *temp;
    int found, ret;
    FILE *cfd;
    struct stat sbuf;

    usesyslog=syslogreq;

    /*
     * Set MyId to my own ID (needed for installhost())
     */
    if (!Configfile) {
	char ext[] = "/etc/parastation.conf";
	char *tmpnam;
	tmpnam = PSI_LookupInstalldir();
	Configfile = (char *) malloc(strlen(tmpnam)+strlen(ext)+1);
	strcpy(Configfile, tmpnam);
	strcat(Configfile, ext);
    }

    if ((cfd = fopen(Configfile,"r"))) {
	/* file found */
	snprintf(errtxt, sizeof(errtxt),
		 "Using <%s> as configuration file", Configfile);
	parser_comment(errtxt, 1);
    } else {
	snprintf(errtxt, sizeof(errtxt),
		 "Unable to locate configuration file <%s>", Configfile);
	parser_comment(errtxt, 0);

	return -1;
    }

    /*
     * Start the parser
     */
    parser_init(usesyslog, cfd);

    parser_setDebugLevel(10);

    ret = parser_parseFile(&config_parser);

    if (ret) {
	parser_comment("ERROR: Syntax error in configuration file.", 0);
	return -1;
    }

    fclose(cfd);

    /*
     * Sanity Checks
     */
    if (NrOfNodes==-1) {
	parser_comment("ERROR: NrOfNodes not defined", 0);
	return -1;
    }

    if (NrOfNodes>nodesfound) { /* hosts missing in hostlist */
	parser_comment("WARNING: # hosts in hostlist less than NrOfNodes", 0);
    }

    if (!ConfigModule) {
	parser_comment("ERROR: Module not defined", 0);
	return -1;
    }

    if (!ConfigRoutefile) {
	parser_comment("ERROR: Routefile not defined", 0);
	return -1;
    }

    return 0;
}
