/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "config_parsing.h" // IWYU pragma: associated

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "parser.h"
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"
#include "pspartition.h"
#include "timer.h"
#include "rdp.h"
#include "selector.h"

#include "psidnodes.h"
#include "psidscripts.h"

static config_t config = {
    .coreDir = "/tmp",
    .selectTime = 2,
    .deadInterval = 10,
    .statusTimeout = 2000,
    .statusBroadcasts = 4,
    .deadLimit = 5,
    .RDPPort = 886,
    .RDPTimeout = 100,
    .useMCast = 0,
    .MCastGroup = 237,
    .MCastPort = 1889,
    .logMask = 0,
    .logDest = LOG_DAEMON,
    .logfile = NULL,
    .freeOnSuspend = 0,
    .nodesSort = PART_SORT_PROC,
    .killDelay = 10,
    .startupScript = NULL,
    .nodeUpScript = NULL,
    .nodeDownScript = NULL,
    .nodeListHash = 0,
};

#define ENV_END 17 /* Some magic value */

#define DEFAULT_ID -1
#define GENERATE_ID -2

static int currentID = DEFAULT_ID;

static int nodesfound = 0;

static int nrOfNodesGiven = 0;

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
    if (!dname) {
	parser_comment(-1, "directory name is empty\n");
	return -1;
    }

    if (stat(dname, &fstat)) {
	parser_comment(-1, "%s: %s\n", dname, strerror(errno));
	return -1;
    }

    if (!S_ISDIR(fstat.st_mode)) {
	parser_comment(-1, "'%s' is not a directory\n", dname);
	return -1;
    }

    if (strcmp(dname, PSC_lookupInstalldir(dname))) {
	parser_comment(-1, "'%s' seems to be no valid installdir\n", dname);
	return -1;
    }

    return 0;
}

static int getNumNodes(char *token)
{
    int num, ret;

    if (PSIDnodes_getNum() != -1) {
	/* NrOfNodes already defined */
	parser_comment(-1, "define NrOfNodes only once\n");
	return -1;
    }

    ret = parser_getNumValue(parser_getString(), &num, "number of nodes");
    if (ret) return ret;

    /* Initialize the PSIDnodes module */
    ret = PSIDnodes_init(num);
    nrOfNodesGiven = 1;
    if (ret) {
	parser_comment(-1, "PSIDnodes_init(%d) failed\n", num);
    }
    parser_comment(-1, "definition of NrOfNodes is obsolete\n");

    return ret;
}

static int getLicServer(char *token)
{
    parser_getString(); /* Throw away the license server's name */
    parser_comment(-1, "definition of license server is obsolete\n");

    return 0;
}

static int getLicFile(char *token)
{
    parser_getString(); /* Throw away the license file's name */
    parser_comment(-1, "definition of license file is obsolete\n");

    return 0;
}

static int getMCastUse(char *token)
{
    config.useMCast = 1;
    parser_comment(-1, "will use MCast. Disable alternative status control\n");
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

static int getRDPTimeout(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp, "RDP timeout");
    if (ret) return ret;

    if (tmp < MIN_TIMEOUT_MSEC) {
	parser_comment(-1, "RDP timeout %d too small. Ignoring...\n", tmp);
    } else {
	config.RDPTimeout = tmp;
    }
    return 0;
}

static int getRDPMaxRetrans(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp,
				 "RDP maximum retransmissions");
    if (ret) return ret;

    setMaxRetransRDP(tmp);
    return 0;
}

static int getRDPResendTimeout(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp,
				 "RDP resend timeout");
    if (ret) return ret;

    setRsndTmOutRDP(tmp);
    return 0;
}

static int getRDPClosedTimeout(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp,
				 "RDP closed timeout");
    if (ret) return ret;

    setClsdTmOutRDP(tmp);
    return 0;
}

static int getRDPMaxACKPend(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp,
				 "RDP maximum pending ACKs");
    if (ret) return ret;

    setMaxAckPendRDP(tmp);
    return 0;
}

static int getRDPStatistics(char *token)
{
    int tmp;
    int ret = parser_getBool(parser_getString(), &tmp, "RDP statistics");
    if (ret) return ret;

    RDP_setStatistics(tmp);
    return 0;
}

static int getSelectTime(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp, "select time");
    if (ret) return ret;

    config.selectTime = tmp;
    return 0;
}

static int getDeadInterval(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp, "dead interval");
    if (ret) return ret;

    config.deadInterval = tmp;
    return 0;
}

static int getStatTmout(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp, "status timeout");
    if (ret) return ret;

    if (tmp < MIN_TIMEOUT_MSEC) {
	parser_comment(-1, "status timeout %d too small. Ignoring...\n", tmp);
    } else {
	config.statusTimeout = tmp;
    }
    return 0;
}

static int getStatBcast(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp, "status broadcasts");
    if (ret) return ret;

    if (tmp < 0) {
	parser_comment(-1, "status broadcasts must be positive. Ignoring...\n");
    } else {
	config.statusBroadcasts = tmp;
    }

    return 0;
}

static int getDeadLmt(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp, "dead limit");
    if (ret) return ret;

    config.deadLimit = tmp;
    return 0;
}

static int getKillDelay(char *token)
{
    int tmp;
    int ret = parser_getNumValue(parser_getString(), &tmp, "kill delay");
    if (ret) return ret;

    config.killDelay = tmp;
    return 0;
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
    {"daemon", destDaemon, NULL},
    {"kernel", destKern, NULL},
    {"local0", destLocal0, NULL},
    {"local1", destLocal1, NULL},
    {"local2", destLocal2, NULL},
    {"local3", destLocal3, NULL},
    {"local4", destLocal4, NULL},
    {"local5", destLocal5, NULL},
    {"local6", destLocal6, NULL},
    {"local7", destLocal7, NULL},
    {NULL, endList, NULL}
};

static parser_t dest_parser = {" \t\n", dest_list};

static int getLogDest(char *token)
{
    int ret;
    char skip_it[] = "log_";

    /* Get next token to parse */
    token = parser_getString();
    if (!token) {
	parser_comment(-1, "empty destination\n");
	return -1;
    }

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

    if (!token) {
	parser_comment(-1, "empty rlimit\n");
	return -1;
    }
    if (strncasecmp(token, skip_it, sizeof(skip_it)) == 0) {
	token += sizeof(skip_it);
    }

    if (strcasecmp(token,"infinity")==0 || strcasecmp(token, "unlimited")==0) {
	*value = RLIM_INFINITY;
	parser_comment(PARSER_LOG_RES, "got 'RLIM_INFINITY' for '%s'\n",
		       valname);
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
    if ( value == RLIM_INFINITY
	 || (value > rlp.rlim_max && rlp.rlim_max != RLIM_INFINITY)) {
	rlp.rlim_max=value;
    }

    if (setrlimit(limit, &rlp)) {
	char *errstr = strerror(errno);
	parser_comment(-1, "%s: setres failed: %s\n",
		       __func__, errstr ? errstr : "UNKNOWN");
    } else {
	/* We might have to inform other facilities, too */
	switch (limit) {
	case RLIMIT_NOFILE:
	    if (value == RLIM_INFINITY) {
		parser_exit(EINVAL, "%s: cannot handle unlimited files",
			    __func__);
		break;
	    }
	    if (Selector_setMax(value) < 0) {
		parser_exit(errno, "%s: Failed to adapt Selector", __func__);
	    }
	    break;
	default:
	    /* nothing to do */
	    break;
	}
    }
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

    setLimit(RLIMIT_DATA, (value == RLIM_INFINITY) ? value : value*1024);

    return 0;
}

static int getRLimitStack(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit StackSize");
    if (ret) return ret;

    setLimit(RLIMIT_STACK, (value == RLIM_INFINITY) ? value : value*1024);

    return 0;
}

static int getRLimitRSS(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit RSSize");
    if (ret) return ret;

    setLimit(RLIMIT_RSS, value);

    return 0;
}

static int getRLimitMemLock(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit MemLock");
    if (ret) return ret;

    setLimit(RLIMIT_MEMLOCK, (value == RLIM_INFINITY) ? value : value*1024);

    return 0;
}

static char rlimitCoreGiven = 0;

static int getRLimitCore(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit Core");
    if (ret) return ret;

    setLimit(RLIMIT_CORE, (value == RLIM_INFINITY) ? value : value*1024);
    rlimitCoreGiven = 1;

    return 0;
}

static int getRLimitNoFile(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit NoFile");
    if (ret) return ret;

    setLimit(RLIMIT_NOFILE, value);
    rlimitCoreGiven = 1;

    return 0;
}

static int endRLimitEnv(char *token)
{
    return ENV_END;
}

static keylist_t rlimitenv_list[] = {
    {"cputime", getRLimitCPU, NULL},
    {"datasize", getRLimitData, NULL},
    {"stacksize", getRLimitStack, NULL},
    {"rssize", getRLimitRSS, NULL},
    {"memlock", getRLimitMemLock, NULL},
    {"core", getRLimitCore, NULL},
    {"nofile", getRLimitNoFile, NULL},
    {"}", endRLimitEnv, NULL},
    {NULL, parser_error, NULL}
};

static parser_t rlimitenv_parser = {" \t\n", rlimitenv_list};

static int getRLimitEnv(char *token)
{
    return parser_parseOn(parser_getString(), &rlimitenv_parser);
}

static keylist_t rlimit_list[] = {
    {"{", getRLimitEnv, NULL},
    {"cputime", getRLimitCPU, NULL},
    {"datasize", getRLimitData, NULL},
    {"stacksize", getRLimitStack, NULL},
    {"rssize", getRLimitRSS, NULL},
    {"memlock", getRLimitMemLock, NULL},
    {"core", getRLimitCore, NULL},
    {"nofile", getRLimitNoFile, NULL},
    {NULL, parser_error, NULL}
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

static int getCoreDir(char *token)
{
    static char *usedDir = NULL;
    char *dname;
    struct stat fstat;

    dname = parser_getString();
    /* test if dir is a valid directory */
    if (!dname) {
	parser_comment(-1, "directory name is empty\n");
	return -1;
    }

    if (stat(dname, &fstat)) {
	parser_comment(-1, "%s: %s\n", dname, strerror(errno));
	return -1;
    }

    if (!S_ISDIR(fstat.st_mode)) {
	parser_comment(-1, "'%s' is not a directory\n", dname);
	return -1;
    }

    config.coreDir = strdup(dname);
    free(usedDir);
    usedDir = config.coreDir;
    if (!rlimitCoreGiven) setLimit(RLIMIT_CORE, RLIM_INFINITY);

    parser_comment(PARSER_LOG_RES, "set coreDir to '%s'\n", dname);

    return 0;
}

static int default_hwtype = 0, node_hwtype, hwtype;

static int setHWType(int hw)
{
    if (currentID == DEFAULT_ID) {
	default_hwtype = hw;
	parser_comment(PARSER_LOG_NODE, "setting default HWType to '%s'\n",
		       HW_printType(hw));
    } else {
	node_hwtype = hw;
	parser_comment(PARSER_LOG_NODE, " HW '%s'", HW_printType(hw));
    }
    return 0;
}

static int getHWnone(char *token)
{
    return setHWType(0);
}

static int getHWent(char *token)
{
    int idx = HW_index(token);

    if (idx < 0) return parser_error(token);

    hwtype |= 1<<idx;

    return 0;
}

static int getHWsingle(char *token)
{
    int ret;

    hwtype = 0;

    ret = getHWent(token);
    if (ret) return ret;

    return setHWType(hwtype);
}

static int endHWEnv(char *token)
{
    int ret = setHWType(hwtype);
    if (ret) return ret;

    return ENV_END;
}

static keylist_t hwenv_list[] = {
    {"none", getHWnone, NULL},
    {"}", endHWEnv, NULL},
    {NULL, getHWent, NULL}
};

static parser_t hwenv_parser = {" \t\n", hwenv_list};

static int getHWEnv(char *token)
{
    hwtype = 0;
    return parser_parseOn(parser_getString(), &hwenv_parser);
}

static keylist_t hw_list[] = {
    {"{", getHWEnv, NULL},
    {"none", getHWnone, NULL},
    {NULL, getHWsingle, NULL}
};

static parser_t hw_parser = {" \t\n", hw_list};

static int getHW(char *token)
{
    int ret = parser_parseToken(parser_getString(), &hw_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

static int default_canstart = 1, node_canstart;

static int getCS(char *token)
{
    int cs, ret;

    ret = parser_getBool(parser_getString(), &cs, "canstart");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_canstart = cs;
	parser_comment(PARSER_LOG_NODE, "setting default 'CanStart' to '%s'\n",
		       cs ? "TRUE" : "FALSE");
    } else {
	node_canstart = cs;
	parser_comment(PARSER_LOG_NODE, " starting%s allowed",
		       cs ? "":" not");
    }
    return 0;
}

static int default_runjobs = 1, node_runjobs;

static int getRJ(char *token)
{
    int rj, ret;

    ret = parser_getBool(parser_getString(), &rj, "runjobs");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_runjobs = rj;
	parser_comment(PARSER_LOG_NODE, "setting default 'RunJobs' to '%s'\n",
		       rj ? "TRUE" : "FALSE");
    } else {
	node_runjobs = rj;
	parser_comment(PARSER_LOG_NODE, " jobs%s allowed", rj ? "":" not");
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

/** List type to store group/user entries */
typedef struct {
    struct list_head next;
    unsigned int id;
} GUent_t;

static LIST_HEAD(defaultUID);
static LIST_HEAD(nodeUID);
static LIST_HEAD(defaultGID);
static LIST_HEAD(nodeGID);
static LIST_HEAD(defaultAdmUID);
static LIST_HEAD(nodeAdmUID);
static LIST_HEAD(defaultAdmGID);
static LIST_HEAD(nodeAdmGID);

/**
 * @brief Clear list of GUIDs
 *
 * Clear the list of GUIDs @a list, i.e. remove all entries from the
 * list and free() the allocated memory.
 *
 * @param list The list to clear.
 *
 * @return No return value.
 */
static void clear_GUIDlist(list_t *list)
{
    list_t *pos, *tmp;

    list_for_each_safe(pos, tmp, list) {
	GUent_t *guent = list_entry(pos, GUent_t, next);
	list_del(pos);
	ASSUME(pos != list->next); // hint to Clang's static analyzer
	free(guent);
    }
}

/**
 * @brief Copy list of GUIDs
 *
 * Create a copy of the list of GUIDs @a src, and store it to the
 * corresponding list @a dest. @a dest will be cleared using @ref
 * clear_GUIDlist() before using it.
 *
 * @param src The list to copy.
 *
 * @param src The destination of the copied list-items.
 *
 * @return On success, 0 is returned. Or -1, if any error occurred.
 */
static int copy_GUIDlist(list_t *src, list_t *dest)
{
    list_t *pos;

    clear_GUIDlist(dest);

    list_for_each(pos, src) {
	GUent_t *old = list_entry(pos, GUent_t, next);
	GUent_t *new = malloc(sizeof(*new));
	if (!new) {
	    parser_comment(-1, "%s: No memory\n", __func__);
	    return -1;
	}
	new->id = old->id;
	list_add_tail(&new->next, dest);
    }
    return 0;
}

static int addID(list_t *list, unsigned int id);

static int setID(list_t *list, unsigned int id)
{
    if (!list) return -1;

    clear_GUIDlist(list);

    return addID(list, id);
}

static int addID(list_t *list, unsigned int id)
{
    GUent_t *guent;
    unsigned int any = PSNODES_ANYUSER;
    list_t *pos, *tmp;

    if (!list) return -1;

    if (list==&defaultGID || list==&defaultAdmGID) any = PSNODES_ANYGROUP;
    if (id == any) clear_GUIDlist(list);

    list_for_each_safe(pos, tmp, list) {
	guent = list_entry(pos, GUent_t, next);
	if (guent->id == any) {
	    parser_comment(-1, "%s(%p, %d): ANY found\n", __func__, list, id);
	    return -1;
	}
	if (guent->id == id) {
	    parser_comment(-1, "%s(%p, %d): already there\n",
			   __func__, list, id);
	    return -1;
	}
    }

    guent = malloc(sizeof(*guent));
    if (!guent) {
	parser_comment(-1, "%s: No memory\n", __func__);
	return -1;
    }
    guent->id = id;
    list_add_tail(&guent->next, list);

    return 0;
}

static int remID(list_t *list, unsigned int id)
{
    list_t *pos, *tmp;

    list_for_each_safe(pos, tmp, list) {
	GUent_t *guent = list_entry(pos, GUent_t, next);
	if (guent->id == id) {
	    list_del(pos);
	    free(guent);
	    return 0;
	}
    }
    parser_comment(-1, "%s(%p, %d): not found\n", __func__, list, id);
    return -1;
}

static int pushGUID(PSnodes_ID_t id, PSIDnodes_gu_t what, list_t *list)
{
    list_t *pos;
    PSIDnodes_guid_t any;

    switch (what) {
    case PSIDNODES_USER:
    case PSIDNODES_ADMUSER:
	any.u = PSNODES_ANYUSER;
	break;
    case PSIDNODES_GROUP:
    case PSIDNODES_ADMGROUP:
	any.g = PSNODES_ANYGROUP;
	break;
    }

    PSIDnodes_setGUID(id, what, any);
    PSIDnodes_remGUID(id, what, any);

    list_for_each(pos, list) {
	PSIDnodes_guid_t val = { .u = 0 };
	GUent_t *guent = list_entry(pos, GUent_t, next);

	switch (what) {
	case PSIDNODES_USER:
	case PSIDNODES_ADMUSER:
	    val.u = guent->id;
	break;
	case PSIDNODES_GROUP:
	case PSIDNODES_ADMGROUP:
	    val.g = guent->id;
	    break;
	}
	if (PSIDnodes_addGUID(id, what, val)) {
	    parser_comment(-1, "%s(%d, %d, %p): failed\n",
			   __func__, id, what, list);
	    return -1;
	}
    }

    return 0;
}

static int (*GUIDaction)(list_t *, unsigned int);

static void setAction(char **token, char **actionStr)
{
    switch (**token) {
    case '+':
	GUIDaction = addID;
	*actionStr = "+";
	(*token)++;
	break;
    case '-':
	GUIDaction = remID;
	*actionStr = "-";
	(*token)++;
	break;
    default:
	GUIDaction = addID;
	*actionStr = "";
	break;
    }
}

/* ---------------------------------------------------------------------- */

static int getSingleUser(char *user)
{
    char *actStr, *uStr;
    uid_t uid;

    if (!user) {
	parser_comment(-1, "Empty user\n");
	return -1;
    }

    setAction(&user, &actStr);

    uid = PSC_uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return -1;
    }

    uStr = PSC_userFromUID(uid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE,
		       "setting default 'User' to '%s%s'\n", actStr, uStr);
	GUIDaction(&defaultUID, uid);
    } else {
	parser_comment(PARSER_LOG_NODE, " user '%s%s'", actStr, uStr);
	GUIDaction(&nodeUID, uid);
    }
    free(uStr);
    return 0;
}

static int endUserEnv(char *token)
{
    return ENV_END;
}

static keylist_t userenv_list[] = {
    {"}", endUserEnv, NULL},
    {NULL, getSingleUser, NULL}
};

static parser_t userenv_parser = {" \t\n", userenv_list};

static int getUserEnv(char *token)
{
    return parser_parseOn(parser_getString(), &userenv_parser);
}

static keylist_t user_list[] = {
    {"{", getUserEnv, NULL},
    {NULL, getSingleUser, NULL}
};

static parser_t user_parser = {" \t\n", user_list};

static int getUser(char *token)
{
    int ret = parser_parseToken(parser_getString(), &user_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

/* ---------------------------------------------------------------------- */

static int getSingleGroup(char *group)
{
    char *actStr, *gStr;
    gid_t gid;

    if (!group) {
	parser_comment(-1, "Empty group\n");
	return -1;
    }

    setAction(&group, &actStr);

    gid = PSC_gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return -1;
    }

    gStr = PSC_groupFromGID(gid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE,
		       "setting default 'Group' to '%s%s'\n", actStr, gStr);
	GUIDaction(&defaultGID, gid);
    } else {
	parser_comment(PARSER_LOG_NODE, " group '%s%s'", actStr, gStr);
	GUIDaction(&nodeGID, gid);
    }
    free(gStr);
    return 0;
}

static int endGroupEnv(char *token)
{
    return ENV_END;
}

static keylist_t groupenv_list[] = {
    {"}", endGroupEnv, NULL},
    {NULL, getSingleGroup, NULL}
};

static parser_t groupenv_parser = {" \t\n", groupenv_list};

static int getGroupEnv(char *token)
{
    return parser_parseOn(parser_getString(), &groupenv_parser);
}

static keylist_t group_list[] = {
    {"{", getGroupEnv, NULL},
    {NULL, getSingleGroup, NULL}
};

static parser_t group_parser = {" \t\n", group_list};

static int getGroup(char *token)
{
    int ret = parser_parseToken(parser_getString(), &group_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

/* ---------------------------------------------------------------------- */

static int getSingleAdminUser(char *user)
{
    char *actStr, *uStr;
    uid_t uid;

    if (!user) {
	parser_comment(-1, "Empty user\n");
	return -1;
    }

    setAction(&user, &actStr);

    uid = PSC_uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return -1;
    }

    uStr = PSC_userFromUID(uid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE,
		       "setting default 'AdminUser' to '%s%s'\n", actStr, uStr);
	GUIDaction(&defaultAdmUID, uid);
    } else {
	parser_comment(PARSER_LOG_NODE, " adminuser '%s%s'", actStr, uStr);
	GUIDaction(&nodeAdmUID, uid);
    }
    free(uStr);
    return 0;
}

static int endAdminUserEnv(char *token)
{
    return ENV_END;
}

static keylist_t admuserenv_list[] = {
    {"}", endAdminUserEnv, NULL},
    {NULL, getSingleAdminUser, NULL}
};

static parser_t admuserenv_parser = {" \t\n", admuserenv_list};

static int getAdminUserEnv(char *token)
{
    return parser_parseOn(parser_getString(), &admuserenv_parser);
}

static keylist_t admuser_list[] = {
    {"{", getAdminUserEnv, NULL},
    {NULL, getSingleAdminUser, NULL}
};

static parser_t admuser_parser = {" \t\n", admuser_list};

static int getAdminUser(char *token)
{
    int ret = parser_parseToken(parser_getString(), &admuser_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

/* ---------------------------------------------------------------------- */

static int getSingleAdminGroup(char *group)
{
    char *actStr, *gStr;
    gid_t gid;

    if (!group) {
	parser_comment(-1, "Empty group\n");
	return -1;
    }

    setAction(&group, &actStr);

    gid = PSC_gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return -1;
    }

    gStr = PSC_groupFromGID(gid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE,
		       "setting default 'AdminGroup' to '%s%s'\n", actStr,gStr);
	GUIDaction(&defaultAdmGID, gid);
    } else {
	parser_comment(PARSER_LOG_NODE, " admingroup '%s%s'", actStr, gStr);
	GUIDaction(&nodeAdmGID, gid);
    }
    free(gStr);
    return 0;
}

static int endAdminGroupEnv(char *token)
{
    return ENV_END;
}

static keylist_t admgroupenv_list[] = {
    {"}", endAdminGroupEnv, NULL},
    {NULL, getSingleAdminGroup, NULL}
};

static parser_t admgroupenv_parser = {" \t\n", admgroupenv_list};

static int getAdminGroupEnv(char *token)
{
    return parser_parseOn(parser_getString(), &admgroupenv_parser);
}

static keylist_t admgroup_list[] = {
    {"{", getAdminGroupEnv, NULL},
    {NULL, getSingleAdminGroup, NULL}
};

static parser_t admgroup_parser = {" \t\n", admgroup_list};

static int getAdminGroup(char *token)
{
    int ret = parser_parseToken(parser_getString(), &admgroup_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

/* ---------------------------------------------------------------------- */

static long default_procs = -1, node_procs;

static int getProcs(char *token)
{
    char *procStr = parser_getString();
    long procs = -1;

    if (parser_getNumber(procStr, &procs) && strcasecmp(procStr, "any")) {
	parser_comment(-1, "Unknown number of processes '%s'\n", procStr);
	return -1;
    }

    if (currentID == DEFAULT_ID) {
	default_procs = procs;
	parser_comment(PARSER_LOG_NODE, "setting default 'Processes' to '");
	if (procs == -1) {
	    parser_comment(PARSER_LOG_NODE, "ANY");
	} else {
	    parser_comment(PARSER_LOG_NODE, "%ld", procs);
	}
	parser_comment(PARSER_LOG_NODE, "'\n");
    } else {
	node_procs = procs;
	if (procs == -1) {
	    parser_comment(PARSER_LOG_NODE, " any");
	} else {
	    parser_comment(PARSER_LOG_NODE, " %ld", procs);
	}
	parser_comment(PARSER_LOG_NODE, " procs");
    }
    return 0;
}

static PSnodes_overbook_t default_overbook = OVERBOOK_FALSE, node_overbook;

static int getOB(char *token)
{
    char *obStr = parser_getString();
    int ob, ret;

    if (!obStr) {
	parser_comment(-1, "overbook-value is empty\n");
	return -1;
    }
    if (strcasecmp(obStr, "auto") == 0) {
	ob = OVERBOOK_AUTO;
	parser_comment(PARSER_LOG_NODE, "got 'auto' for value 'overbook'\n");
	ret = 0;
    } else {
	ret = parser_getBool(obStr, &ob, "overbook");
    }
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_overbook = ob;
	parser_comment(PARSER_LOG_NODE, "setting default 'Overbook' to '%s'\n",
		       (ob==OVERBOOK_AUTO) ? "auto" : ob ? "TRUE" : "FALSE");
    } else {
	node_overbook = ob;
	parser_comment(PARSER_LOG_NODE, " overbooking is '%s'",
		       ob==OVERBOOK_AUTO ? "auto" : ob ? "TRUE" : "FALSE");
    }
    return 0;
}

static int default_exclusive = 1, node_exclusive;

static int getExcl(char *token)
{
    int excl, ret;

    ret = parser_getBool(parser_getString(), &excl, "exclusive");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_exclusive = excl;
	parser_comment(PARSER_LOG_NODE,"setting default 'Exclusive' to '%s'\n",
		       excl ? "TRUE" : "FALSE");
    } else {
	node_exclusive = excl;
	parser_comment(PARSER_LOG_NODE, " exclusive assign%s allowed",
		       excl ? "":" not");
    }
    return 0;
}

static int default_pinProcs = 1, node_pinProcs;

static int getPinProcs(char *token)
{
    int pinProcs, ret;

    ret = parser_getBool(parser_getString(), &pinProcs, "pinProcs");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_pinProcs = pinProcs;
	parser_comment(PARSER_LOG_NODE, "setting default 'PinProcs' to '%s'\n",
		       pinProcs ? "TRUE" : "FALSE");
    } else {
	node_pinProcs = pinProcs;
	parser_comment(PARSER_LOG_NODE, " processes are%s pinned",
		       pinProcs ? "":" not");
    }
    return 0;
}

static int default_bindMem = 1, node_bindMem;

static int getBindMem(char *token)
{
    int bindMem, ret;

    ret = parser_getBool(parser_getString(), &bindMem, "bindMem");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_bindMem = bindMem;
	parser_comment(PARSER_LOG_NODE, "setting default 'BindMem' to '%s'\n",
		       bindMem ? "TRUE" : "FALSE");
    } else {
	node_bindMem = bindMem;
	parser_comment(PARSER_LOG_NODE, " memory is%s bound",
		       bindMem ? "":" not");
    }
    return 0;
}

static int default_bindGPUs = 1, node_bindGPUs;

static int getBindGPUs(char *token)
{
    int bindGPUs, ret;

    ret = parser_getBool(parser_getString(), &bindGPUs, "bindGPUs");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_bindGPUs = bindGPUs;
	parser_comment(PARSER_LOG_NODE, "setting default 'BindGPUs' to '%s'\n",
		       bindGPUs ? "TRUE" : "FALSE");
    } else {
	node_bindGPUs = bindGPUs;
	parser_comment(PARSER_LOG_NODE, " GPUs get%s bound",
		       bindGPUs ? "":" not");
    }
    return 0;
}

static int default_allowUserMap = 0, node_allowUserMap;

static int getAllowUserMap(char *token)
{
    int allowMap, ret;

    ret = parser_getBool(parser_getString(), &allowMap, "allowUserMap");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_allowUserMap = allowMap;
	parser_comment(PARSER_LOG_NODE, "setting default 'AllowUserMap' to"
		       " '%s'\n", allowMap ? "TRUE" : "FALSE");
    } else {
	node_allowUserMap = allowMap;
	parser_comment(PARSER_LOG_NODE, " user's CPU-mapping is%s allowed",
		       allowMap ? "":" not");
    }
    return 0;
}

static int default_supplGrps = 0, node_supplGrps;

static int getSupplGrps(char *token)
{
    int supplGrps, ret;

    ret = parser_getBool(parser_getString(), &supplGrps, "supplGrps");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_supplGrps = supplGrps;
	parser_comment(PARSER_LOG_NODE, "setting default 'supplGrps' to '%s'\n",
		       supplGrps ? "TRUE" : "FALSE");
    } else {
	node_supplGrps = supplGrps;
	parser_comment(PARSER_LOG_NODE, " supplementary groups are%s set",
		       supplGrps ? "":" not");
    }
    return 0;
}

static int default_maxStatTry = 1, node_maxStatTry;

static int getMaxStatTry(char *token)
{
    int try, ret;

    ret = parser_getNumValue(parser_getString(), &try, "maxStatTry");
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	default_maxStatTry = try;
	parser_comment(PARSER_LOG_NODE,
		       "setting default 'maxStatTry' to %d\n", try);
    } else {
	node_maxStatTry = try;
	parser_comment(PARSER_LOG_NODE, " maxStatTry are '%d'", try);
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

static short std_cpumap[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static short *default_cpumap = std_cpumap;
static size_t default_cpumap_size = 16, default_cpumap_maxsize;
static short *node_cpumap = NULL;
static size_t node_cpumap_size = 0, node_cpumap_maxsize = 0;

static int getCPUmapEnt(char *token)
{
    long val;
    int ret;

    ret = parser_getNumber(token, &val);
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	if (default_cpumap == std_cpumap) {
	    default_cpumap_maxsize = 16;
	    default_cpumap = malloc(default_cpumap_maxsize
				    * sizeof(*default_cpumap));
	} else if (default_cpumap_size == default_cpumap_maxsize) {
	    default_cpumap_maxsize *= 2;
	    short *oldMap = default_cpumap;
	    default_cpumap = realloc(default_cpumap, default_cpumap_maxsize
				     * sizeof(*default_cpumap));
	    if (!default_cpumap) free(oldMap);
	}
	if (!default_cpumap) {
	    parser_comment(-1, "%s: No memory for default_cpumap\n", __func__);
	    return -1;
	}
	default_cpumap[default_cpumap_size] = val;
	default_cpumap_size++;
    } else {
	if (node_cpumap_size == node_cpumap_maxsize) {
	    node_cpumap_maxsize *= 2;
	    short *oldMap = node_cpumap;
	    node_cpumap = realloc(node_cpumap, node_cpumap_maxsize
				  * sizeof(*node_cpumap));
	    if (!node_cpumap) free(oldMap);
	}
	if (!node_cpumap) {
	    parser_comment(-1, "%s: No memory for node_cpumap\n", __func__);
	    return -1;
	}
	node_cpumap[node_cpumap_size] = val;
	node_cpumap_size++;
    }
    parser_comment(PARSER_LOG_NODE, " %ld", val);
    return 0;
}

static int endCPUmapEnv(char *token)
{
    return ENV_END;
}

static keylist_t cpumapenv_list[] = {
    {"}", endCPUmapEnv, NULL},
    {NULL, getCPUmapEnt, NULL}
};

static parser_t cpumapenv_parser = {" \t\n", cpumapenv_list};

static int getCPUmapEnv(char *token)
{
    return parser_parseOn(parser_getString(), &cpumapenv_parser);
}

static keylist_t cpumap_list[] = {
    {"{", getCPUmapEnv, NULL},
    {NULL, getCPUmapEnt, NULL}
};

static parser_t cpumap_parser = {" \t\n", cpumap_list};

static int getCPUmap(char *token)
{
    int ret;

    if (currentID == DEFAULT_ID) {
	default_cpumap_size = 0;
	parser_comment(PARSER_LOG_NODE, "default CPUmap {");
    } else {
	node_cpumap_size = 0;
	parser_comment(PARSER_LOG_NODE, " CPUMap {");
    }

    ret = parser_parseToken(parser_getString(), &cpumap_parser);
    if (ret == ENV_END) ret = 0;

    if (!ret) {
	parser_comment(PARSER_LOG_NODE, " }");
	if (currentID == DEFAULT_ID) parser_comment(PARSER_LOG_NODE, "\n");
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

/** List type to store environment entries */
typedef struct {
    struct list_head next;
    char *name;
    char *value;
} EnvEnt_t;

/** List to collect environments that might be used locally */
static LIST_HEAD(envList);

static int envActive = 0;

static int getEnvLine(char *token)
{
    char *value;
    EnvEnt_t *envent;

    if (!token) {
	parser_comment(-1, "syntax error\n");
	return -1;
    }

    value = parser_getQuotedString();
    if (!value) {
	parser_comment(-1, "no value for %s\n", token);
	return -1;
    }

    /* store environment */
    envent = malloc(sizeof(*envent));
    if (!envent) {
	parser_comment(-1, "%s: No memory\n", __func__);
	return -1;
    }

    envent->name = strdup(token);
    envent->value = strdup(value);

    list_add_tail(&envent->next, &envList);

    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_RES, "got environment: %s='%s'\n",
		       token, value);
    } else {
	parser_comment(PARSER_LOG_NODE, " env %s='%s'", token, value);
    }

    return envActive ? 0 : ENV_END;
}

static int endEnvEnv(char *token)
{
    envActive = 0;
    return ENV_END;
}

static keylist_t envenv_list[] = {
    {"}", endEnvEnv, NULL},
    {NULL, getEnvLine, NULL}
};

static parser_t envenv_parser = {" \t\n", envenv_list};

static int getEnvEnv(char *token)
{
    envActive = 1;
    return parser_parseOn(parser_getString(), &envenv_parser);
}

static keylist_t env_list[] = {
    {"{", getEnvEnv, NULL},
    {NULL, getEnvLine, NULL}
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

/**
 * @brief Store environment
 *
 * Store the current list of environments collected in @ref envList to
 * the actual environment. At the same time, @ref envList is cleared.
 *
 * @return No return value.
 */
static void pushAndClearEnv(void)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &envList) {
	EnvEnt_t *env = list_entry(pos, EnvEnt_t, next);
	list_del(pos);
	if (env->name && env->value) {
	    parser_comment(PARSER_LOG_NODE, "set environment %s to '%s'\n",
			   env->name, env->value);
	    setenv(env->name, env->value, 1);
	}
	free(env->name);
	free(env->value);
	free(env);
    }
}

/**
 * @brief Clear environment
 *
 * Clear the current list of environments collected in @ref envList.
 *
 * @return No return value.
 */
static void clearEnv(void)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &envList) {
	EnvEnt_t *env = list_entry(pos, EnvEnt_t, next);
	list_del(pos);
	free(env->name);
	free(env->value);
	free(env);
    }
}

/*----------------------------------------------------------------------*/
/** @brief Setup node settings
 *
 * Create a snapshot for node-settings from the various defaults
 * defined before. The node settings might be modified by directives
 * from the configuration-file before they are actually used to
 * register the node via @ref newHost().
 *
 * @return On success, 0 is returned. Or -1, if any error occurred.
 */
static int setupNodeFromDefault(void)
{
    int ret;

    node_hwtype = default_hwtype;
    node_canstart = default_canstart;
    node_runjobs = default_runjobs;
    node_procs = default_procs;
    node_overbook = default_overbook;
    node_exclusive = default_exclusive;
    node_pinProcs = default_pinProcs;
    node_bindMem = default_bindMem;
    node_bindGPUs = default_bindGPUs;
    node_allowUserMap = default_allowUserMap;
    node_supplGrps = default_supplGrps;
    node_maxStatTry = default_maxStatTry;

    if (default_cpumap_size) {
	if (default_cpumap_size > node_cpumap_maxsize) {
	    node_cpumap_maxsize = default_cpumap_size;

	    short *oldMap = node_cpumap;
	    node_cpumap = realloc(node_cpumap, node_cpumap_maxsize
				  * sizeof(*node_cpumap));
	    if (!node_cpumap) free(oldMap);
	}
	if (!node_cpumap) {
	    parser_comment(-1, "%s: No memory\n", __func__);
	    return -1;
	}
	for (size_t i = 0; i < default_cpumap_size; i++) {
	    node_cpumap[i] = default_cpumap[i];
	}
	node_cpumap_size = default_cpumap_size;
    }

    ret = copy_GUIDlist(&defaultUID, &nodeUID);
    if (ret) return ret;
    ret = copy_GUIDlist(&defaultGID, &nodeGID);
    if (ret) return ret;
    ret = copy_GUIDlist(&defaultAdmUID, &nodeAdmUID);
    if (ret) return ret;
    return copy_GUIDlist(&defaultAdmGID, &nodeAdmGID);
}

/**
 * @brief Insert a node.
 *
 * Helper function to make a node known to the ParaStation daemon. All
 * other information besides the node's ParaStation ID @a id and its
 * IP address @a addr are passed implicitely via the node_* variables.
 *
 * The node_* variables are pre-set from the current default settings
 * by @ref setupNodeFromDefault() and might be modified by directives
 * from the configuration file.
 *
 * @param id ParaStation ID for this node.
 *
 * @param addr IP address of the node to register.
 *
 * @return Return -1 if an error occurred or 0 if the node was
 * inserted successfully.
 */
static int newHost(int id, in_addr_t addr)
{
    if (id < 0) { /* id out of Range */
	parser_comment(-1, "node ID <%d> out of range\n", id);
	return -1;
    }

    if ((ntohl(addr) >> 24) == IN_LOOPBACKNET) {
	parser_comment(-1, "node ID <%d> resolves to address <%s> within"
		       " loopback range\n",
		       id, inet_ntoa(* (struct in_addr *) &addr));
	return -1;
    }

    if (PSIDnodes_lookupHost(addr)!=-1) { /* duplicated host */
	parser_comment(-1, "duplicated host <%s>\n",
		       inet_ntoa(* (struct in_addr *) &addr));
	return -1;
    }

    if (PSIDnodes_getAddr(id) != INADDR_ANY) { /* duplicated PSI-ID */
	in_addr_t other = PSIDnodes_getAddr(id);
	parser_comment(-1, "duplicated ID <%d> for hosts <%s>",
		       id, inet_ntoa(* (struct in_addr *) &addr));
	parser_comment(-1, " and <%s>\n",
		       inet_ntoa(* (struct in_addr *) &other));
	return -1;
    }

    /* install hostname */
    if (!PSIDnodes_register(id, addr)) {
	parser_comment(-1, "PSIDnodes_register(%d, <%s>) failed\n",
		       id, inet_ntoa(*(struct in_addr *)&addr));
	return -1;
    }

    /* setup further settings */
    if (PSIDnodes_setHWType(id, node_hwtype)) {
	parser_comment(-1, "PSIDnodes_setHWType(%d, %d) failed\n",
		       id, node_hwtype);
	return -1;
    }

    if (PSIDnodes_setIsStarter(id, node_canstart)) {
	parser_comment(-1, "PSIDnodes_setIsStarter(%d, %d) failed\n",
		       id, node_canstart);
	return -1;
    }

    if (PSIDnodes_setRunJobs(id, node_runjobs)) {
	parser_comment(-1, "PSIDnodes_setRunJobs(%d, %d) failed\n",
		       id, node_runjobs);
	return -1;
    }

    if (PSIDnodes_setProcs(id, node_procs)) {
	parser_comment(-1, "PSIDnodes_setProcs(%d, %ld) failed\n",
		       id, node_procs);
	return -1;
    }

    if (PSIDnodes_setOverbook(id, node_overbook)) {
	parser_comment(-1, "PSIDnodes_setOverbook(%d, %d) failed\n",
		       id, node_overbook);
	return -1;
    }

    if (PSIDnodes_setExclusive(id, node_exclusive)) {
	parser_comment(-1, "PSIDnodes_setExclusive(%d, %d) failed\n",
		       id, node_exclusive);
	return -1;
    }

    if (PSIDnodes_setPinProcs(id, node_pinProcs)) {
	parser_comment(-1, "PSIDnodes_setPinProcs(%d, %d) failed\n",
		       id, node_pinProcs);
	return -1;
    }

    if (PSIDnodes_setBindMem(id, node_bindMem)) {
	parser_comment(-1, "PSIDnodes_setBindMem(%d, %d) failed\n",
		       id, node_bindMem);
	return -1;
    }

    if (PSIDnodes_setBindGPUs(id, node_bindGPUs)) {
	parser_comment(-1, "PSIDnodes_setBindGPUs(%d, %d) failed\n",
		       id, node_bindGPUs);
	return -1;
    }

    if (PSIDnodes_setAllowUserMap(id, node_allowUserMap)) {
	parser_comment(-1, "PSIDnodes_setAllowUserMap(%d, %d) failed\n",
		       id, node_allowUserMap);
	return -1;
    }

    if (PSIDnodes_setSupplGrps(id, node_supplGrps)) {
	parser_comment(-1, "PSIDnodes_setSupplGrps(%d, %d) failed\n",
		       id, node_supplGrps);
	return -1;
    }

    if (PSIDnodes_setMaxStatTry(id, node_maxStatTry)) {
	parser_comment(-1, "PSIDnodes_setMaxStatTry(%d, %d) failed\n",
		       id, node_maxStatTry);
	return -1;
    }

    if (node_cpumap_size) {
	size_t i;
	for (i=0; i<node_cpumap_size; i++) {
	    if (PSIDnodes_appendCPUMap(id, node_cpumap[i])) {
		parser_comment(-1, "PSIDnodes_appendCPUMap(%d, %d) failed\n",
			       id, node_cpumap[i]);
		return -1;
	    }
	}
    }

    if (pushGUID(id, PSIDNODES_USER, &nodeUID)) {
	parser_comment(-1, "pushGUID(%d, PSIDNODES_USER, %p) failed\n",
		       id, &nodeUID);
	return -1;
    }

    if (pushGUID(id, PSIDNODES_GROUP, &nodeGID)) {
	parser_comment(-1, "pushGUID(%d, PSIDNODES_GROUP, %p) failed\n",
		       id, &nodeGID);
	return -1;
    }

    if (pushGUID(id, PSIDNODES_ADMUSER, &nodeAdmUID)) {
	parser_comment(-1, "pushGUID(%d, PSIDNODES_ADMUSER, %p) failed\n",
		       id, &nodeAdmUID);
	return -1;
    }

    if (pushGUID(id, PSIDNODES_ADMGROUP, &nodeAdmGID)) {
	parser_comment(-1, "pushGUID(%d, PSIDNODES_ADMGROUP, %p) failed\n",
		       id, &nodeAdmGID);
	return -1;
    }

    nodesfound++;

    if (nodesfound > PSIDnodes_getNum()) { /* more hosts than nodes ??? */
	parser_comment(-1, "NrOfNodes = %d does not match number of hosts in"
		       " list (%d)\n", PSIDnodes_getNum(), nodesfound);
	return -1;
    }

    parser_comment(PARSER_LOG_VERB,
		   "%s: host <%s> inserted in hostlist with id=%d.\n",
		   __func__, inet_ntoa(* (struct in_addr *) &addr), id);

    return 0;

}
/* ---------------------------------------------------------------------- */

static keylist_t nodeline_list[] = {
    {"hwtype", getHW, NULL},
    {"runjobs", getRJ, NULL},
    {"starter", getCS, NULL},
    {"user", getUser, NULL},
    {"group", getGroup, NULL},
    {"adminuser", getAdminUser, NULL},
    {"admingroup", getAdminGroup, NULL},
    {"processes", getProcs, NULL},
    {"overbook", getOB, NULL},
    {"exclusive", getExcl, NULL},
    {"pinprocs", getPinProcs, NULL},
    {"bindmem", getBindMem, NULL},
    {"bindGPUs", getBindGPUs, NULL},
    {"allowusermap", getAllowUserMap, NULL},
    {"supplGrps", getSupplGrps, NULL},
    {"maxStatTry", getMaxStatTry, NULL},
    {"cpumap", getCPUmap, NULL},
    {"environment", getEnv, NULL},
    {NULL, parser_error, NULL}
};

static parser_t nodeline_parser = {" \t\n", nodeline_list};

static int getNodeLine(char *token)
{
    in_addr_t ipaddr = parser_getHostname(token);
    if (!ipaddr) return -1;

    char *hostname = strdup(token);

    int nodenum;
    int ret = parser_getNumValue(parser_getString(), &nodenum, "node number");
    if (ret) {
	free(hostname);
	return ret;
    }

    ret = setupNodeFromDefault();
    if (ret) {
	free(hostname);
	return ret;
    }

    parser_comment(PARSER_LOG_NODE, "Register '%s' as %d", hostname, nodenum);

    currentID = nodenum;

    ret = parser_parseString(parser_getString(), &nodeline_parser);
    parser_comment(PARSER_LOG_NODE, "\n");
    if (ret) {
	free(hostname);
	return ret;
    }

    currentID = DEFAULT_ID;

    ret = newHost(nodenum, ipaddr);
    if (ret) {
	free(hostname);
	return ret;
    }

    parser_updateHash(&config.nodeListHash, hostname);
    free(hostname);

    if (PSC_isLocalIP(ipaddr)) {
	pushAndClearEnv();
	PSC_setMyID(nodenum);
    } else {
	clearEnv();
    }

    return ret;
}

/**
 * @brief Analyze range string
 *
 * Analyze the range string @a range and extract the @a first and @a
 * last element. If an optional @a step is also given, this is
 * extracted, too. Otherwise @a step will be set to 1.
 *
 * The range has to be of the form first-last[/step]. If the string @a
 * range does not conform to this syntax, a error is detected and -1
 * is returned.
 *
 * @param range String to analyze
 *
 * @param first First element detected
 *
 * @param last Last element detected
 *
 * @param step Step-size detected in @a range. If no range is given,
 * will be set to 1.
 *
 * @return If @a range conforms to the syntax described above, 0 is
 * returned. Otherwise -1 is given back.
 */
static int analyzeRange(char *range, long *first, long *last, long *step)
{
    char *minus, *slash, *rangeStr = strdup(range);
    int ret = -1;

    if (!rangeStr) {
	parser_comment(-1, "%s: Out of mem\n", __func__);
	return -1;
    }

    minus = strchr(rangeStr, '-');
    slash = strchr(rangeStr, '/');

    if (!minus) goto end;
    *minus = '\0';
    if (! *rangeStr) goto end;

    ret = parser_getNumber(rangeStr, first);
    if (ret) goto end;

    minus++;
    if (slash) *slash = '\0';
    if (! *minus) {
	ret = -1;
	goto end;
    }

    ret = parser_getNumber(minus, last);
    if (ret) goto end;

    if (slash) {
	slash++;
	ret = parser_getNumber(slash, step);
	if (ret) goto end;
    } else {
	*step = 1;
    }

    if (*last < *first) {
	ret = -1;
	goto end;
    }

end:
    free(rangeStr);
    if (ret) parser_comment(-1, "%s: broken range '%s'\n", __func__, range);
    return ret;
}

static int handleGenStr(long val, char *in, char **out, size_t *size)
{
    char *inStr, *dollar = strchr(in, '$'), *rec;
    size_t len;
    int ret = -1;

    if (!dollar) {
	/* Nothing to replace */
	len = strlen(in)+1;

	if (len > *size) {
	    *size = len;
	    *out = realloc(*out, *size);
	}
	if (!out) {
	    parser_comment(-1, "%s: Out of mem\n", __func__);
	    return -1;
	}
	sprintf(*out, "%s", in);

	return 0;
    } else {
	char *start, *end, base = 'd', fmt[32];
	long off = 0, width = 1;

	inStr = strdup(in);
	if (!inStr) {
	    parser_comment(-1, "%s: Out of mem\n", __func__);
	    return -1;
	}

	/* Now handle inStr */
	dollar = strchr(inStr, '$');
	*dollar = '\0';
	start = dollar+1;
	if (*start != '{') {
	    end = start;
	} else {
	    rec = start+1;
	    end = strchr(rec, '}');
	    if (!end) goto end;

	    *end = '\0';
	    off = strtol(rec, &end, 0);
	    if (end == rec) goto end;

	    if (*end == ',') {
		start = end+1;
		width = strtol(start, &end, 0);
		if (end == start) goto end;
		if (*end == ',') {
		    start = end+1;
		    if (! *start || (*start!='d' && *start!='o' &&
				     *start!='x' && *start!='X')) goto end;
		    base = *start;
		    end = start+1;
		}
	    }
	    if (*end) goto end;
	    end++;
	}

	snprintf(fmt, sizeof(fmt), "%%s%%.%ld%c%%s", width, base);

	len = snprintf(*out, *size, fmt, inStr, val+off, end);
	if (len >= *size) {
	    *size = len+80; /* some extra space */
	    *out = realloc(*out, *size);
	    if (!*out) {
		parser_comment(-1, "%s: Out of mem\n", __func__);
		free(inStr);
		return -1;
	    }
	    sprintf(*out, fmt, inStr, val+off, end);
	}
    }

    ret = 0;
end:
    free(inStr);
    if (ret) parser_comment(-1, "%s: broken record ${%s}\n", __func__, rec);
    return ret;
}

static int getMultiNodes(char *token)
{
    int ret;
    long n, first, last, step;
    char *rangeStr, *hostStr, *idStr, *realHost = NULL, *realID = NULL;
    size_t realHostSize = 0, realIDSize = 0;

    rangeStr = parser_getString();
    if (!rangeStr) {
	parser_comment(-1, "%s: Out of mem\n", __func__);
	return -1;
    }

    ret = analyzeRange(rangeStr, &first, &last, &step);
    if (ret) return ret;

    hostStr = parser_getString();
    if (!hostStr) return -1;
    idStr = parser_getString();
    if (!idStr) return -1;

    ret = setupNodeFromDefault();
    if (ret) return ret;

    parser_comment(PARSER_LOG_NODE, "Register '%s' as %s [%s]",
		   hostStr, idStr, rangeStr);

    currentID = GENERATE_ID;

    ret = parser_parseString(parser_getString(), &nodeline_parser);
    parser_comment(PARSER_LOG_NODE, "\n");
    if (ret) return ret;

    currentID = DEFAULT_ID;

    for (n=first; n<=last; n+=step) {
	in_addr_t ipaddr;
	int nodenum;

	ret = handleGenStr(n, hostStr, &realHost, &realHostSize);
	if (ret) return ret;
	ipaddr = parser_getHostname(realHost);
	if (!ipaddr) return -1;

	handleGenStr(n, idStr, &realID, &realIDSize);
	ret = parser_getNumValue(realID, &nodenum, "node number");
	if (ret) return ret;

	ret = newHost(nodenum, ipaddr);
	if (ret) return ret;

	if (PSC_isLocalIP(ipaddr)) {
	    pushAndClearEnv();
	    PSC_setMyID(nodenum);
	}
    }

    free(realHost);
    free(realID);

    clearEnv();

    return 0;
}

static int endNodeEnv(char *token)
{
    return ENV_END;
}

static keylist_t nodeenv_list[] = {
    {"$generate", getMultiNodes, NULL},
    {"}", endNodeEnv, NULL},
    {NULL, getNodeLine, NULL}
};

static parser_t nodeenv_parser = {" \t\n", nodeenv_list};

static int getNodeEnv(char *token)
{
    return parser_parseOn(parser_getString(), &nodeenv_parser);
}


static keylist_t node_list[] = {
    {"{", getNodeEnv, NULL},
    {NULL, getNodeLine, NULL}
};

static parser_t node_parser = {" \t\n", node_list};


static int getNodes(char *token)
{
    int ret;

    pushAndClearEnv();

    ret = parser_parseString(parser_getString(), &node_parser);

    if (ret == ENV_END) {
	return 0;
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static int actHW = -1;

static int getHardwareScript(char *token)
{
    char *name, *value;

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
	parser_comment(-1, "unknown script type '%s'\n", token);
	return -1;
    }

    value = parser_getQuotedString();
    if (!value) {
	parser_comment(-1, "no value for %s\n", name);
	return -1;
    }

    /* store environment */
    if (HW_getScript(actHW, name)) {
	parser_comment(-1, "redefineing hardware script: %s\n", name);
    }
    HW_setScript(actHW, name, value);

    parser_comment(PARSER_LOG_RES, "got hardware script: %s='%s'\n",
		   name, value);

    return 0;
}

static int getHardwareEnvLine(char *token)
{
    char *value;

    if (!token) {
	parser_comment(-1, "syntax error\n");
	return -1;
    }

    value = parser_getQuotedString();
    if (!value) {
	parser_comment(-1, "no value for %s\n", token);
	return -1;
    }

    /* store environment */
    if (HW_getEnv(actHW, token)) {
	parser_comment(-1, "redefineing hardware environment: %s\n", token);
    }
    HW_setEnv(actHW, token, value);

    parser_comment(PARSER_LOG_RES, "got hardware environment: %s='%s'\n",
		   token, value);

    return 0;
}

static int endHardwareEnv(char *token)
{
    actHW = -1;
    return ENV_END;
}

static keylist_t hardwareenv_list[] = {
    {"}", endHardwareEnv, NULL},
    {"startscript", getHardwareScript, NULL},
    {"stopscript", getHardwareScript, NULL},
    {"setupscript", getHardwareScript, NULL},
    {"headerscript", getHardwareScript, NULL},
    {"statusscript", getHardwareScript, NULL},
    {NULL, getHardwareEnvLine, NULL}
};

static parser_t hardwareenv_parser = {" \t\n", hardwareenv_list};

static int getHardware(char *token)
{
    char *name, *brace;
    int ret;

    name = parser_getString();
    if (!name) {
	parser_comment(-1, "no hardware name\n");
	return -1;
    }

    actHW = HW_index(name);

    if (actHW == -1) {
	actHW = HW_add(name);

	parser_comment(PARSER_LOG_RES, "new hardware '%s' registered as %d\n",
		       name, actHW);
    }

    brace = parser_getString();
    if (!brace || strcmp(brace, "{")) return -1;

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
    parser_comment(-1, "suspended jobs will free their resources\n");
    return 0;
}

static int sortLoad1(char *token)
{
    config.nodesSort = PART_SORT_LOAD_1;
    return 0;
}

static int sortLoad5(char *token)
{
    config.nodesSort = PART_SORT_LOAD_5;
    return 0;
}

static int sortLoad15(char *token)
{
    config.nodesSort = PART_SORT_LOAD_15;
    return 0;
}

static int sortProc(char *token)
{
    config.nodesSort = PART_SORT_PROC;
    return 0;
}

static int sortProcLoad(char *token)
{
    config.nodesSort = PART_SORT_PROCLOAD;
    return 0;
}

static int sortNone(char *token)
{
    config.nodesSort = PART_SORT_NONE;
    return 0;
}

static keylist_t sort_list[] = {
    {"load1", sortLoad1, NULL},
    {"load_1", sortLoad1, NULL},
    {"load5", sortLoad5, NULL},
    {"load_5", sortLoad5, NULL},
    {"load15", sortLoad15, NULL},
    {"load_15", sortLoad15, NULL},
    {"proc", sortProc, NULL},
    {"proc+load", sortProcLoad, NULL},
    {"none", sortNone, NULL},
    {NULL, parser_error, NULL}
};

static parser_t sort_parser = {" \t\n", sort_list};

static int getPSINodesSort(char *token)
{
    int ret;

    ret = parser_parseString(parser_getString(), &sort_parser);
    if (!ret) {
	parser_comment(-1, "default sorting strategy for nodes is '%s'\n",
		       (config.nodesSort == PART_SORT_PROC) ? "PROC" :
		       (config.nodesSort == PART_SORT_LOAD_1) ? "LOAD_1" :
		       (config.nodesSort == PART_SORT_LOAD_5) ? "LOAD_5" :
		       (config.nodesSort == PART_SORT_LOAD_15) ? "LOAD_15" :
		       (config.nodesSort == PART_SORT_PROCLOAD) ? "PROC+LOAD" :
		       (config.nodesSort == PART_SORT_NONE) ? "NONE" :
		       "UNKNOWN");
    }
    return ret;
}

/* ---------------------------------------------------------------------- */

static int getPluginEnt(char *token)
{
    nameList_t *new;

    parser_comment(PARSER_LOG_RES, "Scheduled plugin for loading: '%s'\n",
		   token);

    new = malloc(sizeof(*new));
    if (!new) parser_exit(errno, "%s", __func__);

    new->name = strdup(token);
    list_add_tail(&new->next, &config.plugins);

    return 0;
}

static int getPluginSingle(char *token)
{
    if (parser_getString()) return -1;
    return getPluginEnt(token);
}

static int endPluginEnv(char *token)
{
    return ENV_END;
}

static keylist_t pluginenv_list[] = {
    {"}", endPluginEnv, NULL},
    {NULL, getPluginEnt, NULL}
};

static parser_t pluginenv_parser = {" \t\n", pluginenv_list};

static int getPluginEnv(char *token)
{
    return parser_parseOn(parser_getString(), &pluginenv_parser);
}

static keylist_t plugin_list[] = {
    {"{", getPluginEnv, NULL},
    {NULL, getPluginSingle, NULL}
};

static parser_t plugin_parser = {" \t\n", plugin_list};

static int getPlugins(char *token)
{
    int ret = parser_parseToken(parser_getString(), &plugin_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

/* ---------------------------------------------------------------------- */

static int getDaemonScript(char *token)
{
    char *value;

    value = parser_getQuotedString();
    if (!value) {
	parser_comment(-1, "no value for %s\n", token);
	return -1;
    }

    if (PSID_registerScript(&config, token, value)) {
	parser_comment(-1, "failed to register script '%s' to type '%s'\n",
		       value, token);
	return -1;
    }

    return 0;
}

/* ---------------------------------------------------------------------- */

static keylist_t config_list[] = {
    {"installationdirectory", getInstDir, NULL},
    {"installdirectory", getInstDir, NULL},
    {"coredirectory", getCoreDir, NULL},
    {"hardware", getHardware, NULL},
    {"nrofnodes", getNumNodes, NULL},
    {"hwtype", getHW, NULL},
    {"runjobs", getRJ, NULL},
    {"starter", getCS, NULL},
    {"user", getUser, NULL},
    {"group", getGroup, NULL},
    {"adminuser", getAdminUser, NULL},
    {"admingroup", getAdminGroup, NULL},
    {"processes", getProcs, NULL},
    {"overbook", getOB, NULL},
    {"exclusive", getExcl, NULL},
    {"pinprocs", getPinProcs, NULL},
    {"bindmem", getBindMem, NULL},
    {"bindGPUs", getBindGPUs, NULL},
    {"allowusermap", getAllowUserMap, NULL},
    {"supplGrps", getSupplGrps, NULL},
    {"maxStatTry", getMaxStatTry, NULL},
    {"cpumap", getCPUmap, NULL},
    {"nodes", getNodes, NULL},
    {"licenseserver", getLicServer, NULL},
    {"licserver", getLicServer, NULL},
    {"licensefile", getLicFile, NULL},
    {"licfile", getLicFile, NULL},
    {"usemcast", getMCastUse, NULL},
    {"mcastgroup", getMCastGroup, NULL},
    {"mcastport", getMCastPort, NULL},
    {"rdpport", getRDPPort, NULL},
    {"rdptimeout", getRDPTimeout, NULL},
    {"rdpmaxretrans", getRDPMaxRetrans, NULL},
    {"rdpresendtimeout", getRDPResendTimeout, NULL},
    {"rdpclosedtimeout", getRDPClosedTimeout, NULL},
    {"rdpmaxackpending", getRDPMaxACKPend, NULL},
    {"rdpstatistics", getRDPStatistics, NULL},
    {"selecttime", getSelectTime, NULL},
    {"deadinterval", getDeadInterval, NULL},
    {"statustimeout", getStatTmout, NULL},
    {"statusbroadcasts", getStatBcast, NULL},
    {"deadlimit", getDeadLmt, NULL},
    {"killdelay", getKillDelay, NULL},
    {"rlimit", getRLimit, NULL},
    {"loglevel", getLogMask, NULL},
    {"logmask", getLogMask, NULL},
    {"logdestination", getLogDest, NULL},
    {"environment", getEnv, NULL},
    {"freeOnSuspend", getFreeOnSusp, NULL},
    {"psiNodesSort", getPSINodesSort, NULL},
    {"plugins", getPlugins, NULL},
    {"startupScript", getDaemonScript, NULL},
    {"nodeUpScript", getDaemonScript, NULL},
    {"nodeDownScript", getDaemonScript, NULL},
    {NULL, parser_error, NULL}
};

static parser_t config_parser = {" \t\n", config_list};

config_t *parseOldConfig(FILE* logfile, int logmask, char *configfile)
{
    FILE *cfd;
    int ret;

    INIT_LIST_HEAD(&config.plugins);

    parser_init(logfile, NULL);

    if (!configfile) {
	parser_comment(-1, "no configuration file defined\n");
	return NULL;
    }

    if (!(cfd = fopen(configfile,"r"))) {
	parser_comment(-1, "unable to locate file <%s>\n", configfile);
	return NULL;
    }

    parser_setFile(cfd);
    parser_setDebugMask(logmask);
    parser_comment(PARSER_LOG_FILE, "using file <%s>\n", configfile);

    setID(&defaultUID, PSNODES_ANYUSER);
    setID(&defaultGID, PSNODES_ANYGROUP);
    setID(&defaultAdmUID, 0);
    setID(&defaultAdmGID, 0);

    ret = parser_parseFile(&config_parser);

    if (ret) {
	parser_comment(-1, "ERROR: Parsing of <%s> failed.\n", configfile);
	return NULL;
    }

    fclose(cfd);

    pushAndClearEnv();

    /*
     * Sanity Checks
     */
    if (PSIDnodes_getNum()==-1) {
	parser_comment(-1, "ERROR: NrOfNodes not defined\n");
	return NULL;
    }

    /*
     * Sanity Checks
     */
    if (PSIDnodes_getNum() > nodesfound && nrOfNodesGiven) {
	/* hosts missing in hostlist */
	parser_comment(-1, "WARNING: # to few hosts in hostlist\n");
    }

    if (default_cpumap != std_cpumap) {
	free(default_cpumap);
	default_cpumap = std_cpumap;
    }
    if (node_cpumap) {
	node_cpumap = NULL;
	node_cpumap_maxsize = 0;
    }

    parser_finalize();

    return &config;
}
