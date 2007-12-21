/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
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
 
#include "list.h"

#include "parser.h"
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"
#include "pspartition.h"

#include "psidnodes.h"

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
    .acctPollInterval = 0,
};

#define ENV_END 17 /* Some magic value */

#define DEFAULT_ID -1

static int currentID = DEFAULT_ID;

static int nodesfound = 0;

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

    PSC_setInstalldir(dname);
    if (strcmp(dname, PSC_lookupInstalldir())) {
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
    if (ret) {
	parser_comment(-1, "PSIDnodes_init(%d) failed\n", num);
    }

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

static int getAcctPollInterval(char *token)
{
    int temp, ret;

    ret = parser_getNumValue(parser_getString(), &temp, "dead interval");
    if (ret) return ret;

    config.acctPollInterval = temp;

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

static int getRLimitCore(char *token)
{
    rlim_t value;
    int ret;

    ret = getRLimitVal(parser_getString(), &value, "RLimit Core");
    if (ret) return ret;

    setLimit(RLIMIT_CORE, (value == RLIM_INFINITY) ? value : value*1024);

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

static int default_hwtype = 0, hwtype;

static int setHWType(int hw)
{
    if (currentID == DEFAULT_ID) {
	default_hwtype = hw;
	parser_comment(PARSER_LOG_NODE, "setting default HWType to '%s'\n",
		       HW_printType(hw));
    } else {
	if (PSIDnodes_setHWType(currentID, hw)) {
	    parser_comment(-1, "PSIDnodes_setHWType(%d, %d) failed\n",
			   currentID, hw);
	    return -1;
	}

	parser_commentCont(PARSER_LOG_NODE, " HW '%s'", HW_printType(hw));
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
    {"none", getHWnone},
    {"}", endHWEnv},
    {NULL, getHWent}
};

static parser_t hwenv_parser = {" \t\n", hwenv_list};

static int getHWEnv(char *token)
{
    hwtype = 0;
    return parser_parseOn(parser_getString(), &hwenv_parser);
}

static keylist_t hw_list[] = {
    {"{", getHWEnv},
    {"none", getHWnone},
    {NULL, getHWsingle}
};

static parser_t hw_parser = {" \t\n", hw_list};

static int getHW(char *token)
{
    int ret = parser_parseToken(parser_getString(), &hw_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

static int default_canstart = 1;

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
	if (PSIDnodes_setIsStarter(currentID, cs)) {
	    parser_comment(-1, "PSIDnodes_setIsStarter(%d, %d) failed\n",
			   currentID, cs);
	    return -1;
	}
	parser_commentCont(PARSER_LOG_NODE, " starting%s allowed",
			   cs ? "":" not");
    }
    return 0;
}

static int default_runjobs = 1;

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
	if (PSIDnodes_setRunJobs(currentID, rj)) {
	    parser_comment(-1, "PSIDnodes_setRunJobs(%d, %d) failed\n",
			   currentID, rj);
	    return -1;
	}
	parser_commentCont(PARSER_LOG_NODE, " jobs%s allowed", rj ? "":" not");
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

/* from bin/admin/adminparser.c */
static uid_t uidFromString(char *user)
{
    long uid;
    struct passwd *passwd = getpwnam(user);

    if (strcasecmp(user, "any") == 0) return -1;
    if (!parser_getNumber(user, &uid) && uid > -1) return uid;
    if (passwd) return passwd->pw_uid;

    return -2;
}

/* from bin/admin/adminparser.c */
static gid_t gidFromString(char *group)
{
    long gid;
    struct group *grp = getgrnam(group);

    if (strcasecmp(group, "any") == 0) return -1;
    if (!parser_getNumber(group, &gid) && gid > -1) return gid;
    if (grp) return grp->gr_gid;

    return -2;
}

/**
 * @brief Create user-string.
 *
 * Create a string describing the user identified by the user ID @a uid.
 *
 * @param uid User ID of the user to describe.
 *
 * @return Pointer to a new string describing the user. Memory for the
 * new string is obtained with malloc(3), and can be freed with
 * free(3).
 */
static char* userFromUID(uid_t uid)
{
    struct passwd *pwd;

    if ((int)uid >= 0) {
	pwd = getpwuid(uid);
	if (pwd) {
	    return strdup(pwd->pw_name);
	} else {
	    return strdup("unknown");
	}
    } else {
	return strdup("ANY");
    }

}

/**
 * @brief Create group-string.
 *
 * Create a string describing the group identified by the group ID @a gid.
 *
 * @param gid Group ID of the group to describe.
 *
 * @return Pointer to a new string describing the group. Memory for the
 * new string is obtained with malloc(3), and can be freed with
 * free(3).
 */
static char* groupFromGID(gid_t gid)
{
    struct group *grp;

    if ((int)gid >= 0) {
	grp = getgrgid(gid);
	if (grp) {
	    return strdup(grp->gr_name);
	} else {
	    return strdup("unknown");
	}
    } else {
	return strdup("ANY");
    }
}

/* ---------------------------------------------------------------------- */

/** List type to store group/user entries */
typedef struct {
    struct list_head next;
    unsigned int id;
} GUent_t;

LIST_HEAD(defaultUID);
LIST_HEAD(defaultGID);
LIST_HEAD(defaultAdmUID);
LIST_HEAD(defaultAdmGID);

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
static void clear_list(list_t *list)
{
    list_t *pos, *tmp;

    list_for_each_safe(pos, tmp, list) {
	GUent_t *guent = list_entry(pos, GUent_t, next);
	list_del(pos);
	free(guent);
    }
}

static int addID(list_t *list, unsigned int id);

static int setID(list_t *list, unsigned int id)
{
    if (!list) return -1;

    clear_list(list);

    return addID(list, id);
}

static int addID(list_t *list, unsigned int id)
{
    GUent_t *guent;
    unsigned int any = PSNODES_ANYUSER;
    list_t *pos, *tmp;

    if (!list) return -1;

    if (list==&defaultGID || list==&defaultAdmGID) any = PSNODES_ANYGROUP;
    if (id == any) clear_list(list);

    list_for_each_safe(pos, tmp, list) {
        guent = list_entry(pos, GUent_t, next);
	if (guent->id == any) {
	    parser_comment(-1, "%s(%p, %d): ANY found\n", __func__, list, id);
	    return -1;
	}
	if (guent->id == id) {
	    parser_comment(-1, "%s(%p, %d): allready there\n",
			   __func__, list, id);
	    return -1;
	}
    }

    guent = malloc(sizeof(*guent));
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

static int pushDefaults(PSnodes_ID_t id, PSIDnodes_gu_t what, list_t *list)
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
	PSIDnodes_guid_t val;
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

static int (*defaultAction)(list_t *, unsigned int);
static int (*nodeAction)(PSnodes_ID_t, PSIDnodes_gu_t, PSIDnodes_guid_t);

static void setAction(char **token, char **action)
{
    switch (**token) {
    case '+':
	defaultAction = addID;
	nodeAction = PSIDnodes_addGUID;
	*action = "+";
	(*token)++;
	break;
    case '-':
	defaultAction = remID;
	nodeAction = PSIDnodes_remGUID;
	*action = "-";
	(*token)++;
	break;
    default:
	defaultAction = setID;
	nodeAction = PSIDnodes_setGUID;
	*action = "";
	break;
    }
}

/* ---------------------------------------------------------------------- */

static int getSingleUser(char *user)
{
    char *action, *userName;
    uid_t uid;

    if (!user) {
	parser_comment(-1, "Empty user\n");
	return -1;
    }

    setAction(&user, &action);

    uid = uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return -1;
    }

    userName = userFromUID(uid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE, "setting default 'User' to '%s%s'\n",
		       action, userName);
	defaultAction(&defaultUID, uid);
    } else {
	PSIDnodes_guid_t guid = { .u = uid };
	parser_commentCont(PARSER_LOG_NODE, " user '%s%s'", action, userName);
	nodeAction(currentID, PSIDNODES_USER, guid);
    }
    free(userName);
    return 0;
}

static int endUserEnv(char *token)
{
    return ENV_END;
}

static keylist_t userenv_list[] = {
    {"}", endUserEnv},
    {NULL, getSingleUser}
};

static parser_t userenv_parser = {" \t\n", userenv_list};

static int getUserEnv(char *token)
{
    return parser_parseOn(parser_getString(), &userenv_parser);
}

static keylist_t user_list[] = {
    {"{", getUserEnv},
    {NULL, getSingleUser}
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
    char *action, *groupName;
    gid_t gid;

    if (!group) {
	parser_comment(-1, "Empty group\n");
	return -1;
    }

    setAction(&group, &action);

    gid = gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return -1;
    }

    groupName = groupFromGID(gid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE, "setting default 'Group' to '%s%s'\n",
		       action, groupName);
	defaultAction(&defaultGID, gid);
    } else {
	PSIDnodes_guid_t guid = { .g = gid };
	parser_commentCont(PARSER_LOG_NODE,
			   " group '%s%s'", action, groupName);
	nodeAction(currentID, PSIDNODES_GROUP, guid);
    }
    free(groupName);
    return 0;
}

static int endGroupEnv(char *token)
{
    return ENV_END;
}

static keylist_t groupenv_list[] = {
    {"}", endGroupEnv},
    {NULL, getSingleGroup}
};

static parser_t groupenv_parser = {" \t\n", groupenv_list};

static int getGroupEnv(char *token)
{
    return parser_parseOn(parser_getString(), &groupenv_parser);
}

static keylist_t group_list[] = {
    {"{", getGroupEnv},
    {NULL, getSingleGroup}
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
    char *action, *userName;
    uid_t uid;

    if (!user) {
	parser_comment(-1, "Empty user\n");
	return -1;
    }

    setAction(&user, &action);

    uid = uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return -1;
    }

    userName = userFromUID(uid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE,
		       "setting default 'AdminUser' to '%s%s'\n",
		       action, userName);
	defaultAction(&defaultAdmUID, uid);
    } else {
	PSIDnodes_guid_t guid = { .u = uid };
	parser_commentCont(PARSER_LOG_NODE,
			   " adminuser '%s%s'", action, userName);
	nodeAction(currentID, PSIDNODES_ADMUSER, guid);
    }
    free(userName);
    return 0;
}

static int endAdminUserEnv(char *token)
{
    return ENV_END;
}

static keylist_t admuserenv_list[] = {
    {"}", endAdminUserEnv},
    {NULL, getSingleAdminUser}
};

static parser_t admuserenv_parser = {" \t\n", admuserenv_list};

static int getAdminUserEnv(char *token)
{
    return parser_parseOn(parser_getString(), &admuserenv_parser);
}

static keylist_t admuser_list[] = {
    {"{", getAdminUserEnv},
    {NULL, getSingleAdminUser}
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
    char *action, *groupName;
    gid_t gid;

    if (!group) {
	parser_comment(-1, "Empty group\n");
	return -1;
    }

    setAction(&group, &action);

    gid = gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return -1;
    }

    groupName = groupFromGID(gid);
    if (currentID == DEFAULT_ID) {
	parser_comment(PARSER_LOG_NODE,
		       "setting default 'AdminGroup' to '%s%s'\n",
		       action, groupName);
	defaultAction(&defaultAdmGID, gid);
    } else {
	PSIDnodes_guid_t guid = { .g = gid };
	parser_commentCont(PARSER_LOG_NODE,
			   " admingroup '%s%s'", action, groupName);
	nodeAction(currentID, PSIDNODES_ADMGROUP, guid);
    }
    free(groupName);
    return 0;
}

static int endAdminGroupEnv(char *token)
{
    return ENV_END;
}

static keylist_t admgroupenv_list[] = {
    {"}", endAdminGroupEnv},
    {NULL, getSingleAdminGroup}
};

static parser_t admgroupenv_parser = {" \t\n", admgroupenv_list};

static int getAdminGroupEnv(char *token)
{
    return parser_parseOn(parser_getString(), &admgroupenv_parser);
}

static keylist_t admgroup_list[] = {
    {"{", getAdminGroupEnv},
    {NULL, getSingleAdminGroup}
};

static parser_t admgroup_parser = {" \t\n", admgroup_list};

static int getAdminGroup(char *token)
{
    int ret = parser_parseToken(parser_getString(), &admgroup_parser);

    if (ret == ENV_END) ret = 0;

    return ret;
}

/* ---------------------------------------------------------------------- */

static int default_procs = -1;

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
	    parser_commentCont(PARSER_LOG_NODE, "ANY");
	} else {
	    parser_commentCont(PARSER_LOG_NODE, "%ld", procs);
	}
	parser_commentCont(PARSER_LOG_NODE, "'\n");
    } else {
	if (PSIDnodes_setProcs(currentID, procs)) {
	    parser_comment(-1, "PSIDnodes_setProcs(%d, %ld) failed\n",
			   currentID, procs);
	    return -1;
	}

	if (procs == -1) {
	    parser_commentCont(PARSER_LOG_NODE, " any");
	} else {
	    parser_commentCont(PARSER_LOG_NODE, " %ld", procs);
	}
	parser_commentCont(PARSER_LOG_NODE, " procs");
    }
    return 0;
}

static PSnodes_overbook_t default_overbook = OVERBOOK_FALSE;

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
	if (PSIDnodes_setOverbook(currentID, ob)) {
	    parser_comment(-1, "PSIDnodes_setOverbook(%d, %d) failed\n",
			   currentID, ob);
	    return -1;
	}
        parser_commentCont(PARSER_LOG_NODE, " overbooking is '%s'",
			   ob==OVERBOOK_AUTO ? "auto" : ob ? "TRUE" : "FALSE");
    }
    return 0;
}

static int default_exclusive = 1;

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
	if (PSIDnodes_setExclusive(currentID, excl)) {
	    parser_comment(-1, "PSIDnodes_setExclusive(%d, %d) failed\n",
			   currentID, excl);
	    return -1;
	}
        parser_commentCont(PARSER_LOG_NODE, " exclusive assign%s allowed",
			   excl ? "":" not");
    }
    return 0;
}

static int default_pinProcs = 1;

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
	if (PSIDnodes_setPinProcs(currentID, pinProcs)) {
	    parser_comment(-1, "PSIDnodes_setPinProcs(%d, %d) failed\n",
			   currentID, pinProcs);
	    return -1;
	}
        parser_commentCont(PARSER_LOG_NODE, " processes are%s pinned",
			   pinProcs ? "":" not");
    }
    return 0;
}

static int default_bindMem = 0;

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
	if (PSIDnodes_setBindMem(currentID, bindMem)) {
	    parser_comment(-1, "PSIDnodes_setBindMem(%d, %d) failed\n",
			   currentID, bindMem);
	    return -1;
	}
        parser_commentCont(PARSER_LOG_NODE, " memory is%s bound",
			   bindMem ? "":" not");
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

static short std_cpumap[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17};
static short *default_cpumap = std_cpumap;
static size_t default_cpumap_size = 8, default_cpumap_maxsize;

static int getCPUmapEnt(char *token)
{
    long val;
    int ret;

    ret = parser_getNumber(token, &val);
    if (ret) return ret;

    if (currentID == DEFAULT_ID) {
	if (default_cpumap == std_cpumap) {
	    default_cpumap_maxsize = 8;
	    default_cpumap = malloc(default_cpumap_maxsize
				    * sizeof(*default_cpumap));
	} else if (default_cpumap_size == default_cpumap_maxsize) {
	    default_cpumap_maxsize *= 2;
	    default_cpumap = realloc(default_cpumap, default_cpumap_maxsize
				     * sizeof(*default_cpumap));
	}
	default_cpumap[default_cpumap_size] = val;
	default_cpumap_size++;
    } else {
	if (PSIDnodes_appendCPUMap(currentID, val)) {
	    parser_comment(-1, "PSIDnodes_appendCPUMap(%d, %ld) failed\n",
			   currentID, val);
	    return -1;
	}
    }
    parser_commentCont(PARSER_LOG_NODE, " %ld", val);
    return 0;
}

static int endCPUmapEnv(char *token)
{
    return ENV_END;
}

static keylist_t cpumapenv_list[] = {
    {"}", endCPUmapEnv},
    {NULL, getCPUmapEnt}
};

static parser_t cpumapenv_parser = {" \t\n", cpumapenv_list};

static int getCPUmapEnv(char *token)
{
    return parser_parseOn(parser_getString(), &cpumapenv_parser);
}

static keylist_t cpumap_list[] = {
    {"{", getCPUmapEnv},
    {NULL, getCPUmapEnt}
};

static parser_t cpumap_parser = {" \t\n", cpumap_list};

static int getCPUmap(char *token)
{
    int ret;

    if (currentID == DEFAULT_ID) {
	default_cpumap_size = 0;
	parser_comment(PARSER_LOG_NODE, "default CPUmap {");
    } else {
	PSIDnodes_clearCPUMap(currentID);
	parser_commentCont(PARSER_LOG_NODE, " CPUMap {");
    }

    ret = parser_parseToken(parser_getString(), &cpumap_parser);
    if (ret == ENV_END) ret = 0;

    if (!ret) {
	parser_commentCont(PARSER_LOG_NODE, " }");
	if (currentID == DEFAULT_ID) parser_commentCont(PARSER_LOG_NODE, "\n");
    }

    return ret;
}

/*----------------------------------------------------------------------*/
/**
 * @brief Insert a node.
 *
 * Helper function to make a node known to the ParaStation
 * daemon. Various information concerning this nodes is stored at this
 * early stage. Some of these informations might be modified at later
 * stages.
 *
 * @param addr IP address of the node to register.
 *
 * @param id ParaStation ID for this node.
 *
 * @return Return -1 if an error occurred or 0 if the node was
 * inserted successfully.
 */
static int newHost(in_addr_t addr, int id)
{
    if (PSIDnodes_getNum() == -1) { /* NrOfNodes not defined */
	parser_comment(-1, "define NrOfNodes before any host\n");
	return -1;
    }

    if ((id<0) || (id >= PSIDnodes_getNum())) { /* id out of Range */
	parser_comment(-1, "node ID <%d> out of range (NrOfNodes = %d)\n",
		       id, PSIDnodes_getNum());
	return -1;
    }

    if (PSIDnodes_lookupHost(addr)!=-1) { /* duplicated host */
	parser_comment(-1, "duplicated host <%s>\n",
		       inet_ntoa(* (struct in_addr *) &addr));
	return -1;
    }

    if (PSIDnodes_getAddr(id) != INADDR_ANY) { /* duplicated PSI-ID */
	in_addr_t other = PSIDnodes_getAddr(id);
	parser_comment(-1, "duplicated ID <%d> for hosts <%s> and <%s>\n",
		       id, inet_ntoa(* (struct in_addr *) &addr),
		       inet_ntoa(* (struct in_addr *) &other));
	return -1;
    }

    /* install hostname */
    if (PSIDnodes_register(id, addr)) {
	parser_comment(-1, "PSIDnodes_register(%d, <%s>) failed\n",
		       id, inet_ntoa(*(struct in_addr *)&addr));
	return -1;
    }

    /* setup default settings here */

    if (PSIDnodes_setHWType(id, default_hwtype)) {
	parser_comment(-1, "PSIDnodes_setHWType(%d, %d) failed\n",
		       id, default_hwtype);
	return -1;
    }

    if (PSIDnodes_setIsStarter(id, default_canstart)) {
	parser_comment(-1, "PSIDnodes_setIsStarter(%d, %d) failed\n",
		       id, default_canstart);
	return -1;
    }

    if (PSIDnodes_setRunJobs(id, default_runjobs)) {
	parser_comment(-1, "PSIDnodes_setRunJobs(%d, %d) failed\n",
		       id, default_runjobs);
	return -1;
    }

    if (PSIDnodes_setProcs(id, default_procs)) {
	parser_comment(-1, "PSIDnodes_setProcs(%d, %d) failed\n",
		       id, default_procs);
	return -1;
    }

    if (PSIDnodes_setOverbook(id, default_overbook)) {
	parser_comment(-1, "PSIDnodes_setOverbook(%d, %d) failed\n",
		       id, default_overbook);
	return -1;
    }

    if (PSIDnodes_setExclusive(id, default_exclusive)) {
	parser_comment(-1, "PSIDnodes_setExclusive(%d, %d) failed\n",
		       id, default_exclusive);
	return -1;
    }

    if (PSIDnodes_setPinProcs(id, default_pinProcs)) {
	parser_comment(-1, "PSIDnodes_setPinProcs(%d, %d) failed\n",
		       id, default_pinProcs);
	return -1;
    }

    if (PSIDnodes_setBindMem(id, default_bindMem)) {
	parser_comment(-1, "PSIDnodes_setBindMem(%d, %d) failed\n",
		       id, default_bindMem);
	return -1;
    }

    if (PSIDnodes_clearCPUMap(id)) {
	parser_comment(-1, "PSIDnodes_clearCPUMap(%d) failed\n", id);
	return -1;
    }
    if (default_cpumap_size) {
	size_t i;
	for (i=0; i<default_cpumap_size; i++) {
	    if (PSIDnodes_appendCPUMap(id, default_cpumap[i])) {
		parser_comment(-1, "PSIDnodes_appendCPUMap(%d, %d) failed\n",
			       id, default_cpumap[i]);
		return -1;
	    }
	}
    }

    if (pushDefaults(id, PSIDNODES_USER, &defaultUID)) {
	parser_comment(-1, "pushDefaults(%d, PSIDNODES_USER, %p) failed",
		       id, &defaultUID);
	return -1;
    }

    if (pushDefaults(id, PSIDNODES_GROUP, &defaultGID)) {
	parser_comment(-1, "pushDefaults(%d, PSIDNODES_GROUP, %p) failed",
		       id, &defaultGID);
	return -1;
    }
    
    if (pushDefaults(id, PSIDNODES_ADMUSER, &defaultAdmUID)) {
	parser_comment(-1, "pushDefaults(%d, PSIDNODES_ADMUSER, %p) failed",
		       id, &defaultAdmUID);
	return -1;
    }

    if (pushDefaults(id, PSIDNODES_ADMGROUP, &defaultAdmGID)) {
	parser_comment(-1, "pushDefaults(%d, PSIDNODES_ADMGROUP, %p) failed",
		       id, &defaultAdmGID);
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
    {"hwtype", getHW},
    {"runjobs", getRJ},
    {"starter", getCS},
    {"user", getUser},
    {"group", getGroup},
    {"adminuser", getAdminUser},
    {"admingroup", getAdminGroup},
    {"processes", getProcs},
    {"overbook", getOB},
    {"exclusive", getExcl},
    {"pinprocs", getPinProcs},
    {"bindmem", getBindMem},
    {"cpumap", getCPUmap},
    {NULL, parser_error}
};

static parser_t nodeline_parser = {" \t\n", nodeline_list};

static int getNodeLine(char *token)
{
    unsigned int ipaddr;
    int nodenum, ret;
    char *hostname;

    ipaddr = parser_getHostname(token);

    if (!ipaddr) return -1;

    hostname = strdup(token);

    ret = parser_getNumValue(parser_getString(), &nodenum, "node number");
    if (ret) return ret;

    newHost(ipaddr, nodenum);
    currentID = nodenum;

    parser_comment(PARSER_LOG_NODE, "Register '%s' as %d", hostname, nodenum);
    free(hostname);

    ret = parser_parseString(parser_getString(), &nodeline_parser);
    currentID = DEFAULT_ID;

    parser_commentCont(PARSER_LOG_NODE, "\n");

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
	parser_comment(-1, "empty line\n");
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
	parser_comment(-1, "no string found within '%s'\n", line);
	return NULL;
    }

    /* Skip trailing whitespace */
    while (*end==' ' || *end=='\t') end++;
    if (*end) {
	parser_comment(-1, "found trailing garbage '%s' %d\n", end, *end);
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
	parser_comment(-1, "syntax error\n");
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	parser_comment(-1, "premature end of line\n");
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	parser_comment(-1, "no value for %s\n", name);
	return -1;
    }

    /* store environment */
    setenv(name, value, 1);

    parser_comment(PARSER_LOG_RES, "got environment: %s='%s'\n", name , value);

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
	parser_comment(-1, "unknown script type '%s'\n", token);
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	parser_comment(-1, "premature end of line\n");
	return -1;
    }

    value = getQuotedString(line);

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
	parser_comment(-1, "syntax error\n");
	return -1;
    }

    line = parser_getLine();

    if (!line) {
	parser_comment(-1, "premature end of line\n");
	return -1;
    }

    value = getQuotedString(line);

    if (!value) {
	parser_comment(-1, "no value for %s\n", name);
	return -1;
    }

    /* store environment */
    if (HW_getEnv(actHW, name)) {
	parser_comment(-1, "redefineing hardware environment: %s\n", name);
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
    
static int getHandleOldBins(char *token)
{
    config.handleOldBins = 1;
    parser_comment(-1, "recognize old binaries within resource management\n");
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
	parser_comment(-1, "default sorting strategy for nodes is '%s'\n",
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
    {"hwtype", getHW},
    {"runjobs", getRJ},
    {"starter", getCS},
    {"user", getUser},
    {"group", getGroup},
    {"adminuser", getAdminUser},
    {"admingroup", getAdminGroup},
    {"processes", getProcs},
    {"overbook", getOB},
    {"exclusive", getExcl},
    {"pinprocs", getPinProcs},
    {"bindmem", getBindMem},
    {"cpumap", getCPUmap},
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
    {"accountpoll", getAcctPollInterval},
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
    if (PSIDnodes_getNum() > nodesfound) { /* hosts missing in hostlist */
	parser_comment(-1, "WARNING: # to few hosts in hostlist\n");
    }
    
    return &config;
}
