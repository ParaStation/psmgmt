/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
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
#include <pwd.h>
#include <grp.h>
 
#include "parser.h"
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"
#include "pspartition.h"

#include "config_parsing.h"

static config_t config = (config_t) {
    .instDir = NULL,
    .selectTime = 2,
    .deadInterval = 10,
    .RDPPort = 886,
    .useMCast = 0,
    .MCastGroup = 237,
    .MCastPort = 1889,
    .logMask = 0,
    .logDest = LOG_DAEMON,
    .logfile = NULL,
    .freeOnSuspend = 0,
    .handleOldBins = 0,
    .nodesSort = PART_SORT_PROC,
};

#define ENV_END 17 /* Some magic value */

static int nodesfound = 0;

/*----------------------------------------------------------------------*/
/**
 * @brief Insert a node.
 *
 * Helper function to insert a node.... @todo
 *
 * @param ipaddr
 *
 * @param id
 *
 * @param hwtype
 *
 * @param extraIP Not used.
 *
 * @param jobs Flag if jobs are allowed on this node.
 *
 * @param starter Flag if start of jobs is allowed on this node.
 *
 * @param uid The user ID the node is reserved to as default.
 *
 * @param gid The group ID the node is reserved to as default.
 *
 * @param procs The numer of processes allowed to run on this node. -1
 * flags unlimited number of processes.
 *
 * @param overbook Flag if overbooking is allowed on this node.
 *
 * @return Return -1 if an error occurred or 0 if the node was
 * inserted successfully.
 */
static int installHost(unsigned int ipaddr, int id, int hwtype, 
		       unsigned int extraIP, int jobs, int starter,
		       uid_t uid, gid_t gid, int procs, int overbook)
{
    if (PSnodes_getNum() == -1) { /* NrOfNodes not defined */
	parser_comment(-1, "define NrOfNodes before any host");
	return -1;
    }

    if ((id<0) || (id >= PSnodes_getNum())) { /* id out of Range */
	parser_comment(-1, "node ID <%d> out of range (NrOfNodes = %d)",
		       id, PSnodes_getNum());
	return -1;
    }

    if (PSnodes_lookupHost(ipaddr)!=-1) { /* duplicated host */
	parser_comment(-1, "duplicated host <%s>",
		       inet_ntoa(* (struct in_addr *) &ipaddr));
	return -1;
    }

    if (PSnodes_getAddr(id) != INADDR_ANY) { /* duplicated PSI-ID */
	unsigned int addr = PSnodes_getAddr(id);
	parser_comment(-1, "duplicated ID <%d> for hosts <%s> and <%s>",
		       id, inet_ntoa(* (struct in_addr *) &ipaddr),
		       inet_ntoa(* (struct in_addr *) &addr));
	return -1;
    }

    /* install hostname */
    if (PSnodes_register(id, ipaddr)) {
	parser_comment(-1, "PSnodes_register(%d, <%s>) failed",
		       id, inet_ntoa(* (struct in_addr *) &ipaddr));
	return -1;
    }

    if (PSnodes_setHWType(id, hwtype)) {
	parser_comment(-1, "PSnodes_setHWType(%d, %d) failed", id, hwtype);
	return -1;
    }

    if (PSnodes_setExtraIP(id, extraIP)) {
	parser_comment(-1, "PSnodes_setExtraIP(%d, %d) failed", id, extraIP);
	return -1;
    }

    if (PSnodes_setRunJobs(id, jobs)) {
	parser_comment(-1, "PSnodes_setRunJobs(%d, %d) failed", id, jobs);
	return -1;
    }

    if (PSnodes_setIsStarter(id, starter)) {
	parser_comment(-1, "PSnodes_setIsStarter(%d, %d) failed", id, starter);
	return -1;
    }

    if (PSnodes_setUser(id, uid)) {
	parser_comment(-1, "PSnodes_setUser(%d, %d) failed", id, uid);
	return -1;
    }

    if (PSnodes_setGroup(id, gid)) {
	parser_comment(-1, "PSnodes_setGroup(%d, %d) failed", id, gid);
	return -1;
    }

    if (PSnodes_setProcs(id, procs)) {
	parser_comment(-1, "PSnodes_setProcs(%d, %d) failed", id, procs);
	return -1;
    }

    if (PSnodes_setOverbook(id, overbook)) {
	parser_comment(-1, "PSnodes_setOverbook(%d, %d) failed", id, overbook);
	return -1;
    }

    nodesfound++;

    if (nodesfound > PSnodes_getNum()) { /* more hosts than nodes ??? */
	parser_comment(-1, "NrOfNodes = %d does not match number of"
		       " hosts in list (%d)", PSnodes_getNum(), nodesfound);
	return -1;
    }

    parser_comment(PARSER_LOG_VERB,
		   "%s: host <%s> inserted in hostlist with id=%d.",
		   __func__, inet_ntoa(* (struct in_addr *) &ipaddr), id);

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
	parser_comment(-1, "%s: %s", dname, strerror(errno));
	return -1;
    }

    if (!S_ISDIR(fstat.st_mode)) {
	parser_comment(-1, "'%s' is not a directory", dname);
	return -1;
    }

    PSC_setInstalldir(dname);
    if (strcmp(dname, PSC_lookupInstalldir())) {
	parser_comment(-1, "'%s' seems to be no valid installdir", dname);
	return -1;
    }

    return 0;
}

static int getNumNodes(char *token)
{
    int num, ret;

    if (PSnodes_getNum() != -1) {
	/* NrOfNodes already defined */
	parser_comment(-1, "define NrOfNodes only once");
	return -1;
    }

    ret = parser_getNumValue(parser_getString(), &num, "number of nodes");

    if (ret) return ret;

    /* Initialize the PSnodes module */
    ret = PSnodes_init(num);
    if (ret) {
	parser_comment(-1, "PSnodes_init(%d) failed", num);
    }

    return ret;
}

static int getLicServer(char *token)
{
    parser_getString(); /* Throw away the license server's name */
    parser_comment(-1, "definition of license server is obsolete");

    return 0;
}

static int getLicFile(char *token)
{
    parser_getString(); /* Throw away the license file's name */
    parser_comment(-1, "definition of license file is obsolete");

    return 0;
}

static int getMCastUse(char *token)
{
    config.useMCast = 1;
    parser_comment(-1, "will use MCast. Disable alternative status control");
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

static int getLogMask(char *token)
{
    return parser_getNumValue(parser_getString(), &config.logMask, "logmask");
}

static int destDaemon(char *token)
{
    config.logDest = LOG_DAEMON;
    return 0;
}

static int destKern(char *token)
{
    config.logDest = LOG_KERN;
    return 0;
}

static int destLocal0(char *token)
{
    config.logDest = LOG_LOCAL0;
    return 0;
}

static int destLocal1(char *token)
{
    config.logDest = LOG_LOCAL1;
    return 0;
}

static int destLocal2(char *token)
{
    config.logDest = LOG_LOCAL2;
    return 0;
}

static int destLocal3(char *token)
{
    config.logDest = LOG_LOCAL3;
    return 0;
}

static int destLocal4(char *token)
{
    config.logDest = LOG_LOCAL4;
    return 0;
}

static int destLocal5(char *token)
{
    config.logDest = LOG_LOCAL5;
    return 0;
}

static int destLocal6(char *token)
{
    config.logDest = LOG_LOCAL6;
    return 0;
}

static int destLocal7(char *token)
{
    config.logDest = LOG_LOCAL7;
    return 0;
}

static int endList(char *token)
{
    return -1;
}

static keylist_t dest_list[] = {
    {"daemon", destDaemon},
    {"kernel", destKern},
    {"local0", destLocal0},
    {"local1", destLocal1},
    {"local2", destLocal2},
    {"local3", destLocal3},
    {"local4", destLocal4},
    {"local5", destLocal5},
    {"local6", destLocal6},
    {"local7", destLocal7},
    {NULL, endList}
};

static parser_t dest_parser = {" \t\n", dest_list};

static int getLogDest(char *token)
{
    int ret;
    char skip_it[] = "log_";

    /* Get next token to parse */
    token = parser_getString();

    /* Ignore heading log_, so e.g. log_local0 and local0 are both valid */
    if (strncasecmp(token, skip_it, strlen(skip_it)) == 0) {
	token += strlen(skip_it);
    }

    ret = parser_parseString(token, &dest_parser);

    if (ret) {
	ret = parser_getNumValue(token, &config.logDest, "log destination");
    }

    return ret;
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
	parser_comment(PARSER_LOG_RES, "got 'RLIM_INFINITY' for '%s'",valname);
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

    setLimit(RLIMIT_RSS, value);

    return 0;
}

static int getRLimitMemLock(char *token)
{
    rlim_t value;
    int ret;

    return getRLimitVal(parser_getString(), &value, "RLimit MemLock");
    if (ret) return ret;

    setLimit(RLIMIT_MEMLOCK, value*1024);

    return 0;
}

static int getRLimitCore(char *token)
{
    rlim_t value;
    int ret;

    return getRLimitVal(parser_getString(), &value, "RLimit Core");
    if (ret) return ret;

    setLimit(RLIMIT_CORE, value*1024);

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
    {"memlock", getRLimitMemLock},
    {"core", getRLimitCore},
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
    {"memlock", getRLimitMemLock},
    {"core", getRLimitCore},
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

    parser_comment(PARSER_LOG_RES, "setting default HWType to '%s'",
		   HW_printType(hwtype));

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

    parser_comment(PARSER_LOG_RES, "setting default 'CanStart' to '%s'",
		   canstart ? "TRUE" : "FALSE");

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

    parser_comment(PARSER_LOG_RES, "setting default 'RunJobs' to '%s'",
		   runjobs ? "TRUE" : "FALSE");

    return ret;
}

static unsigned int node_extraIP;

static int getExtraIP(char *token)
{
    node_extraIP = parser_getHostname(parser_getString());

    if (!node_extraIP) return -1;

    return 0;
}

/* from bin/admin/adminparser.c */
static uid_t uidFromString(char *user)
{
    long tmp = parser_getNumber(user);
    struct passwd *passwd = getpwnam(user);

    if (strcasecmp(user, "any") == 0) return -1;
    if (tmp > -1) return tmp;
    if (passwd) return passwd->pw_uid;

    return -2;
}

/* from bin/admin/adminparser.c */
static gid_t gidFromString(char *group)
{
    long tmp = parser_getNumber(group);
    struct group *grp = getgrnam(group);

    if (strcasecmp(group, "any") == 0) return -1;
    if (tmp > -1) return tmp;
    if (grp) return grp->gr_gid;

    return -2;
}

static uid_t uid = -1, node_uid;

static int getUser(char *token)
{
    char *user = parser_getString();
    node_uid = uidFromString(user);

    if ((int)node_uid < -1) {
	parser_comment(-1, "Unknown user '%s'", user);
	return -1;
    }

    return 0;
}

static int getUserLine(char *token)
{
    int ret;
    char *user;
    struct passwd *pwd;

    ret = getUser(token);

    uid = node_uid;

    if ((int)uid >= 0) {
	pwd = getpwuid(uid);
	user = pwd->pw_name;
    } else {
	user = "ANY";
    }
    parser_comment(PARSER_LOG_RES, "setting default 'User' to '%s'", user);

    return ret;
}

static uid_t gid = -1, node_gid;

static int getGroup(char *token)
{
    char *group = parser_getString();
    node_gid = gidFromString(group);

    if ((int)node_gid < -1) {
	parser_comment(-1, "Unknown group '%s'", group);
	return -1;
    }

    return 0;
}

static int getGroupLine(char *token)
{
    int ret;
    char *group;
    struct group *grp;

    ret = getGroup(token);

    gid = node_gid;

    if ((int)gid >= 0) {
	grp = getgrgid(uid);
	group = grp->gr_name;
    } else {
	group = "ANY";
    }
    parser_comment(PARSER_LOG_RES, "setting default 'Group' to '%s'", group);

    return ret;
}

static int procs = -1, node_procs;

static int getProcs(char *token)
{
    char *procStr = parser_getString();
    node_procs = parser_getNumber(procStr);

    if ((node_procs == -1) && strcasecmp(procStr, "any")) {
	parser_comment(-1, "Unknown number of processes '%s'", procStr);
	return -1;
    }

    return 0;
}

static int getProcsLine(char *token)
{
    int ret;

    ret = getProcs(token);

    procs = node_procs;

    if (procs == -1) {
	parser_comment(PARSER_LOG_RES, "setting default 'Processes' to 'ANY'");
    } else {
	parser_comment(PARSER_LOG_RES, "setting default 'Processes' to '%d'",
		       procs);
    }

    return ret;
}

static int overbook = 0, node_overbook;

static int getOB(char *token)
{
    return parser_getBool(parser_getString(), &node_overbook, "node_overbook");
}

static int getOBLine(char *token)
{
    int ret;

    ret = getOB(token);

    overbook = node_overbook;

    parser_comment(PARSER_LOG_RES, "setting default 'Overbook' to '%s'",
		   overbook ? "TRUE" : "FALSE");

    return ret;
}

/* ---------------------------------------------------------------------- */

static keylist_t nodeline_list[] = {
    {"hwtype", getHW},
    {"runjobs", getRJ},
    {"starter", getCS},
    {"extraip", getExtraIP},
    {"user", getUser},
    {"group", getGroup},
    {"processes", getProcs},
    {"overbook", getOB},
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
    node_uid = uid;
    node_gid = gid;
    node_procs = procs;
    node_overbook = overbook;

    ipaddr = parser_getHostname(token);

    if (ipaddr) {
	hostname = strdup(token);
    } else {
	return -1;
    }

    ret = parser_getNumValue(parser_getString(), &nodenum, "node number");

    if (ret) goto exit;

    ret = parser_parseString(parser_getString(), &nodeline_parser);

    if (ret) goto exit;

    if (parser_getDebugMask() & PARSER_LOG_NODE) {
	char procStr[20];
	if (node_procs==-1) {
	    snprintf(procStr, sizeof(procStr), "any");
	} else {
	    snprintf(procStr, sizeof(procStr), "%d", node_procs);
	}

	parser_comment(PARSER_LOG_NODE, "Register '%s' as %d with"
		       " HW '%s', %s procs, jobs%s allowed,"
		       " starting%s allowed, overbooking%s allowed.",
		       hostname, nodenum, HW_printType(node_hwtype),
		       procStr, node_runjobs ? "":" not",
		       node_canstart ? "":" not", node_overbook ? "":" not");
	if (node_extraIP) {
	    parser_comment(PARSER_LOG_NODE, " Myrinet IP will be <%s>.",
			   inet_ntoa(* (struct in_addr *) &node_extraIP));
	}
    }

    ret = installHost(ipaddr, nodenum, node_hwtype, node_extraIP,
		      node_runjobs, node_canstart, node_uid, node_gid,
		      node_procs, node_overbook);

 exit:
    free(hostname);

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
	parser_comment(-1, "empty line");
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
	parser_comment(-1, "no string found within '%s'", line);
	return NULL;
    }

    /* Skip trailing whitespace */
    while (*end==' ' || *end=='\t') end++;
    if (*end) {
	parser_comment(-1, "found trailing garbage '%s' %d", end, *end);
	if (value) free(value);
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
	parser_comment(-1, "syntax error");
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	parser_comment(-1, "premature end of line");
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	parser_comment(-1, "no value for %s", name);
	return -1;
    }

    /* store environment */
    setenv(name, value, 1);

    parser_comment(PARSER_LOG_RES, "got environment: %s='%s'", name , value);

    free(name);
    free(value);

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
	parser_comment(-1, "unknown script type '%s'", token);
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	parser_comment(-1, "premature end of line");
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	parser_comment(-1, "no value for %s", name);
	return -1;
    }

    /* store environment */
    if (HW_getScript(actHW, name)) {
	parser_comment(-1, "redefineing hardware script: %s", name);
    }
    HW_setScript(actHW, name, value);

    parser_comment(PARSER_LOG_RES, "got hardware script: %s='%s'",name, value);

    free(value);

    return 0;
}

static int getHardwareEnvLine(char *token)
{
    char *line;
    char *name, *value = NULL;

    if (token) {
	name = strdup(token);
    } else {
	parser_comment(-1, "syntax error");
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	parser_comment(-1, "premature end of line");
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	parser_comment(-1, "no value for %s", name);
	return -1;
    }

    /* store environment */
    if (HW_getEnv(actHW, name)) {
	parser_comment(-1, "redefineing hardware environment: %s", name);
    }
    HW_setEnv(actHW, name, value);

    parser_comment(PARSER_LOG_RES, "got hardware environment: %s='%s'\n",
		   name , value);

    free(name);
    free(value);

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

static int getHardware(char *token)
{
    char *name, *brace;
    int ret;

    name = parser_getString();

    actHW = HW_index(name);

    if (actHW == -1) {
	actHW = HW_add(name);

	parser_comment(PARSER_LOG_RES, "new hardware '%s' registered as %d",
		       name, actHW);
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
    parser_comment(-1, "suspended jobs will free their resources");
    return 0;
}
    
static int getHandleOldBins(char *token)
{
    config.handleOldBins = 1;
    parser_comment(-1, "recognize old binaries within resource management");
    return 0;
}

static char* origToken;

static int sortLoad1or15(char *token)
{
    config.nodesSort = PART_SORT_LOAD_1;
    if ((strcasecmp(origToken, "load15")==0)
	|| (strcasecmp(origToken, "load_15")==0)) {
	config.nodesSort = PART_SORT_LOAD_15;
    }
    return 0;
}

static int sortLoad5(char *token)
{
    config.nodesSort = PART_SORT_LOAD_5;
    return 0;
}

static int sortProcOrProcLoad(char *token)
{
    const char discr[]="proc+";
    config.nodesSort = PART_SORT_PROC;
    if (strncasecmp(origToken, discr, strlen(discr))==0) {
	config.nodesSort = PART_SORT_PROCLOAD;
    }
    return 0;
}

static int sortNone(char *token)
{
    config.nodesSort = PART_SORT_NONE;
    return 0;
}

static keylist_t sort_list[] = {
    {"load15", sortLoad1or15},
    {"load_15", sortLoad1or15},
    {"load5", sortLoad5},
    {"load_5", sortLoad5},
    {"proc+load", sortProcOrProcLoad},
    {"none", sortNone},
    {NULL, parser_error}
};

static parser_t sort_parser = {" \t\n", sort_list};

static int getPSINodesSort(char *token)
{
    int ret;

    origToken = parser_getString();
    ret = parser_parseString(origToken, &sort_parser);
    if (!ret) {
	parser_comment(-1, "default sorting strategy for nodes is '%s'",
		       (config.nodesSort == PART_SORT_PROC) ? "PROC" :
		       (config.nodesSort == PART_SORT_LOAD_1) ? "LOAD_1" :
		       (config.nodesSort == PART_SORT_LOAD_5) ? "LOAD_5" :
		       (config.nodesSort == PART_SORT_LOAD_15) ? "LOAD_15" :
		       (config.nodesSort == PART_SORT_PROCLOAD) ? "PROCLOAD" :
		       (config.nodesSort == PART_SORT_NONE) ? "NONE" :
		       "UNKNOWN");
    }
    return ret;
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
    {"user", getUserLine},
    {"group", getGroupLine},
    {"processes", getProcsLine},
    {"overbook", getOBLine},
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
    {"loglevel", getLogMask},
    {"logmask", getLogMask},
    {"logdestination", getLogDest},
    {"environment", getEnv},
    {"freeOnSuspend", getFreeOnSusp},
    {"handleOldBins", getHandleOldBins},
    {"psiNodesSort", getPSINodesSort},
    {NULL, parser_error}
};

static parser_t config_parser = {" \t\n", config_list};

config_t *parseConfig(FILE* logfile, int logmask, char *configfile)
{
    FILE *cfd;
    int ret;

    parser_init(logfile, NULL);

    if (!configfile) {
	parser_comment(-1, "no configuration file defined");
	return NULL;
    }
 
    if (!(cfd = fopen(configfile,"r"))) {
	parser_comment(-1, "unable to locate file <%s>", configfile);
	return NULL;
    }

    parser_setFile(cfd);
    parser_setDebugMask(logmask);
    parser_comment(PARSER_LOG_FILE, "using file <%s>", configfile);

    ret = parser_parseFile(&config_parser);

    if (ret) {
	parser_comment(-1, "ERROR: Parsing of <%s> failed.", configfile);
	return NULL;
    }

    fclose(cfd);

    /*
     * Sanity Checks
     */
    if (PSnodes_getNum()==-1) {
	parser_comment(-1, "ERROR: NrOfNodes not defined");
	return NULL;
    }

    /*
     * Sanity Checks
     */
    if (PSnodes_getNum() > nodesfound) { /* hosts missing in hostlist */
	parser_comment(-1, "WARNING: # hosts in hostlist less than NrOfNodes");
    }
    
    return &config;
}
