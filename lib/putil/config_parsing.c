/*
 *               ParaStation
 * config_parsing.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: config_parsing.c,v 1.15 2004/03/11 14:53:56 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: config_parsing.c,v 1.15 2004/03/11 14:53:56 eicker Exp $";
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
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"

#include "config_parsing.h"
#include "pslic.h"

/* magic license check */
#include "../../license/pslic_hidden.h"

static config_t config = (config_t) {
    .instDir = NULL,
    .licEnv = { NULL, 0, 0},
    .selectTime = 2,
    .deadInterval = 10,
    .RDPPort = 886,
    .useMCast = 0,
    .MCastGroup = 237,
    .MCastPort = 1889,
    .logLevel = 0,
    .logDest = LOG_DAEMON,
    .useSyslog = 1,
    .freeOnSuspend = 0,
    .handleOldBins = 0,
};

#define ENV_END 17 /* Some magic value */

static int nodesfound = 0;

static char *licFile = NULL;

static char errtxt[256];

/*----------------------------------------------------------------------*/
/* Helper function to insert a node */

static int installHost(unsigned int ipaddr, int id, int hwtype, 
		       unsigned int extraIP, int jobs, int starter)
{
    unsigned int hostno;
    struct host_t *host;

    if (PSnodes_getNum() == -1) { /* NrOfNodes not defined */
	parser_comment("You have to define NrOfNodes before any host", 0);
	return -1;
    }

    if ((id<0) || (id >= PSnodes_getNum())) { /* id out of Range */
	snprintf(errtxt, sizeof(errtxt),
		 "node ID <%d> out of range (NrOfNodes = %d)",
		 id, PSnodes_getNum());
	parser_comment(errtxt, 0);
	return -1;
    }

    if (PSnodes_lookupHost(ipaddr)!=-1) { /* duplicated host */
	snprintf(errtxt, sizeof(errtxt),
		 "duplicated host <%s> in config file",
		 inet_ntoa(* (struct in_addr *) &ipaddr));
	parser_comment(errtxt, 0);
	return -1;
    }

    if (PSnodes_getAddr(id) != INADDR_ANY) { /* duplicated PSI-ID */
	unsigned int addr = PSnodes_getAddr(id);
	snprintf(errtxt, sizeof(errtxt),
		 "duplicated ID <%d> for hosts <%s> and <%s> in config file",
		 id, inet_ntoa(* (struct in_addr *) &ipaddr),
		 inet_ntoa(* (struct in_addr *) &addr));
	parser_comment(errtxt, 0);
	return -1;
    }

    /* install hostname */
    if (PSnodes_register(id, ipaddr)) {
	snprintf(errtxt, sizeof(errtxt),
		 "PSnodes_register(%d, <%s>) failed",
		 id, inet_ntoa(* (struct in_addr *) &ipaddr));
	parser_comment(errtxt, 0);
	return -1;
    }

    if (PSnodes_setHWType(id, hwtype)) {
	snprintf(errtxt, sizeof(errtxt),
		 "PSnodes_setHWType(%d, %d) failed", id, hwtype);
	parser_comment(errtxt, 0);
	return -1;
    }

    if (PSnodes_setExtraIP(id, extraIP)) {
	snprintf(errtxt, sizeof(errtxt),
		 "PSnodes_setExtraIP(%d, %d) failed", id, extraIP);
	parser_comment(errtxt, 0);
	return -1;
    }

    if (PSnodes_setRunJobs(id, jobs)) {
	snprintf(errtxt, sizeof(errtxt),
		 "PSnodes_setRunJobs(%d, %d) failed", id, jobs);
	parser_comment(errtxt, 0);
	return -1;
    }

    if (PSnodes_setIsStarter(id, starter)) {
	snprintf(errtxt, sizeof(errtxt),
		 "PSnodes_setIsStarter(%d, %d) failed", id, starter);
	parser_comment(errtxt, 0);
	return -1;
    }

    nodesfound++;

    if (nodesfound > PSnodes_getNum()) { /* more hosts than nodes ??? */
	snprintf(errtxt, sizeof(errtxt),
		 "NrOfNodes = %d does not match number of hosts in list (%d)",
		 PSnodes_getNum(), nodesfound);
	parser_comment(errtxt, 0);
	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "installHost():"
	     " host <%s> is inserted in the hostlist with id=%d.",
	     inet_ntoa(* (struct in_addr *) &ipaddr), id);
    parser_comment(errtxt, 10);

    return 0;
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
    int num, ret;

    if (PSnodes_getNum() != -1) {
	/* NrOfNodes already defined */
	parser_comment("You have to define NrOfNodes only once", 0);

	return -1;
    }

    ret = parser_getNumValue(parser_getString(), &num, "number of nodes");

    if (ret) return ret;

    /* Initialize the PSnodes module */
    ret = PSnodes_init(num);
    if (ret) {
    	snprintf(errtxt, sizeof(errtxt), "PSnodes_init(%d) failed", num);
	parser_comment(errtxt, 0);
    }

    return ret;
}

static int getLicServer(char *token)
{
    char *hname;
    unsigned int ipaddr;

    hname = parser_getString();
    parser_comment("definition of license server is obsolete", 0);

    return 0;
}

static int getLicFile(char *token)
{
    char *licfile = parser_getString();

    /*
     * Don't use absLicFile here. It will be assigned later.
     * This is due to the fact that you don't have to give the
     * LicenseFile entry inevitably.
     */
    if (licFile) free(licFile);

    licFile = strdup(licfile);

    return 0;
}

static int getMCastUse(char *token)
{
    config.useMCast = 1;
    parser_comment("Will use MCast. Disable alternative status control", 0);
    return 0;
}

static int getMCastGroup(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &config.MCastGroup, "MCast group");
}

static int getMCastPort(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &config.MCastPort, "MCast port");
}

static int getRDPPort(char *token)
{
    return parser_getNumValue(parser_getString(), &config.RDPPort, "RDP port");
}

static int getSelectTime(char *token)
{
    int temp, ret;

    ret = parser_getNumValue(parser_getString(), &temp, "select time");

    if (ret) return ret;

    config.selectTime = temp;

    return ret;
}

static int getDeadInterval(char *token)
{
    int temp, ret;

    ret = parser_getNumValue(parser_getString(), &temp, "dead interval");

    if (ret) return ret;

    config.deadInterval = temp;

    return ret;
}

static int getLogLevel(char *token)
{
    return parser_getNumValue(parser_getString(),
			      &config.logLevel, "loglevel");
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
	config.logDest = LOG_DAEMON;
    } else if (strcasecmp(token, "kernel")==0) {
	config.logDest = LOG_KERN;
    } else if (strcasecmp(token, "kern")==0) {
	config.logDest = LOG_KERN;
    } else if (strcasecmp(token, "local0")==0) {
	config.logDest = LOG_LOCAL0;
    } else if (strcasecmp(token, "local1")==0) {
	config.logDest = LOG_LOCAL1;
    } else if (strcasecmp(token, "local2")==0) {
	config.logDest = LOG_LOCAL2;
    } else if (strcasecmp(token, "local3")==0) {
	config.logDest = LOG_LOCAL3;
    } else if (strcasecmp(token, "local4")==0) {
	config.logDest = LOG_LOCAL4;
    } else if (strcasecmp(token, "local5")==0) {
	config.logDest = LOG_LOCAL5;
    } else if (strcasecmp(token, "local6")==0) {
	config.logDest = LOG_LOCAL6;
    } else if (strcasecmp(token, "local7")==0) {
	config.logDest = LOG_LOCAL7;
    } else {
	return parser_getNumValue(token, &config.logDest, "log destination");
    }

    return 0;
}

/* ---------------------- Stuff for rlimit lines ------------------------ */

static int getRLimitVal(char *token, rlim_t *value, char *valname)
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

static void setLimit(int limit, rlim_t value)
{
    struct rlimit rlp;

    getrlimit(limit, &rlp);
    rlp.rlim_cur=value;
    setrlimit(limit, &rlp);
}


static int getRLimitCPU(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit CPUTime");
    if (ret) return ret;

    setLimit(RLIMIT_CPU, value);

    return 0;
}

static int getRLimitData(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit DataSize");
    if (ret) return ret;

    setLimit(RLIMIT_DATA, value*1024);

    return 0;
}

static int getRLimitStack(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit StackSize");
    if (ret) return ret;

    setLimit(RLIMIT_STACK, value*1024);

    return 0;
}

static int getRLimitRSS(char *token)
{
    rlim_t value;
    int ret;

    return getRLimitVal(parser_getString(), &value, "RLimit RSSize");
    if (ret) return ret;

    setLimit(RLIMIT_RSS, value*1024);

    return 0;
}

static int endRLimitEnv(char *token)
{
    return ENV_END;
}

static keylist_t rlimitenv_list[] = {
    {"cputime", getRLimitCPU},
    {"datasize", getRLimitData},
    {"stacksize", getRLimitStack},
    {"rssize", getRLimitRSS},
    {"}", endRLimitEnv},
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
    {"rssize", getRLimitRSS},
    {NULL, parser_error}
};

static parser_t rlimit_parser = {" \t\n", rlimit_list};

static int getRLimit(char *token)
{
    int ret;

    ret = parser_parseString(parser_getString(), &rlimit_parser);

    if (ret == ENV_END) {
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

static int getHWent(char *token)
{
    int idx = HW_index(token);

    if (idx < 0) return parser_error(token);

    node_hwtype |= 1<<idx;

    return 0;
}


static int endHWEnv(char *token)
{
    return ENV_END;
}

static keylist_t hwenv_list[] = {
    {"none", getHWnone},
    {"}", endHWEnv},
    {NULL, getHWent}
};

static parser_t hwenv_parser = {" \t\n", hwenv_list};

static int getHWEnv(char *token)
{
    return parser_parseOn(parser_getString(), &hwenv_parser);
}

static keylist_t hw_list[] = {
    {"{", getHWEnv},
    {"none", getHWnone},
    {NULL, getHWent}
};

static parser_t hw_parser = {" \t\n", hw_list};

static int getHW(char *token)
{
    int ret;

    node_hwtype = 0;

    ret = parser_parseToken(parser_getString(), &hw_parser);

    if (ret == ENV_END) {
	return 0;
    }

    return ret;
}

static int getHWLine(char *token)
{
    int ret;

    ret = getHW(token);

    hwtype = node_hwtype;

    snprintf(errtxt, sizeof(errtxt), "Default HWType is now '%s'",
	     HW_printType(hwtype));
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

static int runjobs = 1, node_runjobs;

static int getRJ(char *token)
{
    return parser_getBool(parser_getString(), &node_runjobs, "node_runjobs");
}

static int getRJLine(char *token)
{
    int ret;

    ret = getRJ(token);

    runjobs = node_runjobs;

    snprintf(errtxt, sizeof(errtxt), "Default for 'RunJobs' is now '%s'",
	     runjobs ? "TRUE" : "FALSE");
    parser_comment(errtxt, 8);

    return ret;
}

static unsigned int node_extraIP;

static int getExtraIP(char *token)
{
    node_extraIP = parser_getHostname(parser_getString());

    if (!node_extraIP) return -1;

    return 0;
}

/* ---------------------------------------------------------------------- */

static keylist_t nodeline_list[] = {
    {"hwtype", getHW},
    {"runjobs", getRJ},
    {"starter", getCS},
    {"extraip", getExtraIP},
    {NULL, parser_error}
};

static parser_t nodeline_parser = {" \t\n", nodeline_list};

static int getNodeLine(char *token)
{
    unsigned int ipaddr;
    int nodenum, ret;
    char *hostname;

    node_hwtype = hwtype;
    node_runjobs = runjobs;
    node_canstart = canstart;
    node_extraIP = 0;

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
		 " HW '%s', jobs%s allowed, starting%s allowed.",
		 hostname, nodenum, HW_printType(node_hwtype),
		 node_runjobs ? "" : " not", node_canstart ? "" : " not");
	parser_comment(errtxt, 6);
	if (node_extraIP) {
	    snprintf(errtxt, sizeof(errtxt), " Myrinet IP will be <%s>.",
		     inet_ntoa(* (struct in_addr *) &node_extraIP));
	    parser_comment(errtxt, 6);
	}
	parser_comment("\n", 6);
    }

    ret = installHost(ipaddr, nodenum, node_hwtype, node_extraIP,
		      node_runjobs, node_canstart);

    return ret;
}

static int endNodeEnv(char *token)
{
    return ENV_END;
}

static keylist_t nodeenv_list[] = {
    {"}", endNodeEnv},
    {NULL, getNodeLine}
};

static parser_t nodeenv_parser = {" \t\n", nodeenv_list};

static int getNodeEnv(char *token)
{
    return parser_parseOn(parser_getString(), &nodeenv_parser);
}


static keylist_t node_list[] = {
    {"{", getNodeEnv},
    {NULL, getNodeLine}
};

static parser_t node_parser = {" \t\n", node_list};


static int getNodes(char *token)
{
    int ret;

    ret = parser_parseString(parser_getString(), &node_parser);

    if (ret == ENV_END) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

char *getQuotedString(char *line)
{
    char *end, *value = NULL;

    if (!line) {
	snprintf(errtxt, sizeof(errtxt), "empty line\n");
	parser_comment(errtxt,0);
	return NULL;
    }

    /* Remove leading whitespace */
    while (*line==' ' || *line=='\t') line++;

    if (*line == '"' || *line == '\'') {
	/* value is protected by quotes or double quotes */
	char quote = *line;

	end = strchr(line+1, quote);

	if (end) {
	    *end = '\0';
	    value = strdup(line+1);
	    end++;
	}
    } else {
	/* search for end of string */
	end = line;
	while (*end!=' ' && *end!='\t' && *end!='\0') end++;

	if (end != line) {
	    char bak = *end;
	    *end = '\0';
	    value = strdup(line);
	    *end = bak;
	}
    }

    if (!value) {
	snprintf(errtxt, sizeof(errtxt), "no string found within %s\n", line);
	parser_comment(errtxt,0);
	return NULL;
    }

    /* Skip trailing whitespace */
    while (*end==' ' || *end=='\t') end++;
    if (*end) {
	snprintf(errtxt, sizeof(errtxt), "found trailing garbage '%s' %d\n",
		 end, *end);
	parser_comment(errtxt,0);
	return NULL;
    }

    return value;

}
/* ---------------------------------------------------------------------- */

static int getEnvLine(char *token)
{
    char *name, *line, *value;

    if (token) {
	name = strdup(token);
    } else {
	snprintf(errtxt, sizeof(errtxt), "syntax error\n");
	parser_comment(errtxt,0);
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	snprintf(errtxt, sizeof(errtxt), "premature end of line\n");
	parser_comment(errtxt,0);
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	snprintf(errtxt, sizeof(errtxt), "no value for %s\n", name);
	parser_comment(errtxt,0);
	return -1;
    }

    /* store environment */
    setenv(name, value, 1);

    snprintf(errtxt, sizeof(errtxt),
	     "Got environment: %s='%s'\n", name , value);
    parser_comment(errtxt,10);

    return 0;
}

static int endEnvEnv(char *token)
{
    return ENV_END;
}

static keylist_t envenv_list[] = {
    {"}", endEnvEnv},
    {NULL, getEnvLine}
};

static parser_t envenv_parser = {" \t\n", envenv_list};

static int getEnvEnv(char *token)
{
    return parser_parseOn(parser_getString(), &envenv_parser);
}

static keylist_t env_list[] = {
    {"{", getEnvEnv},
    {NULL, getEnvLine}
};

static parser_t env_parser = {" \t\n", env_list};

static int getEnv(char *token)
{
    int ret;

    ret = parser_parseString(parser_getString(), &env_parser);

    if (ret == ENV_END) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static int actHW = -1;

static int getHardwareScript(char *token)
{
    char *name, *line, *value;

    if (strcasecmp(token, "startscript")==0) {
	name = HW_STARTER;
    } else if (strcasecmp(token, "stopscript")==0) {
	name = HW_STOPPER;
    } else if (strcasecmp(token, "setupscript")==0) {
	name = HW_SETUP;
    } else if (strcasecmp(token, "headerscript")==0) {
	name = HW_HEADERLINE;
    } else if (strcasecmp(token, "statusscript")==0) {
	name = HW_COUNTER;
    } else {
	snprintf(errtxt, sizeof(errtxt), "unknown script type '%s'\n", token);
	parser_comment(errtxt,0);
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	snprintf(errtxt, sizeof(errtxt), "premature end of line\n");
	parser_comment(errtxt,0);
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	snprintf(errtxt, sizeof(errtxt), "no value for %s\n", name);
	parser_comment(errtxt,0);
	return -1;
    }

    /* store environment */
    if (HW_getScript(actHW, name)) {
	snprintf(errtxt, sizeof(errtxt),
	     "Redefineing hardware script: %s\n", name);
	parser_comment(errtxt,0);
    }
    HW_setScript(actHW, name, value);

    snprintf(errtxt, sizeof(errtxt),
	     "Got hardware script: %s='%s'\n", name , value);
    parser_comment(errtxt,10);

    return 0;
}

static int getHardwareEnvLine(char *token)
{
    char *line, *end;
    char *name, *value = NULL;

    if (token) {
	name = strdup(token);
    } else {
	snprintf(errtxt, sizeof(errtxt), "syntax error\n");
	parser_comment(errtxt,0);
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	snprintf(errtxt, sizeof(errtxt), "premature end of line\n");
	parser_comment(errtxt,0);
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	snprintf(errtxt, sizeof(errtxt), "no value for %s\n", name);
	parser_comment(errtxt,0);
	return -1;
    }

    /* store environment */
    if (HW_getEnv(actHW, name)) {
	snprintf(errtxt, sizeof(errtxt),
	     "Redefineing hardware environment: %s\n", name);
	parser_comment(errtxt,0);
    }
    HW_setEnv(actHW, name, value);

    snprintf(errtxt, sizeof(errtxt),
	     "Got hardware environment: %s='%s'\n", name , value);
    parser_comment(errtxt,10);

    return 0;
}

static int endHardwareEnv(char *token)
{
    actHW = -1;
    return ENV_END;
}

static keylist_t hardwareenv_list[] = {
    {"}", endHardwareEnv},
    {"startscript", getHardwareScript},
    {"stopscript", getHardwareScript},
    {"setupscript", getHardwareScript},
    {"headerscript", getHardwareScript},
    {"statusscript", getHardwareScript},
    {NULL, getHardwareEnvLine}
};

static parser_t hardwareenv_parser = {" \t\n", hardwareenv_list};

static int getHardwareEnv(char *token)
{
    return parser_parseOn(parser_getString(), &hardwareenv_parser);
}

static int getHardware(char *token)
{
    char *name, *brace;
    int ret;

    name = parser_getString();

    actHW = HW_index(name);

    if (actHW == -1) {
	actHW = HW_add(name);

	snprintf(errtxt, sizeof(errtxt),
		 "New hardware '%s' registered as %d\n", name, actHW);
	parser_comment(errtxt,10);
    }

    brace = parser_getString();
    if (strcmp(brace, "{")) return -1;

    ret = parser_parseOn(parser_getString(), &hardwareenv_parser);

    if (ret == ENV_END) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static int getFreeOnSusp(char *token)
{
    config.freeOnSuspend = 1;
    parser_comment("Suspended jobs will free their resources", 0);
    return 0;
}
    
static int getHandleOldBins(char *token)
{
    config.handleOldBins = 1;
    parser_comment("Recognize old binaries within resource management", 0);
    return 0;
}
    
/* ---------------------------------------------------------------------- */

static keylist_t config_list[] = {
    {"installationdir", getInstDir},
    {"installdir", getInstDir},
    {"hardware", getHardware},
    {"nrofnodes", getNumNodes},
    {"hwtype", getHWLine},
    {"runjobs", getRJLine},
    {"starter", getCSLine},
    {"node", getNodes},
    {"nodes", getNodes},
    {"licenseserver", getLicServer},
    {"licserver", getLicServer},
    {"licensefile", getLicFile},
    {"licfile", getLicFile},
    {"usemcast", getMCastUse},
    {"mcastgroup", getMCastGroup},
    {"mcastport", getMCastPort},
    {"rdpport", getRDPPort},
    {"selecttime", getSelectTime},
    {"deadinterval", getDeadInterval},
    {"rlimit", getRLimit},
    {"loglevel", getLogLevel},
    {"logdestination", getLogDest},
    {"logdest", getLogDest},
    {"environment", getEnv},
    {"env", getEnv},
    {"freeOnSuspend", getFreeOnSusp},
    {"handleOldBins", getHandleOldBins},
    {NULL, parser_error}
};

static parser_t config_parser = {" \t\n", config_list};

#define DEFAULT_LICFILE "license"

config_t *parseConfig(int usesyslog, int loglevel, char *configfile)
{
    FILE *cfd;
    char *absLicFile = NULL;
    int ret;

    parser_init(usesyslog, NULL);

    if (!configfile) {
	parser_comment("No configuration file defined", 0);
	return NULL;
    }
 
    if (!(cfd = fopen(configfile,"r"))) {
	snprintf(errtxt, sizeof(errtxt),
		 "Unable to locate configuration file <%s>", configfile);
	parser_comment(errtxt, 0);

	return NULL;
    }
    parser_setFile(cfd);

    parser_setDebugLevel(loglevel);

    snprintf(errtxt, sizeof(errtxt),
	     "Using <%s> as configuration file", configfile);
    parser_comment(errtxt, 1);

    ret = parser_parseFile(&config_parser);

    if (ret) {
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR: Parsing of configuration file <%s> failed.",
		 configfile);
	parser_comment(errtxt, 0);
	return NULL;
    }

    fclose(cfd);

    /*
     * Sanity Checks
     */
    if (PSnodes_getNum()==-1) {
	parser_comment("ERROR: NrOfNodes not defined", 0);
	return NULL;
    }

    /*
     * Test if the Licensefile exists
     */
    if (!licFile) {
	licFile = strdup(DEFAULT_LICFILE);
    }

    absLicFile = parser_getFilename(licFile, PSC_lookupInstalldir(),
				    "/config");

    if (!absLicFile) {
	char *tmp = strdup(configfile);
	absLicFile = parser_getFilename(licFile, dirname(tmp), NULL);
	free(tmp);
    }
	
    if (!absLicFile) {
 	snprintf(errtxt, sizeof(errtxt),
		 "Unable to locate LicenseFile '%s'", licFile);
	parser_comment(errtxt, 0);
	return NULL;
    } else {
 	snprintf(errtxt, sizeof(errtxt),
		 "Using <%s> as license file", absLicFile);
	parser_comment(errtxt, 1);
    }

    /*
     * Read the Licensefile 
     */
    env_init(&config.licEnv);
    if (lic_fromfile(&config.licEnv, absLicFile) < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "ERROR: %s.", lic_errstr ? lic_errstr : "in licensefile");
	parser_comment(errtxt, 0);
	return NULL;
    }

    {
	/* Put the MCP license key into the right environment */
	char *LicKeyMCP = env_get(&config.licEnv, LIC_MCPKEY);

	if (LicKeyMCP) {
	    int hw = HW_index("myrinet");

	    if (hw >= 0) {
		HW_setEnv(hw, "PS_LIC", LicKeyMCP);
	    }
	}
    }

    /*
     * Sanity Checks
     */
    if (PSnodes_getNum() > nodesfound) { /* hosts missing in hostlist */
	parser_comment("WARNING: # hosts in hostlist less than NrOfNodes", 0);
    }
    
    if (!lic_isvalid(&config.licEnv)) {
	parser_comment("ERROR: Corrupted license.", 0);
	return NULL;
    }
    
    if (PSnodes_getNum() > lic_numval(&config.licEnv, LIC_NODES, 0)) {
	parser_comment("ERROR: NrOfNodes to large for this License.", 0);
	return NULL;
    }

    if (lic_isexpired(&config.licEnv)) {
	parser_comment("ERROR: License expired.", 0);
	return NULL;
    }
    
    return &config;
}
