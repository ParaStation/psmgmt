/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifdef BUILD_WITHOUT_PSCONFIG
#include "config_parsing.h"

config_t *parseConfig(FILE* logfile, int logmask, char *configfile)
{
    return parseOldConfig(logfile, logmask, configfile);
}
#else

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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>
#include <glib.h>
#include <psconfig.h>
#include <psconfig-utils.h>

#include "parser.h"
#include "psnodes.h"

#include "pscommon.h"
#include "hardware.h"
#include "pspartition.h"
#include "timer.h"
#include "rdp.h"
#include "selector.h"

#include "psidnodes.h"
#include "psidstatus.h"
#include "psidscripts.h"

#include "config_parsing.h"

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
    .acctPollInterval = 0,
    .killDelay = 10,
    .startupScript = NULL,
    .nodeUpScript = NULL,
    .nodeDownScript = NULL,

};

#define ENV_END 17 /* Some magic value */

#define DEFAULT_ID -1
#define GENERATE_ID -2

static int nodesfound = 0;

static PSConfig* psconfig = NULL;
static char* psconfigobj = NULL;
static guint psconfig_flags = PSCONFIG_FLAG_FOLLOW | PSCONFIG_FLAG_INHERIT
				| PSCONFIG_FLAG_ANCESTRAL;

typedef struct confkeylist_T {
    char *key;                /**< psconfig key */
    int (*handler)(char*);    /**< function to read and handle the value */
} confkeylist_t;

typedef struct suprlimit_T {
    char *key;                /**< psconfig key */
    int resource;             /**< ressource limit identifier */
    int mult;                 /**< flag to multiply value with 1024 */
} suprlimit_t;

typedef struct nodeconf_T {
    long id;
    unsigned int hwtype;     /**< bit field of enabled hw types */
    int canstart;
    int runjobs;
    long procs;
    PSnodes_overbook_t overbook;
    int exclusive;
    int pinProcs;
    int bindMem;
    int bindGPUs;
    int allowUserMap;
    int supplGrps;
    int maxStatTry;
    short *cpumap;
    size_t cpumap_size;
    size_t cpumap_maxsize;
} nodeconf_t;

static nodeconf_t nodeconf = {
    .id = 0,                       /* local node */
    .hwtype = 0,                   /* local node */
    .canstart = 1,                 /* local node */
    .runjobs = 1,                  /* local node */
    .procs = -1,                   /* local node */
    .overbook = OVERBOOK_FALSE,    /* local node */
    .exclusive = 1,                /* local node */
    .pinProcs = 1,                 /* local node */
    .bindMem = 1,                  /* local node */
    .bindGPUs = 1,                 /* local node */
    .allowUserMap = 0,             /* local node */
    .supplGrps = 0,                /* local node */
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
 * On success, *value is set to the string value and 0 is returned.
 * On error a parser comment is printed, *value is set to NULL and -1 returned.
 *
 * Note: For psconfig an non existing key and an empty value is the same
 */
static int getString(char *key, gchar **value)
{
    GError *err = NULL;

    *value = psconfig_get(psconfig, psconfigobj, key, psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(*value, key, err, -1);

    if (**value == '\0') {
	parser_comment(PARSER_LOG_VERB, "PSConfig: %s(%s) does not exist or has"
		" empty value\n", psconfigobj, key);
    }

    return 0;
}

static int toBool(char *token, int *value)
{
    int ret;
    ret = 0;

    if (strcasecmp(token, "true")==0) {
	*value = 1;
    } else if (strcasecmp(token, "false")==0) {
	*value = 0;
    } else if (strcasecmp(token, "yes")==0) {
	*value = 1;
    } else if (strcasecmp(token, "no")==0) {
	*value = 0;
    } else if (strcasecmp(token, "1")==0) {
	*value = 1;
    } else if (strcasecmp(token, "0")==0) {
	*value = 0;
    } else {
	ret = -1;
    }

    return ret;
}

static int getBool(char *key, int *value)
{
    gchar *token;
    int ret;

    ret = getString(key, &token);
    if (ret || *token == '\0') return -1;

    ret = toBool(token, value);
    if (ret) {
	parser_comment(-1, "PSConfig: '%s(%s)' cannot convert string '%s' to"
		       " boolean\n", psconfigobj, key, token);
    }
    g_free(token);
    return ret;
}

static int toNumber(char *token, int *val)
{
    char *end;
    int num;

    if (!token || *token == '\0') return -1;

    num = (int)strtol(token, &end, 0);
    if (*end != '\0') {
	return -1;
    }
    *val = num;

    return 0;
}

static int getNumber(char *key, int *val)
{
    gchar *token;
    int ret;

    ret = getString(key, &token);
    if (ret || *token == '\0') return -1;

    ret = toNumber(token, val);
    if (ret) {
	parser_comment(-1, "PSConfig: '%s(%s)' cannot convert string '%s' to"
		       " number\n", psconfigobj, key, token);
    }
    g_free(token);
    return ret;
}

static int doForList(char *key, int (*action)(char *))
{
    GPtrArray *list;
    GError *err = NULL;
    guint i;
    int ret;

    list = psconfig_getList(psconfig, psconfigobj, key, psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(list, key, err, -1);

    if (list->len == 0) {
	parser_comment(-1, "PSConfig: '%s(%s)' does not exist or is empty"
		       " list\n", psconfigobj, key);
	return -1;
    }

    for(i = 0; i < list->len; i++) {
	ret = action((char*)g_ptr_array_index(list,i));
	if (ret) break;
    }
    g_ptr_array_free(list, TRUE);

    return ret;
}

/* ---------------------- Stuff for ressource limits ----------------------- */

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
	    if (PSIDscripts_setMax(value) < 0) {
		parser_exit(errno, "%s: Failed to adapt PSIDscripts", __func__);
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

static suprlimit_t supported_rlimits[] = {
    { "Psid.ResourceLimits.CpuTime", RLIMIT_CPU, 0 },
    { "Psid.ResourceLimits.DataSize", RLIMIT_DATA, 1 },
    { "Psid.ResourceLimits.StackSize", RLIMIT_STACK, 1 },
    { "Psid.ResourceLimits.RsSize", RLIMIT_RSS, 0 },
    { "Psid.ResourceLimits.MemLock", RLIMIT_MEMLOCK, 1 },
    { "Psid.ResourceLimits.Core", RLIMIT_CORE, 1 },
    { "Psid.ResourceLimits.NoFile", RLIMIT_NOFILE, 0 },
    { NULL, 0, 0 }
};

static int getRLimit(char *pointer)
{
    gchar *limit;
    rlim_t value;
    int i, intval, ret = 0;

    for (i=0; supported_rlimits[i].key != NULL; i++) {
	ret = getString(supported_rlimits[i].key, &limit);
	if (ret) continue; /* no not break on errors here */

	if (*limit == '\0') {
	    /* limit not set */
	    g_free(limit);
	    continue;
	}

	if (strcasecmp(limit,"infinity") == 0
	    || strcasecmp(limit, "unlimited") == 0) {
	    value = RLIM_INFINITY;
	    parser_comment(PARSER_LOG_RES, "got 'RLIM_INFINITY' for '%s'\n",
			   supported_rlimits[i].key);
	} else {
	    ret = toNumber(limit, &intval);
	    if (ret) {
		g_free(limit);
		break;
	    }
	    value = intval;
	    parser_comment(PARSER_LOG_RES, "got '%d' for '%s'\n",
			   intval, supported_rlimits[i].key);
	}

	if (supported_rlimits[i].mult) {
	    setLimit(supported_rlimits[i].resource,
		     (value == RLIM_INFINITY) ? value : value*1024);
	} else {
	    setLimit(supported_rlimits[i].resource, value);
	}
	g_free(limit);
    }

    return ret;
}

/*----------------------------------------------------------------------*/

/*
 * Worker routines to set various variables from psconfig
 */
static int getInstDir(char *key)
{
    gchar *dname;
    struct stat fstat;

    if (getString(key, &dname)) return -1;

    /* test if dir is a valid directory */
    if (*dname == '\0') {
	parser_comment(-1, "directory name is empty\n");
	g_free(dname);
	return -1;
    }

    if (stat(dname, &fstat)) {
	parser_comment(-1, "%s: %s\n", dname, strerror(errno));
	g_free(dname);
	return -1;
    }

    if (!S_ISDIR(fstat.st_mode)) {
	parser_comment(-1, "'%s' is not a directory\n", dname);
	g_free(dname);
	return -1;
    }

    if (strcmp(dname, PSC_lookupInstalldir(dname))) {
	parser_comment(-1, "'%s' seems to be no valid installdir\n", dname);
	g_free(dname);
	return -1;
    }

    g_free(dname);
    return 0;
}

static int getCoreDir(char *key)
{
    static char *usedDir = NULL;
    char *dname;
    struct stat fstat;

    if (getString(key, &dname)) return -1;

    /* test if dir is a valid directory */
    if (*dname == '\0') {
	parser_comment(-1, "directory name is empty\n");
	g_free(dname);
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
    if (usedDir) free(usedDir);
    usedDir = config.coreDir;
    setLimit(RLIMIT_CORE, RLIM_INFINITY);

    parser_comment(PARSER_LOG_RES, "set coreDir to '%s'\n", dname);

    return 0;
}

static int getMCastUse(char *key)
{
    int mc, ret;

    ret = getBool(key, &mc);

    if (ret) return ret;

    if (mc) {
	config.useMCast = 1;
	parser_comment(-1,
		       "will use MCast. Disable alternative status control\n");
    }

    return 0;
}

static int getMCastGroup(char *key)
{
    return getNumber(key, &config.MCastGroup);
}

static int getMCastPort(char *key)
{
    return getNumber(key, &config.MCastPort);
}

static int getRDPPort(char *key)
{
    return getNumber(key, &config.RDPPort);
}

static int getRDPTimeout(char *key)
{
    int tmp;
    int ret;

    ret = getNumber(key, &tmp);
    if (ret) return ret;

    if (tmp < MIN_TIMEOUT_MSEC) {
	parser_comment(-1, "RDP timeout %d too small. Ignoring...\n", tmp);
    } else {
	config.RDPTimeout = tmp;
    }

    return ret;
}

static int getRDPMaxRetrans(char *key)
{
    int tmp;
    int ret;

    ret = getNumber(key, &tmp);
    if (ret) return ret;

    setMaxRetransRDP(tmp);

    return ret;
}

static int getRDPResendTimeout(char *key)
{
    int tmp;
    int ret;

    ret = getNumber(key, &tmp);
    if (ret) return ret;

    setRsndTmOutRDP(tmp);

    return ret;
}

static int getRDPClosedTimeout(char *key)
{
    int tmp;
    int ret;

    ret = getNumber(key, &tmp);
    if (ret) return ret;

    setClsdTmOutRDP(tmp);

    return ret;
}

static int getRDPMaxACKPend(char *key)
{
    int tmp;
    int ret;

    ret = getNumber(key, &tmp);
    if (ret) return ret;

    setMaxAckPendRDP(tmp);

    return ret;
}

static int getRDPStatistics(char *key)
{
    int ret, tmp;

    ret = getBool(key, &tmp);

    if (ret) return ret;

    RDP_setStatistics(tmp);

    return ret;
}

static int getSelectTime(char *key)
{
    int temp;
    int ret;

    ret = getNumber(key, &temp);
    if (ret) return ret;

    config.selectTime = temp;

    return ret;
}

static int getDeadInterval(char *key)
{
    int temp;
    int ret;

    ret = getNumber(key, &temp);
    if (ret) return ret;

    config.deadInterval = temp;

    return ret;
}

static int getStatTmout(char *key)
{
    int temp;
    int ret;

    ret = getNumber(key, &temp);
    if (ret) return ret;

    if (temp < MIN_TIMEOUT_MSEC) {
	parser_comment(-1, "status timeout %d too small. Ignoring...\n", temp);
    } else {
	config.statusTimeout = temp;
    }

    return ret;
}

static int getStatBcast(char *key)
{
    int temp;
    int ret;

    ret = getNumber(key, &temp);
    if (ret) return ret;

    if (temp < 0) {
	parser_comment(-1, "status broadcasts must be positive. Ignoring...\n");
    } else {
	config.statusBroadcasts = temp;
    }

    return ret;
}

static int getDeadLmt(char *key)
{
    int temp;
    int ret;

    ret = getNumber(key, &temp);
    if (ret) return ret;

    config.deadLimit = temp;

    return ret;
}

static int getAcctPollInterval(char *key)
{
    int temp;
    int ret;

    ret = getNumber(key, &temp);
    if (ret) return ret;

    config.acctPollInterval = temp;

    return ret;
}

static int getKillDelay(char *key)
{
    int temp;
    int ret;

    ret = getNumber(key, &temp);
    if (ret) return ret;

    config.killDelay = temp;

    return ret;
}

static int getLogMask(char *key)
{
    return getNumber(key, &config.logMask);
}

static int getLogDest(char *key)
{
    int ret;
    gchar *value;

    if (getString(key, &value)) return -1;

    if (*value == '\0') {
	parser_comment(-1, "empty destination\n");
	g_free(value);
	return -1;
    }

    ret = 0;

    if (!strcasecmp(value, "LOG_DAEMON")) {
	config.logDest = LOG_DAEMON;
    }
    else if (strcasecmp(value, "LOG_KERN")) {
	config.logDest = LOG_KERN;
    }
    else if (strcasecmp(value, "LOG_LOCAL0")) {
	config.logDest = LOG_LOCAL0;
    }
    else if (strcasecmp(value, "LOG_LOCAL1")) {
	config.logDest = LOG_LOCAL1;
    }
    else if (strcasecmp(value, "LOG_LOCAL2")) {
	config.logDest = LOG_LOCAL2;
    }
    else if (strcasecmp(value, "LOG_LOCAL3")) {
	config.logDest = LOG_LOCAL3;
    }
    else if (strcasecmp(value, "LOG_LOCAL4")) {
	config.logDest = LOG_LOCAL4;
    }
    else if (strcasecmp(value, "LOG_LOCAL5")) {
	config.logDest = LOG_LOCAL5;
    }
    else if (strcasecmp(value, "LOG_LOCAL6")) {
	config.logDest = LOG_LOCAL6;
    }
    else if (strcasecmp(value, "LOG_LOCAL7")) {
	config.logDest = LOG_LOCAL7;
    }
    else {
	ret = toNumber(value, &config.logDest);
    }

    g_free(value);
    return ret;
}

static int getFreeOnSusp(char *key)
{
    int fs, ret;

    ret = getBool(key, &fs);

    if (ret) return ret;

    if (fs) {
	config.freeOnSuspend = 1;
	parser_comment(-1, "suspended jobs will free their resources\n");
    }

    return 0;
}

static int getPSINodesSort(char *key)
{
    gchar *value;
    int ret;

    if (getString(key, &value)) return -1;

    ret = 0;

    if (!strcasecmp(value, "load1") || !strcasecmp(value, "load_1")) {
	config.nodesSort = PART_SORT_LOAD_1;
    }
    else if (!strcasecmp(value, "load5") || !strcasecmp(value, "load_5")) {
	config.nodesSort = PART_SORT_LOAD_5;
    }
    else if (!strcasecmp(value, "load15") || !strcasecmp(value, "load_15")) {
	config.nodesSort = PART_SORT_LOAD_15;
    }
    else if (!strcasecmp(value, "proc")) {
	config.nodesSort = PART_SORT_PROC;
    }
    else if (!strcasecmp(value, "proc+load")) {
	config.nodesSort = PART_SORT_PROCLOAD;
    }
    else if (!strcasecmp(value, "none")) {
	config.nodesSort = PART_SORT_NONE;
    }
    else {
	parser_comment(-1, "Unknown sorting strategy: '%s'\n", value);
	ret = -1;
    }

    g_free(value);

    if (!ret) {
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

static unsigned int hwtype;

static int setHWType(const unsigned int hw)
{
    nodeconf.hwtype |= hw;
    parser_comment(PARSER_LOG_NODE, " HW '%s'\n", HW_printType(hw));

    return 0;
}

static int getHWnone(char *token)
{
    return setHWType(0);
}

static int getHWent(char *token)
{
    int idx = HW_index(token);

    if (idx < 0) {
	parser_comment(-1, "Hardware type '%s' not available.\n", token);
	return -1;
    }

    hwtype |= 1<<idx;

    return 0;
}

static int getHW(char *key)
{
    GPtrArray *list;
    GError *err = NULL;
    guint i;
    int ret;

    list = psconfig_getList(psconfig, psconfigobj, key, psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(list, key, err, -1);

    if (list->len == 0) {
	ret = getHWnone("none");
    } else {
	for(i = 0; i < list->len; i++) {
	    hwtype = 0;

	    ret = getHWent((char*)g_ptr_array_index(list,i));
	    if (ret) break;

	    ret = setHWType(hwtype);
	    if (ret) break;
	}
    }
    g_ptr_array_free(list, TRUE);

    return ret;
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
    if ((int)uid >= 0) {
	struct passwd *pwd = getpwuid(uid);
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
    if ((int)gid >= 0) {
	struct group *grp = getgrgid(gid);
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

static int getSingleUser(char *user)
{
    char *actStr, *uStr;
    uid_t uid;

    if (!user) {
	parser_comment(-1, "Empty user\n");
	return -1;
    }

    setAction(&user, &actStr);

    uid = uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return -1;
    }

    uStr = userFromUID(uid);
    parser_comment(PARSER_LOG_NODE, " user '%s%s'\n", actStr, uStr);
    GUIDaction(&nodeUID, uid);

    free(uStr);
    return 0;
}

static int getUsers(char *key)
{
    return doForList(key, getSingleUser);
}

static int getSingleGroup(char *group)
{
    char *actStr, *gStr;
    gid_t gid;

    if (!group) {
	parser_comment(-1, "Empty group\n");
	return -1;
    }

    setAction(&group, &actStr);

    gid = gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return -1;
    }

    gStr = groupFromGID(gid);
    parser_comment(PARSER_LOG_NODE, " group '%s%s'\n", actStr, gStr);
    GUIDaction(&nodeGID, gid);

    free(gStr);
    return 0;
}

static int getGroups(char *key)
{
    return doForList(key, getSingleGroup);
}

static int getSingleAdminUser(char *user)
{
    char *actStr, *uStr;
    uid_t uid;

    if (!user) {
	parser_comment(-1, "Empty user\n");
	return -1;
    }

    setAction(&user, &actStr);

    uid = uidFromString(user);
    if ((int)uid < -1) {
	parser_comment(-1, "Unknown user '%s'\n", user);
	return -1;
    }

    uStr = userFromUID(uid);
    parser_comment(PARSER_LOG_NODE, " adminuser '%s%s'\n", actStr, uStr);
    GUIDaction(&nodeAdmUID, uid);

    free(uStr);
    return 0;
}

static int getAdminUsers(char *key)
{
    return doForList(key, getSingleAdminUser);
}

static int getSingleAdminGroup(char *group)
{
    char *actStr, *gStr;
    gid_t gid;

    if (!group) {
	parser_comment(-1, "Empty group\n");
	return -1;
    }

    setAction(&group, &actStr);

    gid = gidFromString(group);
    if ((int)gid < -1) {
	parser_comment(-1, "Unknown group '%s'\n", group);
	return -1;
    }

    gStr = groupFromGID(gid);
    parser_comment(PARSER_LOG_NODE, " admingroup '%s%s'\n", actStr, gStr);
    GUIDaction(&nodeAdmGID, gid);

    free(gStr);
    return 0;
}

static int getAdminGroups(char *key)
{
    return doForList(key, getSingleAdminGroup);
}

/* ---------------------------------------------------------------------- */

static int getCS(char *key)
{
    int cs, ret;

    ret = getBool(key, &cs);
    if (ret) return ret;

    nodeconf.canstart = cs;
    parser_comment(PARSER_LOG_NODE, " starting%s allowed\n",
		   cs ? "":" not");
    return 0;
}

static int getRJ(char *key)
{
    int rj, ret;

    ret = getBool(key, &rj);
    if (ret) return ret;

    nodeconf.runjobs = rj;
    parser_comment(PARSER_LOG_NODE, " jobs%s allowed\n", rj ? "":" not");

    return 0;
}

static int getProcs(char *key)
{
    gchar *procStr;
    int procs = -1;

    if (getString(key, &procStr)) return -1;

    if (toNumber(procStr, &procs) && strcasecmp(procStr, "any")) {
	parser_comment(-1, "Unknown number of processes '%s'\n", procStr);
	g_free(procStr);
	return -1;
    }
    g_free(procStr);

    nodeconf.procs = procs;
    if (procs == -1) {
	parser_comment(PARSER_LOG_NODE, " any");
    } else {
	parser_comment(PARSER_LOG_NODE, " %d", procs);
    }
    parser_comment(PARSER_LOG_NODE, " procs\n");

    return 0;
}

static int getOB(char *key)
{
    gchar *obStr;
    int ob, ret;

    if (getString(key, &obStr)) return -1;

    if (strcasecmp(obStr, "auto") == 0) {
	ob = OVERBOOK_AUTO;
	parser_comment(PARSER_LOG_NODE, "got 'auto' for value 'overbook'\n");
	ret = 0;
    } else {
	ret = toBool(obStr, &ob);
    }
    g_free(obStr);

    if (ret) return ret;

    nodeconf.overbook = ob;
    parser_comment(PARSER_LOG_NODE, " overbooking is '%s'\n",
		   ob==OVERBOOK_AUTO ? "auto" : ob ? "TRUE" : "FALSE");

    return 0;
}

static int getExcl(char *key)
{
    int excl, ret;

    ret = getBool(key, &excl);
    if (ret) return ret;

    nodeconf.exclusive = excl;
    parser_comment(PARSER_LOG_NODE, " exclusive assign%s allowed\n",
		   excl ? "":" not");

    return 0;
}

static int getPinProcs(char *key)
{
    int pinProcs, ret;

    ret = getBool(key, &pinProcs);
    if (ret) return ret;

    nodeconf.pinProcs = pinProcs;
    parser_comment(PARSER_LOG_NODE, " processes are%s pinned\n",
		   pinProcs ? "":" not");

    return 0;
}

static int getBindMem(char *key)
{
    int bindMem, ret;

    ret = getBool(key, &bindMem);
    if (ret) return ret;

    nodeconf.bindMem = bindMem;
    parser_comment(PARSER_LOG_NODE, " memory is%s bound\n",
		   bindMem ? "":" not");

    return 0;
}

static int getBindGPUs(char *key)
{
    int bindGPUs, ret;

    ret = getBool(key, &bindGPUs);
    if (ret) return ret;

    nodeconf.bindGPUs = bindGPUs;
    parser_comment(PARSER_LOG_NODE, " GPUs get%s bound\n",
		   bindGPUs ? "":" not");

    return 0;
}

static int getAllowUserMap(char *key)
{
    int allowMap, ret;

    ret = getBool(key, &allowMap);
    if (ret) return ret;

    nodeconf.allowUserMap = allowMap;
    parser_comment(PARSER_LOG_NODE, " user's CPU-mapping is%s allowed\n",
		   allowMap ? "":" not");

    return 0;
}

static int getSupplGrps(char *key)
{
    int supplGrps, ret;

    ret = getBool(key, &supplGrps);
    if (ret) return ret;

    nodeconf.supplGrps = supplGrps;
    parser_comment(PARSER_LOG_NODE, " supplementary groups are%s set\n",
		   supplGrps ? "":" not");

    return 0;
}

static int getMaxStatTry(char *key)
{
    int try, ret;

    ret = getNumber(key, &try);

    if (ret) return ret;

    nodeconf.maxStatTry = try;
    parser_comment(PARSER_LOG_NODE, " maxStatTry are '%d'\n", try);

    return 0;
}

/* ---------------------------------------------------------------------- */

static int getCPUmapEnt(char *token)
{
    int val, ret;

    ret = toNumber(token, &val);
    if (ret) return ret;

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
	return -1;
    }
    nodeconf.cpumap[nodeconf.cpumap_size] = val;
    nodeconf.cpumap_size++;

    parser_comment(PARSER_LOG_NODE, " %d", val);
    return 0;
}

static int getCPUmap(char *key)
{
    nodeconf.cpumap_size = 0;
    parser_comment(PARSER_LOG_NODE, " CPUMap {");

    int ret = doForList(key, getCPUmapEnt);

    if (!ret) parser_comment(PARSER_LOG_NODE, " }\n");

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

static int setEnv(char *var, char *val)
{
    EnvEnt_t *envent;

    /* store environment */
    envent = malloc(sizeof(*envent));
    if (!envent) {
	parser_comment(-1, "%s: No memory\n", __func__);
	return -1;
    }

    envent->name = strdup(var);
    envent->value = strdup(val);

    list_add_tail(&envent->next, &envList);

    parser_comment(PARSER_LOG_NODE, " env %s='%s'\n", var, val);

    return 0;
}

static int getEnv(char *key)
{
    GPtrArray *env;
    GError *err = NULL;
    unsigned int i;
    int ret;

    env = psconfig_getList(psconfig, psconfigobj, key, psconfig_flags, &err);
    CHECK_PSCONFIG_ERROR_AND_RETURN(env, key, err, -1);

    if (env->len % 2 != 0) {
	parser_comment(-1, "Invalid environment setting for hwtype '%s'\n",
		       key);
	return -1;
    }

    ret = 0;
    for(i = 0; (i+1) < env->len; i+=2) {
	gchar *var = (gchar*)g_ptr_array_index(env,i);
	gchar *val = (gchar*)g_ptr_array_index(env,i+1);

	ret = setEnv(var, val);
	if (ret) break;
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
	if (env->name) free(env->name);
	if (env->value) free(env->value);

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
	if (env->name) free(env->name);
	if (env->value) free(env->value);

	free(env);
    }
}

/*----------------------------------------------------------------------*/
static confkeylist_t each_node_configkey_list[] = {
    {"Psid.CpuMap", getCPUmap},
    {NULL, NULL}
};

/**
 * @brief Insert a node.
 *
 * Helper function to make a node known to the ParaStation daemon.
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
    if (PSIDnodes_register(id, addr)) {
	parser_comment(-1, "PSIDnodes_register(%d, <%s>) failed\n",
		       id, inet_ntoa(*(struct in_addr *)&addr));
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


    // get parameters this node
    int allret = 0;
    for (size_t i = 0; each_node_configkey_list[i].key != NULL; i++) {
	parser_comment(PARSER_LOG_VERB, "%s: processing config key '%s'\n",
		       __func__, each_node_configkey_list[i].key);
	int ret = each_node_configkey_list[i].handler(
					    each_node_configkey_list[i].key);
	if (ret) {
	    parser_comment(-1, "Processing config key '%s' for node %d"
		    " failed\n", each_node_configkey_list[i].key, id);
	}
	allret = ret ? 1 : allret;
    }
    if (allret) return -1;

    /* set cpu map for new node */
    if (nodeconf.cpumap_size) {
	size_t c;
	for (c = 0; c < nodeconf.cpumap_size; c++) {
	    if (PSIDnodes_appendCPUMap(id, nodeconf.cpumap[c])) {
		parser_comment(-1, "PSIDnodes_appendCPUMap(%d, %d) failed\n",
			       id, nodeconf.cpumap[c]);
		return -1;
	    }
	}
    }

    return 0;

}
/* ---------------------------------------------------------------------- */

/*
    - works on node those obj is currently in psconfigobj
    - as a side effect set id of local node in nodeconf struct
*/
static int insertNode(void)
{
    gchar *nodename, *netname, *ipaddress;
    char buffer[64];
    struct in_addr *tmpaddr;
    in_addr_t ipaddr;
    int nodeid, ret;

    // ignore this host object silently if NodeName is not set
    if (getString("NodeName", &nodename)) return 0;
    if (*nodename == '\0') {
	g_free(nodename);
	return 0;
    }

    // get parameters for the node
    if (getString("Psid.NetworkName", &netname)) {
	g_free(nodename);
	return -1;
    }

    if (*netname == '\0') {
	parser_comment(-1, "empty network name for node '%s'\n", nodename);
	g_free(netname);
	g_free(nodename);
	return -1;
    }

    snprintf(buffer, sizeof(buffer), "%s.DevIPAddress", netname);
    g_free(netname);
    if (getString(buffer, &ipaddress)) {
	g_free(nodename);
	return -1;
    }

    if (*ipaddress == '\0') {
	parser_comment(-1, "empty value of '%s' for node '%s'\n", buffer,
		nodename);
	g_free(nodename);
	return -1;
    }

    tmpaddr = g_malloc(sizeof(struct in_addr));
    if (!inet_pton(AF_INET, ipaddress, tmpaddr)) {
	parser_comment(-1, "Cannot convert IP address '%s' for node '%s'\n",
		ipaddress, nodename);
	g_free(tmpaddr);
	return -1;
    }
    ipaddr = tmpaddr->s_addr;
    g_free(tmpaddr);

    ret = getNumber("Psid.NodeId", &nodeid);
    if (ret == -1) {
	ret = getNumber("NodeNo", &nodeid);
	if (ret == -1) {
	    parser_comment(-1, "Psid.NodeId is not set for node '%s'\n",
		    nodename);
	    return -1;
	}
	parser_comment(-1, "Using NodeNo for node '%s'. NodeNo is deprecated"
		" and support will be removed. Use Psid.NodeId instead.\n",
		nodename);
    }
    if (ret) return ret;

    parser_comment(PARSER_LOG_NODE, "Register '%s' as %d\n", nodename, nodeid);
    g_free(nodename);

    ret = newHost(nodeid, ipaddr);
    if (ret) return ret;

    if (PSC_isLocalIP(ipaddr)) {
	nodeconf.id = nodeid;
	PSC_setMyID(nodeid);
    } else {
	clearEnv();
    }

    return ret;
}

static int getNodes(char *psiddomain)
{
    GPtrArray *nodeobjlist;
    GError *err = NULL;

    gchar *parents[] = { "class:host", NULL };
    GHashTable* keyvals = g_hash_table_new(g_str_hash,g_str_equal);
    unsigned int i;

    nodeobjlist = psconfig_getObjectList(psconfig, "host:*", parents, keyvals,
					 psconfig_flags, &err);
    g_hash_table_unref(keyvals);
    if (nodeobjlist == NULL) {
	parser_comment(-1, "PSConfig: getObjectList(host:*): %s\n",
		       (err)->message);
	g_error_free(err);
	return -1;
    }

    char *psconfigobj_old = psconfigobj;

    int ret = 0;
    for(i = 0; i < nodeobjlist->len; i++) {
	psconfigobj = (gchar*)g_ptr_array_index(nodeobjlist,i);

	/* check psiddomain if set */
	if (psiddomain) {
	    char *domain;
	    /* ignore errors */
	    if (getString("Psid.Domain", &domain)) continue;
	    /* ignore nodes with wrong psid domain */
	    if (strcmp(domain, psiddomain)) {
		g_free(domain);
		continue;
	    }
	    g_free(domain);
	}

	ret = insertNode();
	if (ret) break;
    }
    g_ptr_array_free(nodeobjlist, TRUE);

    psconfigobj = psconfigobj_old;

    parser_comment(PARSER_LOG_NODE, "%d nodes registered\n", nodesfound);

    if (PSC_getMyID() == -1) {
	parser_comment(-1, "PSConfig-Error: Local node not configured.\n"
		" The host object for this node needs to contain a valid"
		" NodeName\n"
		" and <Psid.NetworkName>.DevIPAddress matching a local IP"
		" address.\n");
    }

    return ret;
}

/* ---------------------------------------------------------------------- */

static int actHW = -1;

static int setHardwareScript(char *type, char *value)
{
    char *name;

    if (strcasecmp(type, "startscript")==0) {
	name = HW_STARTER;
    } else if (strcasecmp(type, "stopscript")==0) {
	name = HW_STOPPER;
    } else if (strcasecmp(type, "setupscript")==0) {
	name = HW_SETUP;
    } else if (strcasecmp(type, "headerscript")==0) {
	name = HW_HEADERLINE;
    } else if (strcasecmp(type, "statusscript")==0) {
	name = HW_COUNTER;
    } else {
	parser_comment(-1, "unknown script type '%s'\n", type);
	return -1;
    }

    /* store script */
    if (HW_getScript(actHW, name)) {
	parser_comment(-1, "redefineing hardware script: %s\n", name);
    }
    HW_setScript(actHW, name, value);

    parser_comment(PARSER_LOG_RES, "got hardware script: %s='%s'\n",
		   name, value);

    return 0;
}

static int setHardwareEnv(char *key, char *value)
{
    /* store environment */
    if (HW_getEnv(actHW, key)) {
	parser_comment(-1, "redefineing hardware environment: %s\n", key);
    }
    HW_setEnv(actHW, key, value);

    parser_comment(PARSER_LOG_RES, "got hardware environment: %s='%s'\n",
		   key, value);

    return 0;
}

static char* hwtype_scripts[] = {
    "StartScript",
    "StopScript",
    "SetupScript",
    "HeaderScript",
    "StatusScript",
    NULL
};

static int getHardwareOptions(char *name)
{
    int objlen = strlen(name) + 12, env_config_style;
    gchar obj[objlen], *key, *val;
    GPtrArray *env;
    GError *err = NULL;
    guint i;
    int ret;

    ret = g_snprintf(obj, objlen, "psidhwtype:%s", name);
    if (ret < 0 || ret >= objlen) return -1;

    for (i = 0; hwtype_scripts[i] != NULL; i++) {
	key = (gchar*)hwtype_scripts[i];
	val = psconfig_get(psconfig, obj, key, psconfig_flags, &err);

	if (val == NULL) {
	    if (g_error_matches(err, PSCONFIG_ERROR,
				PSCONFIG_ERROR_OBJNOTEXIST)) {
		// it's perfectly ok to have hwtypes without options
		g_error_free(err);
		return 0;
	    } else {
		parser_comment(-1, "Failed to get %s of hwtype '%s': %s\n", key,
			       name, err->message);
		g_error_free(err);
		return -1;
	    }
	}

	ret = setHardwareScript(key, val);
	g_free(val);
	if (ret) return ret;
    }

    env = psconfig_getList(psconfig, obj, "Environment", psconfig_flags, &err);

    // it's fine to have hwtype options w/o environment
    if (env == NULL) return 0;

    // holds the detected config style
    // 0 means config style yet undetected
    // 1 means new config style having "key=value" list elements
    // 2 means old config style having key, value alternating list entries
    env_config_style = 0;

    for(i = 0; (i+1) < env->len; i++) {
	key = (gchar*)g_ptr_array_index(env,i);
	val = strstr(key, "=");

	if (val == NULL) {
	    if (env_config_style != 0) {
		parser_comment(-1, "Invalid environment setting for hwtype"
			       " '%s'\n", name);
		break;
	    }
	    env_config_style = 2; // old style detected
	    break;
	}

	env_config_style = 1; // new style detected

	*val = '\0';
	val = val+1;

	ret = setHardwareEnv(key, val);
	if (ret) break;
    }

    if (env_config_style == 2) {

	// warn about old style config
	parser_comment(-1, "Old style environment config used in hwtype '%s'."
		       " You should update your configuration.\n", name);

	if (env->len % 2 != 0) {
	    parser_comment(-1, "Invalid environment setting for hwtype '%s'\n",
			   name);
	    g_ptr_array_free(env, TRUE);
	    return 0;
	}

	for(i = 0; (i+1) < env->len; i+=2) {
	    key = (gchar*)g_ptr_array_index(env,i);
	    val = (gchar*)g_ptr_array_index(env,i+1);

	    ret = setHardwareEnv(key, val);
	    if (ret) break;
	}
    }

    g_ptr_array_free(env, TRUE);

    return ret;
}

static int getHardware(char *name)
{
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

    return getHardwareOptions(name);
}

static int getHardwareList(char *key)
{
    return doForList(key, getHardware);
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

static int getPlugins(char *key)
{
    return doForList(key, getPluginEnt);
}

/* ---------------------------------------------------------------------- */

static int getDaemonScript(char *key)
{
    char *value;
    char *sname;

    if (strcmp(key, "Psid.StartupScript") == 0) {
	sname = "startupScript";
    }
    else if (strcmp(key, "Psid.NodeUpScript") == 0) {
	sname = "nodeUpScript";
    }
    else if (strcmp(key, "Psid.NodeDownScript") == 0) {
	sname = "nodeDownScript";
    }
    else {
	return -1;
    }

    if (getString(key, &value)) return -1;

    if (*value == '\0') {
	// script not set
	return 0;
    }

    if (PSID_registerScript(&config, sname, value)) {
	parser_comment(-1, "failed to register script '%s' to type '%s'\n",
		       value, sname);
	return -1;
    }

    return 0;
}

/* ---------------------------------------------------------------------- */

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
    {"Psid.RdpStatusTimeout", getStatTmout},
    {"Psid.RdpStatusBroadcasts", getStatBcast},
    {"Psid.RdpStatusDeadLimit", getDeadLmt},
    {"Psid.AccountPollInterval", getAcctPollInterval},
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
 * @return On success, 0 is returned. Or -1, if any error occurred.
 */
static int setupLocalNode(void)
{
    int i, allret;

    // get parameters for local node
    allret = 0;
    for (i = 0; local_node_configkey_list[i].key != NULL; i++) {
	parser_comment(PARSER_LOG_VERB, "%s: processing config key '%s'\n",
		       __func__, local_node_configkey_list[i].key);
	int ret = local_node_configkey_list[i].handler(
					    local_node_configkey_list[i].key);
	if (ret) {
	    parser_comment(-1, "Processing config key '%s' failed\n",
		    local_node_configkey_list[i].key);
	}
	allret = ret ? 1 : allret;
    }
    if (allret) return -1;

    // setup local node
    if (PSIDnodes_setHWType(nodeconf.id, nodeconf.hwtype)) {
	parser_comment(-1, "PSIDnodes_setHWType(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.hwtype);
	return -1;
    }

    if (PSIDnodes_setIsStarter(nodeconf.id, nodeconf.canstart)) {
	parser_comment(-1, "PSIDnodes_setIsStarter(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.canstart);
	return -1;
    }

    if (PSIDnodes_setRunJobs(nodeconf.id, nodeconf.runjobs)) {
	parser_comment(-1, "PSIDnodes_setRunJobs(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.runjobs);
	return -1;
    }

    if (PSIDnodes_setProcs(nodeconf.id, nodeconf.procs)) {
	parser_comment(-1, "PSIDnodes_setProcs(%ld, %ld) failed\n",
		       nodeconf.id, nodeconf.procs);
	return -1;
    }

    if (PSIDnodes_setOverbook(nodeconf.id, nodeconf.overbook)) {
	parser_comment(-1, "PSIDnodes_setOverbook(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.overbook);
	return -1;
    }

    if (PSIDnodes_setExclusive(nodeconf.id, nodeconf.exclusive)) {
	parser_comment(-1, "PSIDnodes_setExclusive(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.exclusive);
	return -1;
    }

    if (PSIDnodes_setPinProcs(nodeconf.id, nodeconf.pinProcs)) {
	parser_comment(-1, "PSIDnodes_setPinProcs(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.pinProcs);
	return -1;
    }

    if (PSIDnodes_setBindMem(nodeconf.id, nodeconf.bindMem)) {
	parser_comment(-1, "PSIDnodes_setBindMem(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.bindMem);
	return -1;
    }

    if (PSIDnodes_setBindGPUs(nodeconf.id, nodeconf.bindGPUs)) {
	parser_comment(-1, "PSIDnodes_setBindGPUs(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.bindGPUs);
	return -1;
    }

    if (PSIDnodes_setAllowUserMap(nodeconf.id, nodeconf.allowUserMap)) {
	parser_comment(-1, "PSIDnodes_setAllowUserMap(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.allowUserMap);
	return -1;
    }

    if (PSIDnodes_setSupplGrps(nodeconf.id, nodeconf.supplGrps)) {
	parser_comment(-1, "PSIDnodes_setSupplGrps(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.supplGrps);
	return -1;
    }

    if (PSIDnodes_setMaxStatTry(nodeconf.id, nodeconf.maxStatTry)) {
	parser_comment(-1, "PSIDnodes_setMaxStatTry(%ld, %d) failed\n",
		       nodeconf.id, nodeconf.maxStatTry);
	return -1;
    }

    // setup environment of the local node
    pushAndClearEnv();

    if (pushGUID(nodeconf.id, PSIDNODES_USER, &nodeUID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_USER, %p) failed\n",
		       nodeconf.id, &nodeUID);
	return -1;
    }

    if (pushGUID(nodeconf.id, PSIDNODES_GROUP, &nodeGID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_GROUP, %p) failed\n",
		       nodeconf.id, &nodeGID);
	return -1;
    }

    if (pushGUID(nodeconf.id, PSIDNODES_ADMUSER, &nodeAdmUID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_ADMUSER, %p) failed\n",
		       nodeconf.id, &nodeAdmUID);
	return -1;
    }

    if (pushGUID(nodeconf.id, PSIDNODES_ADMGROUP, &nodeAdmGID)) {
	parser_comment(-1, "pushGUID(%ld, PSIDNODES_ADMGROUP, %p) failed\n",
		       nodeconf.id, &nodeAdmGID);
	return -1;
    }

    return 0;
}

config_t *parseConfig(FILE* logfile, int logmask, char *configfile)
{
    int ret;

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

    // open psconfig database
    psconfig = psconfig_new();
    psconfigobj = NULL;

    // generate local psconfig host object name
    psconfigobj = malloc(70*sizeof(char));
    strncpy(psconfigobj, "host:", 6);
    gethostname(psconfigobj+5, 65);
    psconfigobj[69] = '\0'; //assure object to be null terminated

    // check if the host object exists or we have to cut the hostname
    char *nodename;
    if (getString("NodeName", &nodename)) {
	// cut hostname
	parser_comment(PARSER_LOG_VERB, "%s: Cutting hostname \"%s\" for"
		" psconfig.\n", __func__, psconfigobj+5);
	char *pos = strchr(psconfigobj, '.');
	if (pos == NULL) {
	    parser_comment(-1,
			   "ERROR: Cannot find host object for this node.\n");
	    goto parseConfig_error;
	}
	*pos = '\0';
	parser_comment(-1,
		       "INFO: Trying to use cutted hostname for psconfig"
		       " host object: \"%s\".\n", psconfigobj);
    }
    else {
	free(nodename);
    }

    // get local psid domain
    char *psiddomain;
    if (getString("Psid.Domain", &psiddomain)) {
	parser_comment(-1,
		       "INFO: No psid domain configured, using all host"
		       " objects.\n");
    }

    // get hostname to ID mapping
    ret = getNodes(psiddomain);
    g_free(psiddomain);
    if (ret) {
	parser_comment(-1,
		       "ERROR: Reading nodes configuration from psconfig"
		       " failed.\n");
	psconfig_unref(psconfig);
	psconfig = NULL;
	return NULL;
    }

    // set default UID/GID for local node
    setID(&nodeUID, PSNODES_ANYUSER);
    setID(&nodeGID, PSNODES_ANYGROUP);
    setID(&nodeAdmUID, 0);
    setID(&nodeAdmGID, 0);

    // read the configuration for the local node
    ret = setupLocalNode();

    if (ret) {
	parser_comment(-1,
		       "ERROR: Reading configuration from psconfig failed.\n");
	free(psconfigobj);
	psconfigobj = NULL;
	psconfig_unref(psconfig);
	psconfig = NULL;
	return NULL;
    }

    /*
     * Sanity Checks
     */
    if (PSIDnodes_getNum() == -1) {
	parser_comment(-1, "ERROR: No Nodes found.\n");
	free(psconfigobj);
	psconfigobj = NULL;
	psconfig_unref(psconfig);
	psconfig = NULL;
	return NULL;
    }

    if (nodeconf.cpumap) {
	free(nodeconf.cpumap);
	nodeconf.cpumap = NULL;
	nodeconf.cpumap_maxsize = 0;
    }

    parser_finalize(); //TODO

    free(psconfigobj);
    psconfigobj = NULL;
    psconfig_unref(psconfig);
    psconfig = NULL;

    return &config;

parseConfig_error:
    free(psconfigobj);
    psconfigobj = NULL;
    psconfig_unref(psconfig);
    psconfig = NULL;
    return NULL;
}
#endif /* BUILD_WITHOUT_PSCONFIG */
