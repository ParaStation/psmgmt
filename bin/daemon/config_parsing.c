/*
 *               ParaStation3
 * config_parsing.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: config_parsing.c,v 1.10 2002/07/19 13:18:18 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: config_parsing.c,v 1.10 2002/07/19 13:18:18 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#include <netdb.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

#include "parser.h"

#include "pscommon.h"
#include "pshwtypes.h"

#include "psidutil.h"

#include "config_parsing.h"
#include "pslic.h"

int nodesfound = 0;

struct host_t *hosts[256];

int NrOfNodes = -1;

struct node_t *nodes = NULL;
struct node_t licNode = {
    INADDR_ANY, /* addr */
    1,          /* numCPU */
    0,          /* isUp */
    0,          /* hwtype */
    0,          /* hwStatus */
    0,          /* hasIP */
    0           /* starter */
};

char *Configfile = NULL;
char *ConfigInstDir = NULL;

char *ConfigLicenseFile = NULL;
env_fields_t ConfigLicEnv = { NULL, 0, 0 };

char *ConfigLicenseKeyMCP = NULL;
char *ConfigMyriModule = NULL;
char *ConfigRoutefile = NULL;

int ConfigSmallPacketSize = -1;
int ConfigRTO = -1;
int ConfigHNPend = -1;
int ConfigAckPend = -1;

char *ConfigIPModule = NULL;
char *ConfigIPPrefix = NULL;
int ConfigIPPrefixLen = 20;

char *ConfigGigaEtherModule = NULL;

long ConfigSelectTime = 2;
long ConfigDeadInterval = 10;
int ConfigRDPPort = 886;
int ConfigMCastGroup = 237;
int ConfigMCastPort = 1889;

rlim_t ConfigRLimitCPUTime = -1;
rlim_t ConfigRLimitDataSize = -1;
rlim_t ConfigRLimitStackSize = -1;
rlim_t ConfigRLimitRSSSize = -1;

int ConfigLogLevel = 0;          /* default logging level */
int ConfigLogDest = LOG_DAEMON;

char *LicFile = NULL;

int MyPsiId = -1;

static char errtxt[256];

/*----------------------------------------------------------------------*/
/* Helper function to manage nodes and hosts list */
static int allocHosts(int num)
{
    int i;

    if (nodes) free(nodes); /* @todo nodes allready allocated. Error? */

    nodes = malloc(sizeof(*nodes) * num);

    if (!nodes) {
	snprintf(errtxt, sizeof(errtxt),
		 "Cannot allocate memory for %d nodes", num);
	parser_comment(errtxt, 0);
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "Allocate memory for %d nodes", num);
    parser_comment(errtxt, 10);

    /* Clear nodes */
    for (i=0; i<num; i++) {
        nodes[i].addr = INADDR_ANY;
	nodes[i].numCPU = 0;
	nodes[i].isUp = 0;
        nodes[i].hwType = 0;
        nodes[i].hwStatus = 0;
	nodes[i].hasIP = 0;
	nodes[i].starter = 0;
    }

    return 0;
}

static int installHost(unsigned int ipaddr, int id,
		       int hwtype, int ip, int starter)
{
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

    if (parser_lookupHost(ipaddr)!=-1) { /* duplicated host */
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
    nodes[id].hwType = hwtype;
    nodes[id].hasIP = ip;
    nodes[id].starter = starter;

    nodesfound++;

    if (nodesfound > NrOfNodes) { /* more hosts than nodes ??? */
	snprintf(errtxt, sizeof(errtxt),
		 "NrOfNodes = %d does not match number of hosts in list (%d)",
		 NrOfNodes, nodesfound);
	parser_comment(errtxt, 0);
	return -1;
    }

    hostno = ntohl(ipaddr) & 0xff;

    for (host = hosts[hostno]; host; host = host->next) {
	if (host->addr == ipaddr) {
	    /* host already configured ?? */
	    snprintf(errtxt, sizeof(errtxt),
		     "Host <%s> already configured (id=%d)",
		     inet_ntoa(* (struct in_addr *) &ipaddr), host->id);
	    parser_comment(errtxt, 0);
	    return -1;
	}
    }

    host = (struct host_t*) malloc(sizeof(struct host_t));
    if (!host) {
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
	return PSC_getMyID(); /* @todo Ist das sinnvoll ? */
                   /* Macht es ueberhaupt Sinn localhost zu benutzen ? */

    /* other addresses */
    hostno = ntohl(ipaddr) & 0xFF;
    for (host = hosts[hostno]; host; host = host->next) {
	if (host->addr == ipaddr) {
	    if (parser_getDebugLevel() >= 10) {
		snprintf(errtxt, sizeof(errtxt),
			 "parser_lookupHost(): host <%s> has id=%d.",
			 inet_ntoa(* (struct in_addr *)&ipaddr), host->id);
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
	snprintf(errtxt, sizeof(errtxt), "%s: %s", dname, strerror(errno));
	parser_comment(errtxt, 0);

	return -1;
    }

    if (!S_ISDIR(fstat.st_mode)) {
	snprintf(errtxt, sizeof(errtxt), "'%s' is not a directory", dname);
	parser_comment(errtxt, 0);

	return -1;
    }

    PSC_setInstalldir(dname);
    if (strcmp(dname, PSC_lookupInstalldir())) {
	snprintf(errtxt, sizeof(errtxt),
		 "'%s' seems to be no valid installdir", dname);
	parser_comment(errtxt, 0);

	return -1;
    }

    return 0;
}

static int getNumNodes(char *token)
{
    int ret;

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

static int getMyriModuleName(char *token)
{
    char *modname, *file;

    modname = parser_getString();
    file = parser_getFilename(modname, PSC_lookupInstalldir(), "/bin/modules");

    if (!file) {
	snprintf(errtxt, sizeof(errtxt),
		 "cannot find MyriNet module '%s'", modname);
	parser_comment(errtxt, 0);

	return -1;
    }

    if (ConfigMyriModule) free(ConfigMyriModule);

    ConfigMyriModule = strdup(file);

    return 0;
}

static int getIPModuleName(char *token)
{
    char *modname, *file;

    modname = parser_getString();
    file = parser_getFilename(modname, PSC_lookupInstalldir(), "/bin/modules");

    if (!file) {
	snprintf(errtxt, sizeof(errtxt),
		 "cannot find IP module '%s'", modname);
	parser_comment(errtxt, 0);

	return -1;
    }

    if (ConfigIPModule) free(ConfigIPModule);

    ConfigIPModule = strdup(file);

    /* Set IPPrefix to default value */
    if (!ConfigIPPrefix) {
	ConfigIPPrefix = strdup("192.168.16");
    }

    return 0;
}

static int getIPPrefix(char *token)
{
    char *prefix;

    prefix = parser_getString();

    /* @todo Testing if prefix is valid */

    if (ConfigIPPrefix) free(ConfigIPPrefix);

    ConfigIPPrefix = strdup(prefix);

    return 0;
}

static int getIPPrefixLen(char *token)
{
    int ret;

    ret = parser_getNumValue(parser_getString(),
			     &ConfigIPPrefixLen, "IP-prefix length");

    if (ret) return ret;

    /* @todo Testing if ConfigIPPrefixLen is valid */

    return 0;
}

static int getGigaEtherModuleName(char *token)
{
    char *modname, *file;

    modname = parser_getString();
    file = parser_getFilename(modname, PSC_lookupInstalldir(), "/bin/modules");

    if (!file) {
	snprintf(errtxt, sizeof(errtxt),
		 "cannot find Gigabit-Ethernet module '%s'", modname);
	parser_comment(errtxt, 0);

	return -1;
    }

    if (ConfigGigaEtherModule) free(ConfigGigaEtherModule);

    ConfigGigaEtherModule = strdup(file);

    return 0;
}

static int getRouteFile(char *token)
{
    char *routefile, *file;

    routefile = parser_getString();
    file= parser_getFilename(routefile, PSC_lookupInstalldir(), "/config");

    if (!file) {
	snprintf(errtxt, sizeof(errtxt),
		 "cannot find routefile '%s'", routefile);
	parser_comment(errtxt, 0);

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
	return parser_error(token);
    }

    ipaddr = parser_getHostname(hname);
    
    if (!ipaddr) {
	snprintf(errtxt, sizeof(errtxt), "license server '%s' invalid", hname);
	parser_comment(errtxt, 0);

	return -1;
    }

    if (licNode.addr != INADDR_ANY) { /* duplicated LicServer */
	parser_comment("license server defined twice in config file", 0);
	return -1;
    }

    /* install hostname */
    licNode.addr = ipaddr;

    return 0;
}

static int getLicFile(char *token)
{
    char *licfile = parser_getString();

    /*
     * Don't use ConfigLicenseFile here. It will be assigned later.
     * This is due to the fact that you don't have to give the
     * LicenseFile entry inevitably.
     */
    if (LicFile) free(LicFile);

    LicFile = strdup(licfile);

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
    int temp, ret;

    ret = parser_getNumValue(parser_getString(), &temp, "select time");

    if (ret) return ret;

    ConfigSelectTime = (long) temp;

    return ret;
}

static int getDeadInterval(char *token)
{
    int temp, ret;

    ret = parser_getNumValue(parser_getString(), &temp, "dead interval");

    if (ret) return ret;

    ConfigDeadInterval = (long) temp;

    return ret;
}

static int getLogLevel(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &ConfigLogLevel, "loglevel");
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
	ConfigLogDest = LOG_DAEMON;
    } else if (strcasecmp(token, "kernel")==0) {
	ConfigLogDest = LOG_KERN;
    } else if (strcasecmp(token, "kern")==0) {
	ConfigLogDest = LOG_KERN;
    } else if (strcasecmp(token, "local0")==0) {
	ConfigLogDest = LOG_LOCAL0;
    } else if (strcasecmp(token, "local1")==0) {
	ConfigLogDest = LOG_LOCAL1;
    } else if (strcasecmp(token, "local2")==0) {
	ConfigLogDest = LOG_LOCAL2;
    } else if (strcasecmp(token, "local3")==0) {
	ConfigLogDest = LOG_LOCAL3;
    } else if (strcasecmp(token, "local4")==0) {
	ConfigLogDest = LOG_LOCAL4;
    } else if (strcasecmp(token, "local5")==0) {
	ConfigLogDest = LOG_LOCAL5;
    } else if (strcasecmp(token, "local6")==0) {
	ConfigLogDest = LOG_LOCAL6;
    } else if (strcasecmp(token, "local7")==0) {
	ConfigLogDest = LOG_LOCAL7;
    } else {
	return parser_getNumValue(token, &ConfigLogDest, "log destination");
    }

    return 0;
}

/* ---------------------- Stuff for rlimit lines ------------------------ */

static int getRLimitVal(char *token, long *value, char *valname)
{
    char skip_it[] = "rlim_";
    int intval, ret;

    if (strncasecmp(token, skip_it, sizeof(skip_it)) == 0) {
	token += sizeof(skip_it);
    }

    if (strcasecmp(token,"infinity")==0 || strcasecmp(token, "unlimited")==0) {
	*value = RLIM_INFINITY;
	snprintf(errtxt, sizeof(errtxt),
		 "Got 'RLIM_INFINITY' for '%s'", valname);
	parser_comment(errtxt, 8);
    } else {
	ret = parser_getNumValue(token, &intval, valname);
	*value = intval;
	return ret;
    }

    return 0;
}

static int getRLimitCPU(char *token)
{
    return getRLimitVal(parser_getString(),
			&ConfigRLimitCPUTime, "RLimit CPUTime");
}

static int getRLimitData(char *token)
{
    return getRLimitVal(parser_getString(),
			&ConfigRLimitDataSize, "RLimit DataSize");
}

static int getRLimitStack(char *token)
{
    return getRLimitVal(parser_getString(),
			&ConfigRLimitStackSize, "RLimit StackSize");
}

static int getRLimitRSS(char *token)
{
    return getRLimitVal(parser_getString(),
			&ConfigRLimitRSSSize, "RLimit RSSSize");
}

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
    return parser_parseOn(parser_getString(), &rlimitenv_parser);
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

    ret = parser_parseString(parser_getString(), &rlimit_parser);

    if (ret == UP) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static int hwtype = 0, node_hwtype;

static int getHWnone(char *token)
{
    node_hwtype = 0;

    return 0;
}

static int getHWethernet(char *token)
{
    node_hwtype |= PSHW_ETHERNET;

    return 0;
}

static int getHWmyrinet(char *token)
{
    node_hwtype |= PSHW_MYRINET;

    return 0;
}

static int getHWgigaethernet(char *token)
{
    node_hwtype |= PSHW_GIGAETHERNET;

    return 0;
}

static int endHWEnv(char *token)
{
    return UP;
}

static keylist_t hwenv_list[] = {
    {"ethernet", getHWethernet},
    {"myrinet", getHWmyrinet},
    {"gigaethernet", getHWgigaethernet},
    {"}", endHWEnv},
    {"#", parser_getComment},
    {NULL, parser_error}
};

static parser_t hwenv_parser = {" \t\n", hwenv_list};

static int getHWEnv(char *token)
{
    return parser_parseOn(parser_getString(), &hwenv_parser);
}

static keylist_t hw_list[] = {
    {"{", getHWEnv},
    {"none", getHWnone},
    {"ethernet", getHWethernet},
    {"myrinet", getHWmyrinet},
    {"gigaethernet", getHWgigaethernet},
    {"#", parser_getComment},
    {NULL, parser_error}
};

static parser_t hw_parser = {" \t\n", hw_list};

static int getHW(char *token)
{
    int ret;

    node_hwtype = 0;

    ret = parser_parseToken(parser_getString(), &hw_parser);

    if (ret == UP) {
	return 0;
    }

    return ret;
}

static int getHWLine(char *token)
{
    int ret;

    ret = getHW(token);

    hwtype = node_hwtype;

    snprintf(errtxt, sizeof(errtxt),
	     "Default HWType is now '%s'", PSHW_printType(hwtype));
    parser_comment(errtxt, 8);

    return ret;
}

static int canstart = 1, node_canstart;

static int getCS(char *token)
{
    return parser_getBool(parser_getString(), &node_canstart, "node_canstart");
}

static int getCSLine(char *token)
{
    int ret;

    ret = getCS(token);

    canstart = node_canstart;

    snprintf(errtxt, sizeof(errtxt), "Default for 'CanStart' is now '%s'",
	     canstart ? "TRUE" : "FALSE");
    parser_comment(errtxt, 8);

    return ret;
}

static int hasIP = 0, node_hasIP;

static int getHasIP(char *token)
{
    return parser_getBool(parser_getString(), &node_hasIP, "hasIP");
}

static int getHasIPLine(char *token)
{
    int ret;

    ret = getHasIP(token);

    hasIP = node_hasIP;

    snprintf(errtxt, sizeof(errtxt), "Default for 'HasIP' is now '%s'",
	     hasIP ? "TRUE" : "FALSE");
    parser_comment(errtxt, 8);

    return ret;
}


/* ---------------------------------------------------------------------- */

static keylist_t nodeline_list[] = {
    {"hwtype", getHW},
    {"starter", getCS},
    {"hasip", getHasIP},
    {"#", parser_getComment},
    {NULL, parser_error}
};

static parser_t nodeline_parser = {" \t\n", nodeline_list};

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

    ret = parser_parseString(parser_getString(), &nodeline_parser);

    if (ret) return ret;

    if (parser_getDebugLevel()>=6) {
	snprintf(errtxt, sizeof(errtxt), "Register '%s' as %d with"
		 " HW '%s' IP%s supported, starting%s allowed.\n",
		 hostname, nodenum, PSHW_printType(node_hwtype),
		 node_hasIP ? "" : " not", node_canstart ? "" : " not");
	parser_comment(errtxt, 6);
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
    return parser_parseOn(parser_getString(), &nodeenv_parser);
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

    ret = parser_parseString(parser_getString(), &node_parser);

    if (ret == UP) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static keylist_t config_list[] = {
    {"installationdir", getInstDir},
    {"installdir", getInstDir},
    {"myrinetmodule", getMyriModuleName},
    {"smallpacketsize", getSmallPktSize},
    {"resendtimeout", getResendTimeout},
    {"hnpend", getHNPend},
    {"ackpend", getAckPend},
    {"ipmodule", getIPModuleName},
    {"ipprefix", getIPPrefix},
    {"ipprefixlen", getIPPrefixLen},
    {"gigaethermodule", getGigaEtherModuleName},
    {"routingfile", getRouteFile},
    {"nrofnodes", getNumNodes},
    {"node", getNodes},
    {"nodes", getNodes},
    {"hwtype", getHWLine},
    {"starter", getCSLine},
    {"hasip", getHasIPLine},
    {"licenseserver", getLicServer},
    {"licserver", getLicServer},
    {"licensefile", getLicFile},
    {"licfile", getLicFile},
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

#define DEFAULT_CONFIGFILE "/etc/parastation.conf"
#define DEFAULT_LICFILE "license"

int parseConfig(int usesyslog, int loglevel)
{
    int ret;
    FILE *cfd;

    /*
     * Set MyId to my own ID (needed for installhost())
     */
    if (!Configfile) {
	Configfile = DEFAULT_CONFIGFILE;
    }

    if (!(cfd = fopen(Configfile,"r"))) {
	snprintf(errtxt, sizeof(errtxt),
		 "Unable to locate configuration file <%s>", Configfile);
	parser_comment(errtxt, 0);

	return -1;
    }

    /*
     * Start the parser
     */
    parser_init(usesyslog, cfd);

    parser_setDebugLevel(loglevel);

    snprintf(errtxt, sizeof(errtxt),
	     "Using <%s> as configuration file", Configfile);
    parser_comment(errtxt, 1);

    ret = parser_parseFile(&config_parser);

    if (ret) {
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR: Parsing of configuration file <%s> failed.",
		 Configfile);
	parser_comment(errtxt, 0);

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

    /*
     * Test if the Licensefile exists
     */
    if (!LicFile) {
	LicFile = strdup(DEFAULT_LICFILE);
    }

    ConfigLicenseFile = parser_getFilename(LicFile, PSC_lookupInstalldir(),
					   "/config");

    if (!ConfigLicenseFile) {
	char *tmp = strdup(Configfile);
	ConfigLicenseFile = parser_getFilename(LicFile, dirname(tmp), NULL);
	free(tmp);
    }
	
    if (!ConfigLicenseFile) {
 	snprintf(errtxt, sizeof(errtxt),
		 "Unable to locate LicenseFile '%s'", LicFile);
	parser_comment(errtxt, 0);

	return -1;
    } else {
 	snprintf(errtxt, sizeof(errtxt),
		 "Using <%s> as license file", ConfigLicenseFile);
	parser_comment(errtxt, 1);
    }

    /*
     * Read the Licensefile 
     */
    if (lic_fromfile(&ConfigLicEnv, ConfigLicenseFile) < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR: %s.", lic_errstr ? lic_errstr : "in licensefile");
	parser_comment(errtxt, 0);
	return -1;
    }
    ConfigLicenseKeyMCP = env_get(&ConfigLicEnv, LIC_MCPKEY);

    /*
     * Sanity Checks
     */
    if (NrOfNodes > lic_numval(&ConfigLicEnv, LIC_NODES, 0)) {
	parser_comment("ERROR: NrOfNodes to large for this License.", 0);
	return -1;
    }

    if (lic_isexpired(&ConfigLicEnv)) {
	parser_comment("ERROR: License expired.", 0);
	return -1;
    }
    
    if (NrOfNodes>nodesfound) { /* hosts missing in hostlist */
	parser_comment("WARNING: # hosts in hostlist less than NrOfNodes", 0);
    }
    
    return 0;
}
