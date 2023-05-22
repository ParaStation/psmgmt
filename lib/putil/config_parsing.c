/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "config_parsing.h"

#ifdef BUILD_WITHOUT_PSCONFIG
config_t *parseConfig(FILE* logfile, int logmask, char *configfile)
{
    return parseOldConfig(logfile, logmask, configfile);
}
#else

#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib.h>
#include <psconfig.h>

#include "parser.h"
#include "psattribute.h"
#include "pscommon.h"
#include "psconfighelper.h"
#include "pspartition.h"
#include "selector.h"
#include "rdp.h"
#include "timer.h"

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
    .psiddomain = NULL,
};

#define ENV_END 17 /* Some magic value */

#define DEFAULT_ID -1
#define GENERATE_ID -2

static int nodesfound = 0;

static PSConfig* psconfig = NULL;
static char* psconfigobj = NULL;
static guint psconfig_flags = PSCONFIG_FLAG_FOLLOW | PSCONFIG_FLAG_INHERIT
				| PSCONFIG_FLAG_ANCESTRAL;

typedef struct {
    char *key;                /**< psconfig key */
    bool (*handler)(char*);   /**< function to read and handle the value */
} confkeylist_t;

typedef struct {
    char *key;                /**< psconfig key */
    int resource;             /**< resource limit identifier */
    bool mult;                /**< flag to multiply value with 1024 */
} suppRLimit_t;

typedef struct {
    long id;
    AttrMask_t attributes;    /**< bit field of enabled attributes */
    bool canstart;
    bool runjobs;
    long procs;
    PSnodes_overbook_t overbook;
    bool exclusive;
    bool pinProcs;
    bool bindMem;
    bool bindGPUs;
    bool bindNICs;
    bool allowUserMap;
    bool supplGrps;
    int maxStatTry;
    short *cpumap;
    size_t cpumap_size;
    size_t cpumap_maxsize;
} nodeconf_t;

static nodeconf_t nodeconf = {
    .id = 0,                       /* local node */
    .attributes = 0,               /* local node */
    .canstart = true,              /* local node */
    .runjobs = true,               /* local node */
    .procs = -1,                   /* local node */
    .overbook = OVERBOOK_FALSE,    /* local node */
    .exclusive = true,             /* local node */
    .pinProcs = true,              /* local node */
    .bindMem = true,               /* local node */
    .bindGPUs = true,              /* local node */
    .bindNICs = true,              /* local node */
    .allowUserMap = false,         /* local node */
    .supplGrps = false,            /* local node */
    .maxStatTry = 1,               /* local node */
    .cpumap = NULL,                /* each node */
    .cpumap_size = 0,              /* each node */
    .cpumap_maxsize = 16           /* each node */
};

/*----------------------------------------------------------------------*/

#define CHECK_PSCONFIG_ERROR_AND_RETURN(val, key, err, ret) { \
    if (val == NULL) { \
	parser_comment(-1, "PSConfig: %s(%s): %s\n", psconfigobj, key,	\
		       (err)->message);					\
	g_error_free(err);						\
	return ret;							\
    } \
}

/*
 * Get string value from psconfigobj in the psconfig configuration.
 *
 * On success, *value is set to the string value and true is returned.
 * On error *value will be NULL, prints a parser comment, and return false
 *
 * Note: For psconfig a non-existing key and an empty value are the same,
 *       so if the key is not found, **value == '\0' and true is returned.
 *
 * @todo Perhaps this should be reworked to not return true if value is empty?
 */
static bool getString(char *key, gchar **value)
{
    GError *err = NULL;

    *value = psconfig_get(psconfig, psconfigobj, key, psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(*value, key, err, false);

    if (**value == '\0') {
	parser_comment(PARSER_LOG_VERB, "PSConfig: %s(%s) does not exist or has"
		" empty value\n", psconfigobj, key);
    }

    return true;
}

static bool toBool(char *token, bool *value)
{
    if (!strcasecmp(token, "true") || !strcasecmp(token, "yes")
	|| !strcasecmp(token, "1")) {
	*value = true;
    } else if (!strcasecmp(token, "false") || !strcasecmp(token, "no")
	|| !strcasecmp(token, "0")) {
	*value = false;
    } else {
	return false;
    }

    return true;
}

static bool getBool(char *key, bool *value)
{
    gchar *token;
    if (!getString(key, &token) || *token == '\0') {
	g_free(token);
	return false;
    }

    bool ret = toBool(token, value);
    if (!ret) {
	parser_comment(-1, "PSConfig: '%s(%s)' cannot convert string '%s' to"
		       " boolean\n", psconfigobj, key, token);
    }
    g_free(token);
    return ret;
}

static bool toNumber(char *token, int *val)
{
    if (!token || *token == '\0') return false;

    char *end;
    int num = (int)strtol(token, &end, 0);
    if (*end) return false;

    *val = num;
    return true;
}

static bool getNumber(char *key, int *val)
{
    gchar *token;
    if (!getString(key, &token) || *token == '\0') {
	g_free(token);
	return false;
    }

    bool ret = toNumber(token, val);
    if (!ret) {
	parser_comment(-1, "PSConfig: '%s(%s)' cannot convert string '%s' to"
		       " number\n", psconfigobj, key, token);
    }
    g_free(token);
    return ret;
}

static bool doForList(char *key, bool (*action)(char *))
{
    GError *err = NULL;
    GPtrArray *list = psconfig_getList(psconfig, psconfigobj, key,
				       psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(list, key, err, false);

    if (!list->len) {
	parser_comment(-1, "PSConfig: '%s(%s)' does not exist or is empty"
		       " list\n", psconfigobj, key);
	return false;
    }

    bool ret = true;
    for (guint i = 0; i < list->len; i++) {
	if (!(ret = action((char*)g_ptr_array_index(list,i)))) break;
    }
    g_ptr_array_free(list, TRUE);

    return ret;
}

/* ---------------------- Stuff for resource limits ------------------------ */

static void setLimit(int limit, rlim_t value)
{
    struct rlimit rlp;

    getrlimit(limit, &rlp);
    rlp.rlim_cur = value;
    if ( value == RLIM_INFINITY
	 || (value > rlp.rlim_max && rlp.rlim_max != RLIM_INFINITY)) {
	rlp.rlim_max = value;
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

static suppRLimit_t suppRLimits[] = {
    { "Psid.ResourceLimits.CpuTime", RLIMIT_CPU, 0 },
    { "Psid.ResourceLimits.DataSize", RLIMIT_DATA, 1 },
    { "Psid.ResourceLimits.StackSize", RLIMIT_STACK, 1 },
    { "Psid.ResourceLimits.RsSize", RLIMIT_RSS, 0 },
    { "Psid.ResourceLimits.MemLock", RLIMIT_MEMLOCK, 1 },
    { "Psid.ResourceLimits.Core", RLIMIT_CORE, 1 },
    { "Psid.ResourceLimits.NoFile", RLIMIT_NOFILE, 0 },
    { NULL, 0, 0 }
};

static bool getRLimit(char *pointer)
{
    for (unsigned int i = 0; suppRLimits[i].key; i++) {
	/* no not break on errors here */
	gchar *limit;
	if (!getString(suppRLimits[i].key, &limit)) continue;

	if (!*limit) {
	    /* limit not set */
	    g_free(limit);
	    continue;
	}

	rlim_t value;
	if (!strcasecmp(limit,"infinity") || !strcasecmp(limit, "unlimited")) {
	    value = RLIM_INFINITY;
	    parser_comment(PARSER_LOG_RES, "got 'RLIM_INFINITY' for '%s'\n",
			   suppRLimits[i].key);
	} else {
	    int intval;
	    if (!toNumber(limit, &intval)) {
		g_free(limit);
		return false;
	    }
	    value = intval;
	    parser_comment(PARSER_LOG_RES, "got '%d' for '%s'\n",
			   intval, suppRLimits[i].key);
	}

	setLimit(suppRLimits[i].resource, (value == RLIM_INFINITY) ? value :
		 suppRLimits[i].mult ? value*1024 : value);
	g_free(limit);
    }

    return true;
}

/*----------------------------------------------------------------------*/

/*
 * Worker routines to set various variables from psconfig
 */
static bool getInstDir(char *key)
{
    struct stat fstat;

    gchar *dname;
    if (!getString(key, &dname)) return false;

    /* test if dir is a valid directory */
    if (*dname == '\0') {
	parser_comment(-1, "directory name is empty\n");
    } else if (stat(dname, &fstat)) {
	parser_comment(-1, "%s: %s\n", dname, strerror(errno));
    } else if (!S_ISDIR(fstat.st_mode)) {
	parser_comment(-1, "'%s' is not a directory\n", dname);
    } else if (strcmp(dname, PSC_lookupInstalldir(dname))) {
	parser_comment(-1, "'%s' seems to be no valid installdir\n", dname);
    } else {
	g_free(dname);
	return true;
    }

    g_free(dname);
    return false;
}

static bool getCoreDir(char *key)
{
    static char *usedDir = NULL;
    struct stat fstat;

    gchar *dname;
    if (!getString(key, &dname)) return false;

    /* test if dir is a valid directory */
    if (*dname == '\0') {
	parser_comment(-1, "directory name is empty\n");
    } else if (stat(dname, &fstat)) {
	parser_comment(-1, "%s: %s\n", dname, strerror(errno));
    } else if (!S_ISDIR(fstat.st_mode)) {
	parser_comment(-1, "'%s' is not a directory\n", dname);
    } else {
	config.coreDir = dname;
	free(usedDir);
	usedDir = config.coreDir;
	setLimit(RLIMIT_CORE, RLIM_INFINITY);

	parser_comment(PARSER_LOG_RES, "set coreDir to '%s'\n", dname);
	return true;
    }

    g_free(dname);
    return false;
}

static bool getMCastUse(char *key)
{
    bool mc;
    if (!getBool(key, &mc)) return false;

    if (mc) {
	config.useMCast = 1;
	parser_comment(-1,
		       "will use MCast. Disable alternative status control\n");
    }

    return true;
}

static bool getMCastGroup(char *key)
{
    return getNumber(key, &config.MCastGroup);
}

static bool getMCastPort(char *key)
{
    return getNumber(key, &config.MCastPort);
}

static bool getRDPPort(char *key)
{
    return getNumber(key, &config.RDPPort);
}

static bool getRDPTimeout(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    if (tmp < MIN_TIMEOUT_MSEC) {
	parser_comment(-1, "RDP timeout %d too small. Ignoring...\n", tmp);
    } else {
	config.RDPTimeout = tmp;
    }
    return true;
}

static bool getRDPMaxRetrans(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    setMaxRetransRDP(tmp);
    return true;
}

static bool getRDPResendTimeout(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    setRsndTmOutRDP(tmp);
    return true;
}

static bool getRDPClosedTimeout(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    setClsdTmOutRDP(tmp);
    return true;
}

static bool getRDPMaxACKPend(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    setMaxAckPendRDP(tmp);
    return true;
}

static bool getRDPStatistics(char *key)
{
    bool tmp;
    if (!getBool(key, &tmp)) return false;

    RDP_setStatistics(tmp);
    return true;
}

static bool getSelectTime(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    config.selectTime = tmp;
    return true;
}

static bool getDeadInterval(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    config.deadInterval = tmp;
    return true;
}

static bool getStatTmout(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    if (tmp < MIN_TIMEOUT_MSEC) {
	parser_comment(-1, "status timeout %d too small. Ignoring...\n", tmp);
    } else {
	config.statusTimeout = tmp;
    }
    return true;
}

static bool getStatBcast(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    if (tmp < 0) {
	parser_comment(-1, "status broadcasts must be positive. Ignoring...\n");
    } else {
	config.statusBroadcasts = tmp;
    }
    return true;
}

static bool getDeadLmt(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    config.deadLimit = tmp;
    return true;
}

static bool getKillDelay(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    config.killDelay = tmp;
    return true;
}

static bool getLogMask(char *key)
{
    return getNumber(key, &config.logMask);
}

static bool getLogDest(char *key)
{
    gchar *value;
    if (!getString(key, &value)) return false;
    if (*value == '\0') {
	parser_comment(-1, "empty destination\n");
	g_free(value);
	return false;
    }

    bool ret = true;
    if (!strcasecmp(value, "LOG_DAEMON")) {
	config.logDest = LOG_DAEMON;
    } else if (!strcasecmp(value, "LOG_KERN")) {
	config.logDest = LOG_KERN;
    } else if (!strcasecmp(value, "LOG_LOCAL0")) {
	config.logDest = LOG_LOCAL0;
    } else if (!strcasecmp(value, "LOG_LOCAL1")) {
	config.logDest = LOG_LOCAL1;
    } else if (!strcasecmp(value, "LOG_LOCAL2")) {
	config.logDest = LOG_LOCAL2;
    } else if (!strcasecmp(value, "LOG_LOCAL3")) {
	config.logDest = LOG_LOCAL3;
    } else if (!strcasecmp(value, "LOG_LOCAL4")) {
	config.logDest = LOG_LOCAL4;
    } else if (!strcasecmp(value, "LOG_LOCAL5")) {
	config.logDest = LOG_LOCAL5;
    } else if (!strcasecmp(value, "LOG_LOCAL6")) {
	config.logDest = LOG_LOCAL6;
    } else if (!strcasecmp(value, "LOG_LOCAL7")) {
	config.logDest = LOG_LOCAL7;
    } else {
	ret = toNumber(value, &config.logDest);
    }

    g_free(value);
    return ret;
}

static bool getFreeOnSusp(char *key)
{
    bool fs;
    if (!getBool(key, &fs)) return false;

    config.freeOnSuspend = fs;
    parser_comment(PARSER_LOG_NODE, "suspended jobs will%s free their"
		   " resources\n", fs ? "" : " not");
    return true;
}

static bool getPSINodesSort(char *key)
{
    gchar *value;
    if (!getString(key, &value)) return false;

    bool ret = true;
    if (!strcasecmp(value, "load1") || !strcasecmp(value, "load_1")) {
	config.nodesSort = PART_SORT_LOAD_1;
    } else if (!strcasecmp(value, "load5") || !strcasecmp(value, "load_5")) {
	config.nodesSort = PART_SORT_LOAD_5;
    } else if (!strcasecmp(value, "load15") || !strcasecmp(value, "load_15")) {
	config.nodesSort = PART_SORT_LOAD_15;
    } else if (!strcasecmp(value, "proc")) {
	config.nodesSort = PART_SORT_PROC;
    } else if (!strcasecmp(value, "proc+load")) {
	config.nodesSort = PART_SORT_PROCLOAD;
    } else if (!strcasecmp(value, "none")) {
	config.nodesSort = PART_SORT_NONE;
    } else {
	parser_comment(-1, "Unknown sorting strategy: '%s'\n", value);
	ret = false;
    }
    g_free(value);

    if (ret) {
	parser_comment(-1, "sorting strategy for nodes is '%s'\n",
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

/* ----------------------- Stuff for hardware types ------------------------ */

static void setHWType(const AttrIdx_t attr)
{
    nodeconf.attributes = attr;
    parser_comment(PARSER_LOG_NODE, " HW '%s'\n", Attr_print(attr));
}

static bool addAttr(char *token, AttrMask_t *attributes)
{
    AttrIdx_t idx = Attr_index(token);

    if (idx < 0) {
	parser_comment(-1, "Hardware type '%s' not available.\n", token);
	return false;
    }

    *attributes |= 1<<idx;
    return true;
}

static bool getHW(char *key)
{
    GError *err = NULL;

    GPtrArray *list = psconfig_getList(psconfig, psconfigobj, key,
				       psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(list, key, err, false);

    bool ret = true;
    if (!list->len) {
	setHWType(0);
    } else {
	AttrMask_t attrs = 0;
	for (guint i = 0; i < list->len; i++) {
	    ret = addAttr((char*)g_ptr_array_index(list,i), &attrs);
	    if (!ret) break;
	}
	if (ret) setHWType(attrs);
    }
    g_ptr_array_free(list, TRUE);
    return ret;
}

/* ---------------------------------------------------------------------- */

/** List type to store group/user entries */
typedef struct {
    struct list_head next;
    unsigned int id;
} GUent_t;

static LIST_HEAD(nodeUID);
static LIST_HEAD(nodeGID);
static LIST_HEAD(nodeAdmUID);
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

static int addID(list_t *list, unsigned int id)
{
    GUent_t *guent;
    unsigned int any = PSNODES_ANYUSER;
    list_t *pos, *tmp;

    if (!list) return -1;

    if (list == &nodeGID || list == &nodeAdmGID) any = PSNODES_ANYGROUP;
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

static int setID(list_t *list, unsigned int id)
{
    if (!list) return -1;

    clear_GUIDlist(list);

    return addID(list, id);
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
	GUIDaction = setID;
	*actionStr = "";
	break;
    }
}

static bool getSingleUser(char *user)
{
    if (!user) {
	parser_comment(-1, "Empty user\n");
	return false;
    }

    char *actStr;
    setAction(&user, &actStr);

    uid_t uid = PSC_uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return false;
    }

    char *uStr = PSC_userFromUID(uid);
    parser_comment(PARSER_LOG_NODE, " user '%s%s'\n", actStr, uStr);
    GUIDaction(&nodeUID, uid);

    free(uStr);
    return true;
}

static bool getUsers(char *key)
{
    return doForList(key, getSingleUser);
}

static bool getSingleGroup(char *group)
{
    if (!group) {
	parser_comment(-1, "Empty group\n");
	return false;
    }

    char *actStr;
    setAction(&group, &actStr);

    gid_t gid = PSC_gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return false;
    }

    char *gStr = PSC_groupFromGID(gid);
    parser_comment(PARSER_LOG_NODE, " group '%s%s'\n", actStr, gStr);
    GUIDaction(&nodeGID, gid);

    free(gStr);
    return true;
}

static bool getGroups(char *key)
{
    return doForList(key, getSingleGroup);
}

static bool getSingleAdminUser(char *user)
{
    if (!user) {
	parser_comment(-1, "Empty user\n");
	return false;
    }

    char *actStr;
    setAction(&user, &actStr);

    uid_t uid = PSC_uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return false;
    }

    char *uStr = PSC_userFromUID(uid);
    parser_comment(PARSER_LOG_NODE, " adminuser '%s%s'\n", actStr, uStr);
    GUIDaction(&nodeAdmUID, uid);

    free(uStr);
    return true;
}

static bool getAdminUsers(char *key)
{
    return doForList(key, getSingleAdminUser);
}

static bool getSingleAdminGroup(char *group)
{
    if (!group) {
	parser_comment(-1, "Empty group\n");
	return false;
    }

    char *actStr;
    setAction(&group, &actStr);

    gid_t gid = PSC_gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return false;
    }

    char *gStr = PSC_groupFromGID(gid);
    parser_comment(PARSER_LOG_NODE, " admingroup '%s%s'\n", actStr, gStr);
    GUIDaction(&nodeAdmGID, gid);

    free(gStr);
    return true;
}

static bool getAdminGroups(char *key)
{
    return doForList(key, getSingleAdminGroup);
}

/* ---------------------------------------------------------------------- */

static bool getCS(char *key)
{
    bool cs;
    if (!getBool(key, &cs)) return false;

    nodeconf.canstart = cs;
    parser_comment(PARSER_LOG_NODE, " starting%s allowed\n", cs ? "":" not");
    return true;
}

static bool getRJ(char *key)
{
    bool rj;
    if (!getBool(key, &rj)) return false;

    nodeconf.runjobs = rj;
    parser_comment(PARSER_LOG_NODE, " jobs%s allowed\n", rj ? "":" not");
    return true;
}

static bool getProcs(char *key)
{
    gchar *procStr;
    if (!getString(key, &procStr)) return false;

    int procs = -1;
    if (!toNumber(procStr, &procs) && strcasecmp(procStr, "any")) {
	parser_comment(-1, "Unknown number of processes '%s'\n", procStr);
	g_free(procStr);
	return false;
    }
    g_free(procStr);

    nodeconf.procs = procs;
    if (procs == -1) {
	parser_comment(PARSER_LOG_NODE, " any");
    } else {
	parser_comment(PARSER_LOG_NODE, " %d", procs);
    }
    parser_comment(PARSER_LOG_NODE, " procs\n");

    return true;
}

static bool getOB(char *key)
{
    gchar *obStr;
    if (!getString(key, &obStr)) return false;

    PSnodes_overbook_t ob;
    if (!strcasecmp(obStr, "auto")) {
	ob = OVERBOOK_AUTO;
	parser_comment(PARSER_LOG_NODE, "got 'auto' for value 'overbook'\n");
    } else {
	bool tmp;
	if (!toBool(obStr, &tmp)) {
	    g_free(obStr);
	    return false;
	}
	ob = tmp ? OVERBOOK_TRUE : OVERBOOK_FALSE;
    }
    g_free(obStr);

    nodeconf.overbook = ob;
    parser_comment(PARSER_LOG_NODE, " overbooking is '%s'\n",
		   ob==OVERBOOK_AUTO ? "auto" : ob ? "TRUE" : "FALSE");

    return true;
}

static bool getExcl(char *key)
{
    bool excl;
    if (!getBool(key, &excl)) return false;

    nodeconf.exclusive = excl;
    parser_comment(PARSER_LOG_NODE, " exclusive assign%s allowed\n",
		   excl ? "":" not");
    return true;
}

static bool getPinProcs(char *key)
{
    bool pinProcs;
    if (!getBool(key, &pinProcs)) return false;

    nodeconf.pinProcs = pinProcs;
    parser_comment(PARSER_LOG_NODE, " processes are%s pinned\n",
		   pinProcs ? "":" not");
    return true;
}

static bool getBindMem(char *key)
{
    bool bindMem;
    if (!getBool(key, &bindMem)) return false;

    nodeconf.bindMem = bindMem;
    parser_comment(PARSER_LOG_NODE, " memory is%s bound\n",
		   bindMem ? "":" not");
    return true;
}

static bool getBindGPUs(char *key)
{
    bool bindGPUs;
    if (!getBool(key, &bindGPUs)) return false;

    nodeconf.bindGPUs = bindGPUs;
    parser_comment(PARSER_LOG_NODE, " GPUs get%s bound\n",
		   bindGPUs ? "":" not");
    return true;
}

static bool getBindNICs(char *key)
{
    bool bindNICs;
    if (!getBool(key, &bindNICs)) return false;

    nodeconf.bindNICs = bindNICs;
    parser_comment(PARSER_LOG_NODE, " NICs get%s bound\n",
		   bindNICs ? "":" not");
    return true;
}

static bool getAllowUserMap(char *key)
{
    bool allowMap;
    if (!getBool(key, &allowMap)) return false;

    nodeconf.allowUserMap = allowMap;
    parser_comment(PARSER_LOG_NODE, " user's CPU-mapping is%s allowed\n",
		   allowMap ? "":" not");
    return true;
}

static bool getSupplGrps(char *key)
{
    bool supplGrps;
    if (!getBool(key, &supplGrps)) return false;

    nodeconf.supplGrps = supplGrps;
    parser_comment(PARSER_LOG_NODE, " supplementary groups are%s set\n",
		   supplGrps ? "":" not");
    return true;
}

static bool getMaxStatTry(char *key)
{
    int tmp;
    if (!getNumber(key, &tmp)) return false;

    nodeconf.maxStatTry = tmp;
    parser_comment(PARSER_LOG_NODE, " maxStatTry are '%d'\n", tmp);

    return true;
}

/* ---------------------------------------------------------------------- */

static bool getCPUmapEnt(char *token)
{
    int val;
    if (!toNumber(token, &val)) return false;

    if (nodeconf.cpumap == NULL) {
	nodeconf.cpumap_maxsize = 16;
	nodeconf.cpumap = malloc(nodeconf.cpumap_maxsize
				 * sizeof(*nodeconf.cpumap));
    } else if (nodeconf.cpumap_size == nodeconf.cpumap_maxsize) {
	nodeconf.cpumap_maxsize *= 2;
	nodeconf.cpumap = realloc(nodeconf.cpumap, nodeconf.cpumap_maxsize
				  * sizeof(*nodeconf.cpumap));
    }
    if (nodeconf.cpumap == NULL) {
	parser_comment(-1, "%s: No memory for nodeconf.cpumap\n", __func__);
	return false;
    }
    nodeconf.cpumap[nodeconf.cpumap_size] = val;
    nodeconf.cpumap_size++;

    parser_comment(PARSER_LOG_NODE, " %d", val);
    return true;
}

static bool getCPUmap(char *key)
{
    nodeconf.cpumap_size = 0;

    parser_comment(PARSER_LOG_NODE, " CPUMap {");
    bool ret = doForList(key, getCPUmapEnt);
    parser_comment(PARSER_LOG_NODE, " }\n");

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

static bool setEnv(char *key, char *val)
{
    EnvEnt_t *envent;

    /* store environment */
    envent = malloc(sizeof(*envent));
    if (!envent) {
	parser_comment(-1, "%s: No memory\n", __func__);
	return false;
    }

    envent->name = strdup(key);
    envent->value = strdup(val);
    if (!envent->name || !envent->value) {
	parser_comment(-1, "%s: No memory\n", __func__);
	free(envent->name);
	free(envent->value);
	free(envent);
	return false;
    }

    list_add_tail(&envent->next, &envList);

    parser_comment(PARSER_LOG_NODE, " env %s='%s'\n", key, val);
    return true;
}

static bool getEnv(char *key)
{
    GError *err = NULL;
    GPtrArray *env = psconfig_getList(psconfig, psconfigobj, key,
				      psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(env, key, err, false);

    if (env->len % 2 != 0) {
	parser_comment(-1, "Invalid environment size: must be even\n");
	return false;
    }

    bool ret = true;
    for (guint i = 0; (i+1) < env->len; i += 2) {
	gchar *var = (gchar*)g_ptr_array_index(env,i);
	gchar *val = (gchar*)g_ptr_array_index(env,i+1);

	if (!(ret = setEnv(var, val))) break;
    }
    g_ptr_array_free(env, TRUE);

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

/*----------------------------------------------------------------------*/
static confkeylist_t each_node_configkey_list[] = {
    {"Psid.CpuMap", getCPUmap},
    {NULL, NULL}
};

/**
 * @brief Insert a node
 *
 * Helper function to make a node known to the ParaStation daemon.
 *
 * @param id ParaStation ID of the node to register
 *
 * @param nodename Symbolic name of the node to register
 *
 * @param addr IP address of the node to register
 *
 * @param hostname Hostname the node has in the network @a addr belongs to
 *		   (may be NULL)
 *
 * @return Return false if an error occurred or true if the node was
 * inserted successfully.
 */
static bool newHost(int id, char *nodename, in_addr_t addr, char *hostname)
{
    if (id < 0) { /* id out of Range */
	parser_comment(-1, "node ID <%d> out of range\n", id);
	return false;
    }

    if ((ntohl(addr) >> 24) == IN_LOOPBACKNET) {
	parser_comment(-1, "node ID <%d> resolves to address <%s> within"
		       " loopback range\n",
		       id, inet_ntoa(* (struct in_addr *) &addr));
	return false;
    }

    if (PSIDnodes_lookupHost(addr) != -1) { /* duplicated host */
	parser_comment(-1, "duplicated host <%s>\n",
		       inet_ntoa(* (struct in_addr *) &addr));
	return false;
    }

    if (PSIDnodes_getAddr(id) != INADDR_ANY) { /* duplicated PSI-ID */
	in_addr_t other = PSIDnodes_getAddr(id);
	parser_comment(-1, "duplicated ID <%d> for hosts <%s>",
		       id, inet_ntoa(* (struct in_addr *) &addr));
	parser_comment(-1, " and <%s>\n",
		       inet_ntoa(* (struct in_addr *) &other));
	return false;
    }

    if (!PSIDnodes_register(id, nodename, addr, hostname)) {
	parser_comment(-1, "PSIDnodes_register(%d, %s, <%s>, %s) failed\n",
		       id, nodename, inet_ntoa(*(struct in_addr *)&addr),
		       hostname ? hostname : "<none>");
	return false;
    }

    if (addr == INADDR_NONE) { /* dynamic node */
	PSIDnodes_setIsDynamic(id, true);
    }

    nodesfound++;

    parser_comment(PARSER_LOG_VERB,
		   "%s: host <%s> inserted in hostlist with id=%d.\n",
		   __func__, inet_ntoa(* (struct in_addr *) &addr), id);

    // get parameters this node
    bool fail = false;
    for (size_t i = 0; each_node_configkey_list[i].key; i++) {
	parser_comment(PARSER_LOG_VERB, "%s: processing config key '%s'\n",
		       __func__, each_node_configkey_list[i].key);
	if (!each_node_configkey_list[i].handler(
		each_node_configkey_list[i].key)) {
	    parser_comment(-1, "Processing config key '%s' for node %d"
			   " failed\n", each_node_configkey_list[i].key, id);
	    fail = true;
	}
    }
    if (fail) return false;

    /* set cpu map for new node */
    if (nodeconf.cpumap_size) {
	size_t c;
	for (c = 0; c < nodeconf.cpumap_size; c++) {
	    if (PSIDnodes_appendCPUMap(id, nodeconf.cpumap[c])) {
		parser_comment(-1, "PSIDnodes_appendCPUMap(%d, %d) failed\n",
			       id, nodeconf.cpumap[c]);
		return false;
	    }
	}
    }

    return true;
}

/* ---------------------------------------------------------------------- */

/*
    - works on node those obj is currently in psconfigobj
    - as a side effect set id of local node in nodeconf struct
*/
static bool insertNode(void)
{
    // ignore this host object silently if NodeName is not set
    gchar *nodename;
    if (!getString("NodeName", &nodename)) return 0;
    if (*nodename == '\0') {
	g_free(nodename);
	return true;
    }

    // get parameters for the node
    gchar *netname;
    if (!getString("Psid.NetworkName", &netname)) {
	g_free(nodename);
	return false;
    }

    if (*netname == '\0') {
	parser_comment(-1, "empty network name for node '%s'\n", nodename);
	g_free(netname);
	g_free(nodename);
	return false;
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s.DevIPAddress", netname);
    gchar *ipaddress;
    if (!getString(buffer, &ipaddress)) {
	g_free(netname);
	g_free(nodename);
	return false;
    }

    if (*ipaddress == '\0') {
	parser_comment(-1, "empty value of '%s' for node '%s'\n", buffer,
		nodename);
	g_free(ipaddress);
	g_free(netname);
	g_free(nodename);
	return false;
    }

    in_addr_t ipaddr = INADDR_NONE;
    if (!strcmp(ipaddress, "DYNAMIC")) {
	parser_comment(PARSER_LOG_VERB, "node '%s' has dynamic IP address\n",
		nodename);
    } else {
	struct in_addr tmpaddr;
	if (!inet_pton(AF_INET, ipaddress, &tmpaddr)) {
	    parser_comment(-1, "Cannot convert IP address '%s' for node '%s'\n",
		    ipaddress, nodename);
	    g_free(ipaddress);
	    g_free(netname);
	    g_free(nodename);
	    return false;
	}
	ipaddr = tmpaddr.s_addr;
    }
    g_free(ipaddress);

    // get hostname used for the node in the psid network
    gchar *hostname;
    snprintf(buffer, sizeof(buffer), "%s.Hostname", netname);
    g_free(netname);
    if (!getString(buffer, &hostname)) {
	g_free(nodename);
	return false;
    }
    if (*hostname == '\0') {
	/* missing hostname is not an error for the time being */
	g_free(hostname);
	hostname = NULL;
    } else {
	parser_comment(PARSER_LOG_NODE, "Found '%s' = '%s' for node '%s'\n",
		       buffer, hostname, nodename);
    }

    int nodeid;
    if (!getNumber("Psid.NodeId", &nodeid)) {
	if (!getNumber("NodeNo", &nodeid)) {
	    parser_comment(-1, "Psid.NodeId not set for node '%s'\n", nodename);
	    g_free(nodename);
	    g_free(hostname);
	    return false;
	}
	parser_comment(-1, "NodeNo used node '%s'. NodeNo is deprecated and"
		       " support will be removed. Use Psid.NodeId instead.\n",
		       nodename);
    }

    parser_comment(PARSER_LOG_NODE, "Register '%s' as %d\n", nodename, nodeid);
    parser_updateHash(&config.nodeListHash, nodename);

    if (!newHost(nodeid, nodename, ipaddr, hostname)) {
	g_free(nodename);
	g_free(hostname);
	return false;
    }

    if (PSC_isLocalIP(ipaddr)) {
	nodeconf.id = nodeid;
	PSC_setMyID(nodeid);
    }

    g_free(nodename);
    g_free(hostname);
    return true;
}

static bool getNodes(char *psiddomain)
{
    GError *err = NULL;

    gchar *parents[] = { "class:host", NULL };
    GHashTable* keyvals = g_hash_table_new(g_str_hash,g_str_equal);
    GPtrArray *nodeobjlist = psconfig_getObjectList(psconfig, "host:*", parents,
						    keyvals, psconfig_flags,
						    &err);
    g_hash_table_unref(keyvals);
    if (!nodeobjlist) {
	parser_comment(-1, "PSConfig: getObjectList(host:*): %s\n",
		       err->message);
	g_error_free(err);
	return false;
    }

    char *psconfigobj_old = psconfigobj;

    bool ret = true;
    for (guint i = 0; i < nodeobjlist->len; i++) {
	psconfigobj = (gchar*)g_ptr_array_index(nodeobjlist,i);

	/* check psiddomain if set */
	if (psiddomain) {
	    gchar *domain;
	    /* ignore errors */
	    if (!getString("Psid.Domain", &domain)) continue;
	    /* ignore nodes with wrong psid domain */
	    if (strcmp(domain, psiddomain)) {
		g_free(domain);
		continue;
	    }
	    g_free(domain);
	}

	if (!(ret = insertNode())) break;
    }
    g_ptr_array_free(nodeobjlist, TRUE);

    psconfigobj = psconfigobj_old;

    parser_comment(PARSER_LOG_NODE, "%d nodes registered\n", nodesfound);

    return ret;
}

/* ---------------------------------------------------------------------- */

static AttrIdx_t thisHW = -1; // @todo

static bool setHardwareScript(char *type, char *value)
{
    char *name;
    if (!strcasecmp(type, "startscript")) {
	name = HW_STARTER;
    } else if (!strcasecmp(type, "stopscript")) {
	name = HW_STOPPER;
    } else if (!strcasecmp(type, "setupscript")) {
	name = HW_SETUP;
    } else if (!strcasecmp(type, "headerscript")) {
	name = HW_HEADERLINE;
    } else if (!strcasecmp(type, "statusscript")) {
	name = HW_COUNTER;
    } else {
	parser_comment(-1, "unknown script type '%s'\n", type);
	return false;
    }

    /* store script */
    if (HW_getScript(thisHW, name)) {
	parser_comment(-1, "redefineing hardware script: %s\n", name);
    }
    HW_setScript(thisHW, name, value);

    parser_comment(PARSER_LOG_RES, "got hardware script: %s='%s'\n",
		   name, value);
    return true;
}

static bool setHardwareEnv(char *key, char *value)
{
    /* store environment */
    if (HW_getEnv(thisHW, key)) {
	parser_comment(-1, "redefineing hardware environment: %s\n", key);
    }
    HW_setEnv(thisHW, key, value);

    parser_comment(PARSER_LOG_RES, "got hardware environment: %s='%s'\n",
		   key, value);
    return true;
}

static char* hwtype_scripts[] = {
    "StartScript",
    "StopScript",
    "SetupScript",
    "HeaderScript",
    "StatusScript",
    NULL
};

static bool getHardwareOptions(char *name)
{
    int objlen = strlen(name) + 12;
    gchar obj[objlen];
    gint len = g_snprintf(obj, objlen, "psidhwtype:%s", name);
    if (len < 0 || len >= objlen) return false;

    for (guint i = 0; hwtype_scripts[i]; i++) {
	GError *err = NULL;
	gchar *key = (gchar*)hwtype_scripts[i];
	gchar *val = psconfig_get(psconfig, obj, key, psconfig_flags, &err);
	if (!val) {
	    if (g_error_matches(err, PSCONFIG_ERROR,
				PSCONFIG_ERROR_OBJNOTEXIST)) {
		// it's perfectly ok to have hwtypes with some options missing
		g_error_free(err);
		continue;
	    } else {
		parser_comment(-1, "Failed to get %s of hwtype '%s': %s\n", key,
			       name, err->message);
		g_error_free(err);
		return false;
	    }
	}

	bool ret = setHardwareScript(key, val);
	g_free(val);
	if (!ret) return false;
    }

    GError *err = NULL;
    GPtrArray *env = psconfig_getList(psconfig, obj, "Environment",
				      psconfig_flags, &err);

    // it's fine to have hwtype options w/o environment
    if (!env) return true;

    // holds the detected config style
    // 0 means config style yet undetected
    // 1 means new config style having "key=value" list elements
    // 2 means old config style having key, value alternating list entries
    int env_config_style = 0;

    for (guint i = 0; i < env->len; i++) {
	gchar *key = (gchar*)g_ptr_array_index(env, i);
	gchar *val = strstr(key, "=");
	if (!val) {
	    if (!env_config_style) {
		env_config_style = 2; // old style detected
		break;
	    }
	    parser_comment(-1, "Invalid environment for hwtype '%s'\n", name);
	    g_ptr_array_free(env, TRUE);
	    return false;
	}
	env_config_style = 1; // new style detected

	*val = '\0';
	val++;

	if (!setHardwareEnv(key, val)) {
	    g_ptr_array_free(env, TRUE);
	    return false;
	}
    }

    if (env_config_style == 2) {
	// warn about old style config
	parser_comment(-1, "Old style environment config used in hwtype '%s'."
		       " You should update your configuration.\n", name);

	if (env->len % 2 != 0) {
	    parser_comment(-1, "Invalid environment setting for hwtype '%s'\n",
			   name);
	    g_ptr_array_free(env, TRUE);
	    return false;
	}

	for (guint i = 0; (i+1) < env->len; i+=2) {
	    gchar *key = (gchar*)g_ptr_array_index(env, i);
	    gchar *val = (gchar*)g_ptr_array_index(env, i+1);

	    if (!setHardwareEnv(key, val)) {
		g_ptr_array_free(env, TRUE);
		return false;
	    }
	}
    }
    g_ptr_array_free(env, TRUE);

    return true;
}

static bool getHardware(char *name)
{
    if (!name) {
	parser_comment(-1, "no hardware name\n");
	return false;
    }

    thisHW = Attr_index(name);

    if (thisHW == -1) {
	thisHW = Attr_add(name);

	parser_comment(PARSER_LOG_RES, "new attribute '%s' registered as %d\n",
		       name, thisHW);
    }

    return getHardwareOptions(name);
}

static bool getHardwareList(char *key)
{
    return doForList(key, getHardware);
}

/* ---------------------------------------------------------------------- */

static bool getPluginEnt(char *token)
{
    parser_comment(PARSER_LOG_RES, "Scheduled plugin for loading: '%s'\n",
		   token);

    nameList_t *new = malloc(sizeof(*new));
    if (!new) parser_exit(errno, "%s", __func__);

    new->name = strdup(token);
    list_add_tail(&new->next, &config.plugins);

    return true;
}

static bool getPlugins(char *key)
{
    return doForList(key, getPluginEnt);
}

/* ---------------------------------------------------------------------- */

static bool getDaemonScript(char *key)
{
    char *sname;
    if (!strcmp(key, "Psid.StartupScript")) {
	sname = "startupScript";
    } else if (!strcmp(key, "Psid.NodeUpScript")) {
	sname = "nodeUpScript";
    } else if (!strcmp(key, "Psid.NodeDownScript")) {
	sname = "nodeDownScript";
    } else {
	return false;
    }

    gchar *value;
    if (!getString(key, &value)) return false;

    if (*value == '\0') {
	// script not set
	g_free(value);
	return true;
    }

    if (PSID_registerScript(&config, sname, value)) {
	parser_comment(-1, "failed to register script '%s' to type '%s'\n",
		       value, sname);
	return false;
    }

    return true;
}

/* ---------------------------------------------------------------------- */

static bool deprecated(char *key)
{
    gchar *value;
    if (!getString(key, &value)) return false;

    if (*value == '\0') {
	// key not set, great
	g_free(value);
	return true;
    }

    /* warn about deprecated keys still in use */
    struct {
	char *depr;
	char *repl;
    } keyList[] = {
	{ "Psid.RdpStatusTimeout", "Psid.StatusTimeout" },
	{ "Psid.RdpStatusDeadLimit", "Psid.DeadLimit" },
	{ "Psid.RdpStatusBroadcasts", "Psid.StatusBroadcasts" },
	{ NULL, NULL }
    };

    for (int i = 0; keyList[i].depr; i++) {
	if (!strcmp(key, keyList[i].depr)) {
	    parser_comment(-1, "Deprecated key '%s' found", keyList[i].depr);
	    if (keyList[i].repl) {
		parser_comment(-1, ", changed to '%s'", keyList[i].repl);
	    }
	    parser_comment(-1, "\n");
	}
    }

    /* do not allow psid to start if deprecated keys are set */
    return false;
}

static confkeylist_t local_node_configkey_list[] = {
    {"Psid.InstallDirectory", getInstDir},
    {"Psid.CoreDirectory", getCoreDir},
    {"Psid.AvailableHardwareTypes", getHardwareList},
    {"Psid.HardwareTypes", getHW},
    {"Psid.RunJobs", getRJ},
    {"Psid.IsStarter", getCS},
    {"Psid.AllowedUsers", getUsers},
    {"Psid.AllowedGroups", getGroups},
    {"Psid.AdminUsers", getAdminUsers},
    {"Psid.AdminGroups", getAdminGroups},
    {"Psid.MaxNumberOfProcesses", getProcs},
    {"Psid.AllowOverbooking", getOB},
    {"Psid.AllowExclusive", getExcl},
    {"Psid.PinProcesses", getPinProcs},
    {"Psid.BindMemory", getBindMem},
    {"Psid.BindGpus", getBindGPUs},
    {"Psid.BindNics", getBindNICs},
    {"Psid.AllowUserCpuMap", getAllowUserMap},
    {"Psid.SetSupplementaryGroups", getSupplGrps},
    {"Psid.MaxStatTry", getMaxStatTry},
    {"Psid.UseMCast", getMCastUse},
    {"Psid.MCastGroup", getMCastGroup},
    {"Psid.MCastPort", getMCastPort},
    {"Psid.RdpPort", getRDPPort},
    {"Psid.RdpTimeout", getRDPTimeout},
    {"Psid.RdpMaxRetrans", getRDPMaxRetrans},
    {"Psid.RdpResendTimeout", getRDPResendTimeout},
    {"Psid.RdpClosedTimeout", getRDPClosedTimeout},
    {"Psid.RdpMaxAckPending", getRDPMaxACKPend},
    {"Psid.EnableRdpStatistics", getRDPStatistics},
    {"Psid.SelectTime", getSelectTime},
    {"Psid.MCastDeadInterval", getDeadInterval},
    {"Psid.RdpStatusTimeout", deprecated}, /* deprecated */
    {"Psid.StatusTimeout", getStatTmout},
    {"Psid.RdpStatusDeadLimit", deprecated}, /* deprecated */
    {"Psid.DeadLimit", getDeadLmt},
    {"Psid.RdpStatusBroadcasts", deprecated}, /* deprecated */
    {"Psid.StatusBroadcasts", getStatBcast},
    {"Psid.KillDelay", getKillDelay},
    {"Psid.ResourceLimits.", getRLimit},
    {"Psid.LogMask", getLogMask},
    {"Psid.LogDestination", getLogDest},
    {"Psid.FreeOnSuspend", getFreeOnSusp},
    {"Psid.PsiNodesSortStrategy", getPSINodesSort},
    {"Psid.LoadPlugins", getPlugins},
    {"Psid.StartupScript", getDaemonScript},
    {"Psid.NodeUpScript", getDaemonScript},
    {"Psid.NodeDownScript", getDaemonScript},
    {"Psid.Environment", getEnv},
    {NULL, NULL}
};

/** @brief Setup local node settings
 *
 * @return On success, true is returned. Or false, if any error occurred.
 */
static bool setupLocalNode(void)
{
    // get parameters for local node
    bool fail = false;
    for (int i = 0; local_node_configkey_list[i].key; i++) {
	parser_comment(PARSER_LOG_VERB, "%s: processing config key '%s'\n",
		       __func__, local_node_configkey_list[i].key);
	if (!local_node_configkey_list[i].handler(
		local_node_configkey_list[i].key)) {
	    parser_comment(-1, "Processing config key '%s' failed\n",
			   local_node_configkey_list[i].key);
	    fail = true;
	}
    }
    if (fail) return false;

    // setup local node
    if (!PSIDnodes_setAttr(nodeconf.id, nodeconf.attributes)) {
	parser_comment(-1, "PSIDnodes_setAttr(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.attributes);
	return false;
    }

    if (PSIDnodes_setIsStarter(nodeconf.id, nodeconf.canstart)) {
	parser_comment(-1, "PSIDnodes_setIsStarter(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.canstart);
	return false;
    }

    if (PSIDnodes_setRunJobs(nodeconf.id, nodeconf.runjobs)) {
	parser_comment(-1, "PSIDnodes_setRunJobs(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.runjobs);
	return false;
    }

    if (PSIDnodes_setProcs(nodeconf.id, nodeconf.procs)) {
	parser_comment(-1, "PSIDnodes_setProcs(%ld, %ld) failed\n",
		       nodeconf.id, nodeconf.procs);
	return false;
    }

    if (PSIDnodes_setOverbook(nodeconf.id, nodeconf.overbook)) {
	parser_comment(-1, "PSIDnodes_setOverbook(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.overbook);
	return false;
    }

    if (PSIDnodes_setExclusive(nodeconf.id, nodeconf.exclusive)) {
	parser_comment(-1, "PSIDnodes_setExclusive(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.exclusive);
	return false;
    }

    if (PSIDnodes_setPinProcs(nodeconf.id, nodeconf.pinProcs)) {
	parser_comment(-1, "PSIDnodes_setPinProcs(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.pinProcs);
	return false;
    }

    if (PSIDnodes_setBindMem(nodeconf.id, nodeconf.bindMem)) {
	parser_comment(-1, "PSIDnodes_setBindMem(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.bindMem);
	return false;
    }

    if (PSIDnodes_setBindGPUs(nodeconf.id, nodeconf.bindGPUs)) {
	parser_comment(-1, "PSIDnodes_setBindGPUs(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.bindGPUs);
	return false;
    }

    if (PSIDnodes_setBindNICs(nodeconf.id, nodeconf.bindNICs)) {
	parser_comment(-1, "PSIDnodes_setBindNICs(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.bindNICs);
	return false;
    }

    if (PSIDnodes_setAllowUserMap(nodeconf.id, nodeconf.allowUserMap)) {
	parser_comment(-1, "PSIDnodes_setAllowUserMap(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.allowUserMap);
	return false;
    }

    if (PSIDnodes_setSupplGrps(nodeconf.id, nodeconf.supplGrps)) {
	parser_comment(-1, "PSIDnodes_setSupplGrps(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.supplGrps);
	return false;
    }

    if (PSIDnodes_setMaxStatTry(nodeconf.id, nodeconf.maxStatTry)) {
	parser_comment(-1, "PSIDnodes_setMaxStatTry(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.maxStatTry);
	return false;
    }

    // setup environment of the local node
    pushAndClearEnv();

    if (pushGUID(nodeconf.id, PSIDNODES_USER, &nodeUID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_USER, %p) failed\n",
		       nodeconf.id, &nodeUID);
	return false;
    }

    if (pushGUID(nodeconf.id, PSIDNODES_GROUP, &nodeGID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_GROUP, %p) failed\n",
		       nodeconf.id, &nodeGID);
	return false;
    }

    if (pushGUID(nodeconf.id, PSIDNODES_ADMUSER, &nodeAdmUID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_ADMUSER, %p) failed\n",
		       nodeconf.id, &nodeAdmUID);
	return false;
    }

    if (pushGUID(nodeconf.id, PSIDNODES_ADMGROUP, &nodeAdmGID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_ADMGROUP, %p) failed\n",
		       nodeconf.id, &nodeAdmGID);
	return false;
    }

    return true;
}

static void cleanup(void)
{
    free(nodeconf.cpumap);
    nodeconf.cpumap = NULL;
    nodeconf.cpumap_maxsize = 0;

    psconfigobj = NULL;
    psconfig_unref(psconfig);
    psconfig = NULL;
}

config_t *parseConfig(FILE* logfile, int logmask, char *configfile)
{
    /* Check if configfile exists and has not length 0.
       If so, use it, else use psconfig. */

    struct stat buf;

    if (stat(configfile, &buf)) {
	parser_comment(1, "%s: %s => using psconfig\n", configfile,
		       strerror(errno));
    } else if (buf.st_size == 0) {
	parser_comment(1, "%s has zero length => using psconfig\n",
		       configfile);
    } else {
	return parseOldConfig(logfile, logmask, configfile);
    }

    /*** use psconfig ***/

    INIT_LIST_HEAD(&config.plugins);

    parser_init(logfile, NULL); //TODO

    parser_setDebugMask(logmask); //TODO

    gchar *psiddomain = NULL;

    // open psconfig database
    psconfig = psconfig_new();
    // generate local psconfig host object name
    psconfigobj = PSCfgHelp_getObject(psconfig, psconfig_flags, parserlogger,
				      PARSER_LOG_VERB);
    if (!psconfigobj) {
	parser_comment(-1, "ERROR: no valid host object for this node.\n");
	goto parseConfigError;
    }

    // get local psid domain
    if (!getString("Psid.Domain", &psiddomain)) {
	parser_comment(-1, "INFO: No psid domain configured, using all host"
		       " objects.\n");
    }

    // get hostname to ID mapping
    if (!getNodes(psiddomain)) {
	parser_comment(-1, "ERROR: Reading nodes configuration from psconfig"
		       " failed.\n");
	goto parseConfigError;
    }

    // did we find ourself in the node list?
    if (PSC_getMyID() == -1) {
	int id;
	if (!getNumber("Psid.NodeId", &id)) {
	    parser_comment(-1, "ERROR: Failed to get local Psid.NodeId\n");
	    goto parseConfigError;
	}
	PSnodes_ID_t nodeid = id;
	if (PSIDnodes_getAddr(nodeid) == INADDR_NONE) { /* dynamic node */
	    nodeconf.id = nodeid;
	    PSC_setMyID(nodeid);

	    const char *hostname = PSIDnodes_getHostname(nodeid);
	    if (!hostname) hostname = PSIDnodes_getNodename(nodeid);
	    if (!hostname) {
		parser_comment(-1, "PSConfig-Error: No hostname or nodename"
			" found for dynamic local node (id %d)\n", nodeid);
		goto parseConfigError;
	    }
	    in_addr_t addr = parser_getHostname(hostname);
	    if (!addr) {
		parser_comment(-1, "PSConfig-Error: No address found for"
			" dynamic local node (hostname %s id %d)\n", hostname,
			nodeid);
		goto parseConfigError;
	    }
	    PSIDnodes_setAddr(nodeid, addr);
	} else {
	    parser_comment(-1, "PSConfig-Error: Local node not configured.\n"
		    " The host object for this node needs to contain a valid"
		    " NodeName\n"
		    " and <Psid.NetworkName>.DevIPAddress matching a local IP"
		    " address or the magic DYNAMIC keyword.\n");
	    goto parseConfigError;
	}
    }

    // set default UID/GID for local node
    setID(&nodeUID, PSNODES_ANYUSER);
    setID(&nodeGID, PSNODES_ANYGROUP);
    setID(&nodeAdmUID, 0);
    setID(&nodeAdmGID, 0);

    // read the configuration for the local node
    if (!setupLocalNode()) {
	parser_comment(-1,
		       "ERROR: Reading configuration from psconfig failed.\n");
	goto parseConfigError;
    }

    /* Sanity Checks */
    if (PSIDnodes_getNum() == -1) {
	parser_comment(-1, "ERROR: No Nodes found.\n");
	goto parseConfigError;
    }

    cleanup();
    parser_finalize(); //TODO

    config.psiddomain = psiddomain;
    return &config;

parseConfigError:
    g_free(psiddomain);
    cleanup();
    return NULL;
}
#endif /* BUILD_WITHOUT_PSCONFIG */
