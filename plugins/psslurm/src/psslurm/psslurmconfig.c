/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>

#include "pshostlist.h"

#include "pluginmalloc.h"
#include "psidhw.h"

#include "psslurmlog.h"
#include "psslurmgres.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

#include "psslurmconfig.h"

/** The psslurm plugin configuration list. */
Config_t Config;

/** The Slurm configuration list. */
Config_t SlurmConfig;

/** The Slurm GRes configuration list. */
Config_t SlurmGresConfig;

/** used to forward information to host visitor */
typedef struct {
    int count;		/**< number of hosts parsed */
    char *options;	/**< host options */
    int gres;		/**< GRes host definition flag */
    bool result;	/**< parsing result */
    bool useNodeAddr;	/**< use NodeAddr option */
    int localHostIdx;	/**< index of local host in host-list */
} Host_Info_t;

const ConfDef_t confDef[] =
{
    { "SLURM_CONF", 0,
	"file",
	"/etc/slurm/slurm.conf",
	"Configuration file of Slurm" },
    { "SLURM_CONF_SERVER", 0,
	"ip:port",
	"none",
	"slurmctld to fetch configuration files from" },
    { "SLURM_CONF_BACKUP_SERVER", 0,
	"ip:port",
	"none",
	"slurmctld backup to fetch configuration files from" },
    { "SLURM_GRES_CONF", 0,
	"file",
	"/etc/slurm/gres.conf",
	"Gres configuration file of slurm" },
    { "SLURM_SPANK_CONF", 0,
	"file",
	"/etc/slurm/plugstack.conf",
	"Default spank configuration file of slurm" },
    { "DIR_SCRIPTS", 0,
	"path",
	SPOOL_DIR "/scripts",
	"Directory to search for prologue/epilogue scripts" },
    { "DIR_JOB_FILES", 0,
	"path",
	SPOOL_DIR "/jobs",
	"Directory to store job-scripts" },
    { "TIMEOUT_PROLOGUE", 1,
	"sec",
	"300",
	"Number of seconds to allow the prologue scripts to run" },
    { "TIMEOUT_EPILOGUE", 1,
	"sec",
	"300",
	"Number of seconds to allow the epilogue scripts to run" },
    { "TIMEOUT_PE_GRACE", 1,
	"sec",
	"60",
	"Number of seconds until the local PE-logue timeout will be enforced" },
    { "TIMEOUT_CHILD_CONNECT", 1,
	"sec",
	"10",
	"Number of seconds until a child must connect to the mother"
	    " superior" },
    { "OFFLINE_PELOGUE_TIMEOUT", 1,
	"bool",
	"1",
	"Set my node offline if a pro/epilogue script timed out" },
    { "TIMEOUT_SCRIPT", 0,
	"string",
	NULL,
	"Script which is called when a prologue/epilogue timeout occurs" },
    { "ENFORCE_BATCH_START", 1,
	"bool",
	"1",
	"Enforce jobs to use the batch-system, only admin user may use mpiexec "
	    "directly" },
    { "PELOGUE_ENV_FILTER", 0,
	"list",
	"SLURM_*,_PSSLURM_*",
	"Positive filter which will allow forwarding of selected "
	"environment variables to prologue/epilogue." },
    { "PELOGUE_LOG_OE", 1,
	"bool",
	"0",
	"Log stdout/stderr of epilogue" },
    { "PELOGUE_LOG_PATH", 0,
	"path",
	"/dev/shm/",
	"Path to write stdout and stderr logs of a prologue/epilogue "
	"scripts." },
    { "RLIMITS_SOFT", 0,
	"list",
	NULL,
	"Set soft resource limits for user processes" },
    { "RLIMITS_HARD", 0,
	"list",
	NULL,
	"Set hard resource limits for user processes" },
    { "DIST_START", 1,
	"bool",
	"0",
	"Distribute mpiexec service processes at startup" },
    { "MEMBIND_DEFAULT", 0,
	"string",
	"local",
	"Default value to be used for memory binding (none|local)" },
    { "MALLOC_CHECK", 1,
	"bool",
	"0",
	"Enable libc malloc checking" },
    { "DEBUG_MASK", 1,
	"int",
	"0",
	"Set the psslurm debug mask" },
    { "PLUGIN_DEBUG_MASK", 1,
	"int",
	"0",
	"Set the plugin library debug mask" },
    { "DISABLE_CONFIG_HASH", 1,
	"bool",
	"0",
	"Disable transmission of SLURM config hash" },
    { "ENABLE_FPE_EXCEPTION", 1,
	"bool",
	"0",
	"Enable libc FPE exception traps" },
    { "RECONNECT_MAX_RETRIES", 1,
	"int",
	"360",
	"TCP connection retries for Slurm communication" },
    { "RECONNECT_TIME", 1,
	"int",
	"60",
	"Time between reconnection retries for Slurm messages" },
    { "RESEND_TIMEOUT", 1,
	"int",
	"300",
	"Timeout in seconds for resending a Slurm message" },
    { "SLURM_PROTO_VERSION", 0,
	"string",
	"auto",
	"Slurm protocol version as string or auto (e.g. 17.11)" },
    { "MEASURE_MUNGE", 1,
	"bool",
	"0",
	"Measure execution times of libmunge calls" },
    { "MEASURE_RPC", 1,
	"bool",
	"0",
	"Measure execution times of RPC calls" },
    { "MAX_TERM_REQUESTS", 1,
	"int",
	"10",
	"Number of maximum terminate requests for an allocation" },
    { "SINFO_BINARY", 0,
	"string",
	"/usr/bin/sinfo",
	"Path to the sinfo binary used for automatic protocol detection" },
    { "DISABLE_SPANK", 1,
	"bool",
	"0",
	"If true no spank plugins will be loaded" },
    { "DEFAULT_CPU_BIND_TYPE", 0,
	"string",
	"threads",
	"Default cpu-bind type used for pinning"
	    " (none|rank|threads|cores|sockets)" },
    { "DEFAULT_SOCKET_DIST", 0,
	"string",
	"cyclic",
	"Default to use as distribution over sockets"
	    " (cyclic|block|fcyclic)" },
    { "DEFAULT_CORE_DIST", 0,
	"string",
	"inherit",
	"Default to use as distribution over sockets"
	    " (inherit|block|cyclic|fcyclic)" },
    { "DIRECT_DRAIN", 0,
	"bool",
	"0",
	"If true nodes will be drained without the help of psexec" },
    { "SLURM_RUN_DIR", 0,
	"path",
	"/run/slurm",
	"The Slurm /run directory. Used to link to Slurm configuration" },
    { "SLURM_CONF_DIR", 0,
	"path",
	SPOOL_DIR "/slurm_conf",
	"The Slurm config directory. Used to save Slurm configuration files" },
    { "SLURM_UPDATE_CONF_AT_STARTUP", 1,
	"bool",
	"1",
	"Always update Slurm configuration at startup in config-less mode" },
    { "CWD_PATTERN", 1,
	"bool",
	"1",
	"Apply filename patterns on job/steps current working directory" },
    { NULL, 0, NULL, NULL, NULL },
};

/**
 * @brief Add current host options to psslurm configuration
 *
 * @param options The current host options to add
 *
 * @param return Returns true on success or false on error
 */
static bool addHostOptions(char *options)
{
    char *toksave, *next;
    const char delimiters[] =" \t\n";

    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!strncasecmp(next, "Sockets=", 8)) {
	    addConfigEntry(&Config, "SLURM_SOCKETS", next+8);
	} else if (!strncasecmp(next, "CoresPerSocket=", 15)) {
	    addConfigEntry(&Config, "SLURM_CORES_PER_SOCKET", next+15);
	} else if (!strncasecmp(next, "ThreadsPerCore=", 15)) {
	    addConfigEntry(&Config, "SLURM_THREADS_PER_CORE", next+15);
	} else if (!strncasecmp(next, "CPUs=", 5)) {
	    addConfigEntry(&Config, "SLURM_CPUS", next+5);
	} else if (!strncasecmp(next, "Feature=", 8)) {
	    addConfigEntry(&Config, "SLURM_FEATURE", next+8);
	} else if (!strncasecmp(next, "Features=", 9)) {
	    addConfigEntry(&Config, "SLURM_FEATURE", next+8);
	} else if (!strncasecmp(next, "Gres=", 5)) {
	    addConfigEntry(&Config, "SLURM_GRES", next+5);
	} else if (!strncasecmp(next, "State=", 6)) {
	    addConfigEntry(&Config, "SLURM_STATE", next+6);
	} else if (!strncasecmp(next, "Procs=", 6)) {
	    addConfigEntry(&Config, "SLURM_PROCS", next+6);
	} else if (!strncasecmp(next, "Weight=", 7)) {
	    addConfigEntry(&Config, "SLURM_WEIGHT", next+7);
	} else if (!strncasecmp(next, "RealMemory=", 11)) {
	    addConfigEntry(&Config, "SLURM_REAL_MEMORY", next+11);
	} else if (!strncasecmp(next, "Boards=", 7)) {
	    addConfigEntry(&Config, "SLURM_BOARDS", next+7);
	} else if (!strncasecmp(next, "NodeAddr=", 9)) {
	    /* already set before */
	} else {
	    mlog("%s: unknown node option '%s'\n", __func__, next);
	    return false;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }
    return true;
}

/**
 * @brief Parse and add gres options of current host to psslurm configuration
 *
 * @param options The current gres options to add
 *
 * @param return Returns true on success or false on error
 */
static bool parseGresOptions(char *options)
{
    char *toksave, *next, *count = NULL;
    const char delimiters[] =" \t\n";
    Gres_Conf_t *gres = ucalloc(sizeof(*gres));

    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!strncasecmp(next, "Name=", 5)) {
	    gres->name = ustrdup(next+5);
	} else if (!strncasecmp(next, "Count=", 6)) {
	    count = ustrdup(next+6);
	} else if (!strncasecmp(next, "File=", 5)) {
	    gres->file = ustrdup(next+5);
	} else if (!strncasecmp(next, "CPUs=", 5)) {
	    gres->cpus = ustrdup(next+5);
	} else if (!strncasecmp(next, "Cores=", 6)) {
	    gres->cores = ustrdup(next+6);
	} else if (!strncasecmp(next, "Type=", 5)) {
	    gres->type = ustrdup(next+5);
	} else if (!strncasecmp(next, "Flags=", 6)) {
	    gres->flags = ustrdup(next+6);
	} else {
	    flog("unknown gres option '%s'\n", next);
	    return false;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    gres = saveGresConf(gres, count);
    if (!gres) flog("saving GRES configuration failed\n");

    ufree(count);
    return gres ? true : false;
}

/**
 * @brief Test if an IP address is local
 *
 * @param addr The address to test
 *
 * @return Returns true if the address is local otherwise false
 */
bool isLocalAddr(char *addr)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int rc;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(addr, NULL, &hints, &result);
    if (rc) {
	mlog("%s: unknown address %s: %s\n", __func__, addr, gai_strerror(rc));
	return false;
    }

    /* try each address returned by getaddrinfo() */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
	struct sockaddr_in *saddr;
	switch (rp->ai_family) {
	case AF_INET:
	    saddr = (struct sockaddr_in *)rp->ai_addr;
	    if (PSC_isLocalIP(saddr->sin_addr.s_addr)) return true;
	    break;
	case AF_INET6:
	    /* ignore -- don't handle IPv6 yet */
	    break;
	}
    }
    freeaddrinfo(result);

    return false;
}

/**
 * @brief Parse a single host
 *
 * @param host The host to parse
 *
 * @param info Pointer to Host_Info_t structure
 *
 * @return Always returns true
 */
static bool parseHost(char *host, void *info)
{
    Host_Info_t *hInfo = info;
    bool res = true;

    hInfo->count++;

    if (hInfo->gres) {
	char *myHost = getConfValueC(&Config, "SLURM_HOSTNAME");
	if (myHost && !strcmp(myHost, host)) {
	    res = parseGresOptions(hInfo->options);
	}
    } else {
	if (!strcmp(host, "DEFAULT")) {
	    flog("saved default host definition\n");
	    res = addHostOptions(hInfo->options);
	} else if (isLocalAddr(host)) {
	    flog("local addr: %s args: %s\n", host, hInfo->options);
	    if (!hInfo->useNodeAddr) {
		addConfigEntry(&Config, "SLURM_HOSTNAME", host);
	    }
	    hInfo->localHostIdx = hInfo->count;
	    res = addHostOptions(hInfo->options);
	}
    }

    if (!res) hInfo->result = false;

    return true;
}

/**
 * @brief Find local host in hostlist
 *
 * @param host The host to test
 *
 * @param info Pointer to Host_Info_t structure
 *
 * @return Returns false if the host was found otherwise
 * true
 */
static bool findMyHost(char *host, void *info)
{
    static int count = 1;
    Host_Info_t *hInfo = info;

    if (count++ == hInfo->localHostIdx) {
	addConfigEntry(&Config, "SLURM_HOSTNAME", host);
	return false;
    }
    return true;
}

/**
 * @brief Search and save the current host definition
 *
 * @param hosts The host range to parse
 *
 * @param hostopt The host options to parse
 *
 * @param nodeAddr Optional node address
 *
 * @param gres True if the hosts are from a gres configuration
 *
 * @param return Returns true on success or false on error
 */
static bool setMyHostDef(char *hosts, char *hostopt, char *nodeAddr, int gres)
{
    Host_Info_t hInfo = {
	.count = 0,
	.options = hostopt,
	.gres = gres,
	.result = true,
	.useNodeAddr = nodeAddr ? true : false,
	.localHostIdx = -1 };

    /* call parseHost() for every host in the list */
    traverseHostList(nodeAddr ? nodeAddr : hosts, parseHost, &hInfo);

    /* set missing SLURM_HOSTNAME if nodeAddr is used */
    if (nodeAddr && hInfo.localHostIdx != -1) {
	traverseHostList(hosts, findMyHost, &hInfo);
    }

    return hInfo.result;
}

/**
 * @brief Find NodeAddr in host options
 *
 * @param hostopt Host options to search
 *
 * @return Returns the found NodeAddr or NULL otherwise
 */
static char *findNodeAddr(char *hostopt)
{
    const char delimiters[] =" \t\n";
    char *toksave, *next, *nodeAddr = NULL, *res = NULL;
    char *options = ustrdup(hostopt);

    /* find optional node address */
    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!strncasecmp(next, "NodeAddr=", 9)) {
	    nodeAddr = next+9;
	    break;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    if (nodeAddr) res = ustrdup(nodeAddr);
    ufree(options);

    return res;
}

/**
 * @brief Save NodeName entry in config
 *
 * @param NodeName The node names
 *
 * @param hostopt The config options of the nodes
 */
static void saveNodeNameEntry(char *NodeName, char *nodeAddr)
{
    char tmp[128];
    int numEntry;

    /* save node entry in config */
    numEntry = getConfValueI(&Config, "SLURM_HOST_ENTRY_COUNT");
    numEntry = (numEntry == -1) ? 0 : numEntry;
    numEntry++;

    snprintf(tmp, sizeof(tmp), "SLURM_HOST_ENTRY_%i", numEntry);
    addConfigEntry(&Config, tmp, NodeName);

    if (nodeAddr) {
	snprintf(tmp, sizeof(tmp), "SLURM_HOST_ADDR_%i", numEntry);
	addConfigEntry(&Config, tmp, nodeAddr);
    }

    /* update node entry count */
    snprintf(tmp, sizeof(tmp), "%i", numEntry);
    addConfigEntry(&Config, "SLURM_HOST_ENTRY_COUNT", tmp);
}

/**
 * @brief Parse a Slurm NodeName entry
 *
 * @param line NodeName line to parse
 *
 * @param gres True if the NodeName line is from a gres configuration
 *
 * @param return Returns true on success or false on error
 */
static bool parseNodeNameEntry(char *line, int gres)
{
    char *hostopt = strchr(line, ' ');
    if (!hostopt) {
	mlog("%s: invalid node definition '%s'\n", __func__, line);
	return false;
    }

    hostopt[0] = '\0';
    hostopt++;
    char *nodeAddr = findNodeAddr(hostopt);

    /* save all host definitions except for gres and the default host */
    if (!gres && strcmp(line, "DEFAULT")) {
	saveNodeNameEntry(line, nodeAddr);
    }

    /* search for definition of the current host */
    bool res = setMyHostDef(line, hostopt, nodeAddr, gres);

    ufree(nodeAddr);

    return res;
}

/**
 * @brief Save slurmctld hosts
 *
 * @param confVal The slurmctld host to save
 *
 * @return Returns true on success otherwise false
 */
static bool saveCtldHost(char *confVal)
{
    /* save node entry in config */
    int numEntry = getConfValueI(&Config, "SLURM_CTLHOST_ENTRY_COUNT");
    numEntry = (numEntry == -1) ? 0 : numEntry;

    /* separate and save host address */
    char *value = strdup(confVal);
    char *addr = strchr(value, '(');
    if (addr) {
	/* remove brackets */
	addr[0] = '\0';
	addr++;
	if (!addr) {
	    flog("parsing entry SlurmctldHost=%s failed\n", confVal);
	    ufree(value);
	    return false;
	}
	size_t len = strlen(addr);
	addr[len-1] = '\0';
    }

    char tmp[128];
    if (addr) {
	snprintf(tmp, sizeof(tmp), "SLURM_CTLHOST_ADDR_%i", numEntry);
	addConfigEntry(&Config, tmp, addr);
    }

    /* save host-name */
    snprintf(tmp, sizeof(tmp), "SLURM_CTLHOST_ENTRY_%i", numEntry);
    addConfigEntry(&Config, tmp, value);

    flog("slurmctld(%i) host=%s", numEntry, value);
    if (addr) {
	mlog(" address=%s\n", addr);
    } else {
	mlog("\n");
    }

    /* update node entry count */
    snprintf(tmp, sizeof(tmp), "%i", ++numEntry);
    addConfigEntry(&Config, "SLURM_CTLHOST_ENTRY_COUNT", tmp);

    ufree(value);
    return true;
}

/**
 * @brief Parse a Slurm configuration pair
 *
 * @param key The key to parse
 *
 * @param value The value to parse
 *
 * @param info Additional infos holding the current hostname and
 * gres flag
 *
 * @param return Returns false on success or true on error
 */
static bool parseSlurmConf(char *key, char *value, const void *info)
{
    const int *gres = info;

    /* parse all NodeName entries */
    if (!strcmp(key, "NodeName")) {
	char *hostline = ustrdup(value);
	if (!parseNodeNameEntry(hostline, *gres)) {
	    ufree(hostline);
	    return true; /* an error occurred, return true to stop parsing */
	}
	ufree(hostline);
    } else if (*gres && !strcmp(key, "Name")) {
	char *tmp = umalloc(strlen(value) +6 + 1);
	snprintf(tmp, strlen(value) +6, "Name=%s", value);
	//mlog("%s: Gres single name '%s'\n", __func__, tmp);
	parseGresOptions(tmp);
	ufree(tmp);
    } else if (!strcmp(key, "SlurmctldHost")) {
	if (!saveCtldHost(value)) {
	    return true; /* an error occurred, return true to stop parsing */
	}
    }
    /* parsing was successful, continue with next line */
    return false;
}

#ifdef HAVE_SPANK

#define DEFAULT_PLUG_DIR "/usr/lib64/slurm"

static bool findSpankAbsPath(char *relPath, char *absPath, size_t lenPath)
{
    const char delimiters[] =": ";
    char *toksave;
    char *plugDir = getConfValueC(&SlurmConfig, "PluginDir");
    if (!plugDir) plugDir = DEFAULT_PLUG_DIR;

    char *dirDup = ustrdup(plugDir);
    char *dirNext = strtok_r(dirDup, delimiters, &toksave);

    while(dirNext) {
	struct dirent *dent;
	DIR *dir = opendir(dirNext);

	if (!dir) {
	    mwarn(errno, "%s: open directory %s failed :", __func__, dirNext);
	    ufree(dirDup);
	    return false;
	}
	rewinddir(dir);

	while ((dent = readdir(dir))) {
	    if (!strcmp(relPath, dent->d_name)) {
		size_t len = strlen(dirNext);
		if (dirNext[len-1] == '/') dirNext[len-1] = '\0';
		snprintf(absPath, lenPath, "%s/%s", dirNext, relPath);
		ufree(dirDup);
		return true;
	    }
	}
	closedir(dir);

	dirNext = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(dirDup);

    flog("spank plugin %s in PluginDir %s not found\n", relPath, plugDir);
    return false;
}

/**
 * @brief Handle an include statement of the Slurm plugstack.conf
 *
 * @param path The path to include
 *
 * @return Return true on succes otherwise false is returned
 */
static bool handleSlurmPlugInc(const char *path)
{
    /* expand shell patterns  */
    glob_t pglob;
    int ret = glob(path, 0, NULL, &pglob);
    switch (ret) {
    case 0:
	break;
    case GLOB_NOSPACE:
	flog("glob(%s) failed: out of memory\n", path);
	return false;
    case GLOB_NOMATCH:
	fdbg(PSSLURM_LOG_DEBUG, "no match for %s\n", path);
	return true;
    case GLOB_ABORTED:
	fdbg(PSSLURM_LOG_WARN, "could not include %s\n", path);
	return true;
    default:
	flog("glob(%s) returns unexpected %d\n", path, ret);
	return true;
    }

    /* parse all files */
    for (size_t i=0; i<pglob.gl_pathc; i++) {
	fdbg(PSSLURM_LOG_SPANK, "parse file %s\n", pglob.gl_pathv[i]);
	Config_t SlurmPlugConf;
	if (parseConfigFile(pglob.gl_pathv[i], &SlurmPlugConf, true) < 0) {
	    flog("parsing file %s failed\n", pglob.gl_pathv[i]);
	    goto ERROR;
	}
	if (traverseConfig(&SlurmPlugConf, parseSlurmPlugLine, NULL)) {
	    flog("parsing file %s failed\n", pglob.gl_pathv[i]);
	    freeConfig(&SlurmPlugConf);
	    goto ERROR;
	};
	freeConfig(&SlurmPlugConf);
    }

    globfree(&pglob);
    return true;

ERROR:
    globfree(&pglob);
    return false;
}

bool parseSlurmPlugLine(char *key, char *value, const void *info)
{
    const char delimiters[] =" \t\n";
    char *toksave;
    Spank_Plugin_t *def = umalloc(sizeof(*def));

    /* include/optional/required flag */
    char *flag = strtok_r(key, delimiters, &toksave);
    if (!flag) {
	flog("missing flag for key: '%s'\n", key);
	goto ERROR;
    }

    if (!strcmp("include", flag)) {
	const char *path = strtok_r(NULL, delimiters, &toksave);
	if (!path) {
	    flog("missing path for include statement\n");
	    goto ERROR;
	}
	if (!handleSlurmPlugInc(path)) return true; /* break on error */
	return false; /* success, continue with next line */
    } else if (!strcmp("optional", flag)) {
	def->optional = true;
    } else if (!strcmp("required", flag)) {
	def->optional = false;
    } else {
	flog("invalid flag '%s'\n", flag);
	goto ERROR;
    }

    /* path to plugin */
    char *path = strtok_r(NULL, delimiters, &toksave);
    if (!path) {
	flog("invalid path to spank plugin '%s'\n", key);
	goto ERROR;
    }

    /* find absolute path to plugin */
    if (path[0] != '/') {
	char absPath[1024];
	if (!findSpankAbsPath(path, absPath, sizeof(absPath))) {
	    flog("path for plugin '%s' not found\n", path);
	    goto ERROR;
	}
	def->path = ustrdup(absPath);
    } else {
	def->path = ustrdup(path);
    }
    fdbg(PSSLURM_LOG_SPANK, "flag '%s' path '%s'", flag, def->path);

    /* additional arguments */
    strvInit(&def->argV, NULL, 0);

    char *arg1 = strtok_r(NULL, delimiters, &toksave);
    if (arg1) {
	char tmp[1024];
	char *val1 = strtok_r(value, delimiters, &toksave);
	snprintf(tmp, sizeof(tmp), "%s=%s", arg1, val1);

	char *args = tmp;
	while (args) {
	    strvAdd(&def->argV, ustrdup(args));
	    mdbg(PSSLURM_LOG_SPANK, " args: '%s'", args);
	    args = strtok_r(NULL, delimiters, &toksave);
	}
    }
    mdbg(PSSLURM_LOG_SPANK, "\n");

    SpankSavePlugin(def);

    /* parsing was successful, continue with next line */
    return false;

ERROR:
    if (def->optional) {
	/* plugin not needed, continue parsing */
	ufree(def);
	return false;
    }

    ufree(def);
    return true; /* an error occurred, return true to stop parsing */

}
#endif

/**
 * @brief Do various sanity checks for a Slurm configuration
 *
 * @return Returns true on success or false on error
 */
static bool verifySlurmConf()
{
    if (!getConfValueC(&Config, "SLURM_HOSTNAME")) {
	mlog("%s: could not find my host addr in slurm.conf\n", __func__);
	return false;
    }

    int boards = getConfValueI(&Config, "SLURM_BOARDS");
    if (boards == -1) {
	/* set default boards */
	addConfigEntry(&Config, "SLURM_BOARDS", "1");
	boards = 1;
    }

    int sockets = getConfValueI(&Config, "SLURM_SOCKETS");
    if (sockets == -1) {
	/* set default socket */
	addConfigEntry(&Config, "SLURM_SOCKETS", "1");
	sockets = 1;
    }

    int cores = getConfValueI(&Config, "SLURM_CORES_PER_SOCKET");
    if (cores == -1) {
	mlog("%s: invalid SLURM_CORES_PER_SOCKET\n", __func__);
	return false;
    }
    int threads = getConfValueI(&Config, "SLURM_THREADS_PER_CORE");
    if (threads == -1) {
	mlog("%s: invalid SLURM_THREADS_PER_CORE\n", __func__);
	return false;
    }

    int calcCPUs = boards * sockets * cores * threads;

    int slurmCPUs = getConfValueI(&Config, "SLURM_CPUS");
    if (slurmCPUs == -1) {
	char CPUs[64];
	snprintf(CPUs, sizeof(CPUs), "%i", calcCPUs);
	addConfigEntry(&Config, "SLURM_CPUS", CPUs);
	slurmCPUs = getConfValueI(&Config, "SLURM_CPUS");
    }
    /* verify that the Slurm configuration is consistent */
    if (calcCPUs != slurmCPUs) {
	flog("mismatching SLURM_CPUS %i calculated by "
		"sockets/threads/cores %i\n", slurmCPUs, calcCPUs);
	return false;
    }

    /* verify psslurm and psid have the same hardware view */
    if (slurmCPUs != PSIDhw_getHWthreads()) {
	flog("Slurm CPUs %i mismatching psid CPUs %i\n", slurmCPUs,
	     PSIDhw_getHWthreads());
	return false;
    }

    if (boards * sockets * cores != PSIDhw_getCores()) {
	flog("Slurm cores %i mismatching psid cores %i\n", sockets * cores,
	     PSIDhw_getCores());
	return false;
    }

    return true;
}

bool parseSlurmConfigFiles(uint32_t *hash)
{
    struct stat sbuf;
    int gres = 0;

    /* parse Slurm config file */
    char *confFile = getConfValueC(&Config, "SLURM_CONF");
    if (!confFile) return false;

    registerConfigHashAccumulator(hash);
    if (parseConfigFile(confFile, &SlurmConfig, true /*trimQuotes*/) < 0)
	return false;
    registerConfigHashAccumulator(NULL);
    if (traverseConfig(&SlurmConfig, parseSlurmConf, &gres)) return false;
    if (!verifySlurmConf()) return false;

    /* parse optional Slurm GRes config file */
    INIT_LIST_HEAD(&SlurmGresConfig);
    gres = 1;
    confFile = getConfValueC(&Config, "SLURM_GRES_CONF");
    if (!confFile) return false;
    if (stat(confFile, &sbuf) != -1) {
	Config_t SlurmGresTmp;
	if (parseConfigFile(confFile, &SlurmGresTmp, true /*trimQuotes*/) < 0)
	    return false;

	if (traverseConfig(&SlurmGresTmp, parseSlurmConf, &gres)) return false;
	freeConfig(&SlurmGresTmp);
    }

#ifdef HAVE_SPANK
    Config_t SlurmPlugConf;

    /* parse optional plugstack.conf holding spank plugins */
    confFile = getConfValueC(&SlurmConfig, "PlugStackConfig");
    if (!confFile) {
	confFile = getConfValueC(&Config, "SLURM_SPANK_CONF");
	if (!confFile) return false;
    }
    int disabled = getConfValueU(&Config, "DISABLE_SPANK");
    if (!disabled && stat(confFile, &sbuf) != -1) {
	if (parseConfigFile(confFile, &SlurmPlugConf, true /*trimQuotes*/) < 0)
	    return false;

	if (traverseConfig(&SlurmPlugConf, parseSlurmPlugLine, NULL))
	    return false;
	freeConfig(&SlurmPlugConf);
    }
#endif

    return true;
}

int initPSSlurmConfig(char *filename, uint32_t *hash)
{
    struct stat sbuf;

    /* parse psslurm config file */
    if (stat(filename, &sbuf) != -1) {
	if (parseConfigFile(filename, &Config, false /*trimQuotes*/) < 0) {
	    flog("parsing '%s' failed\n", filename);
	    return CONFIG_ERROR;
	}
    } else {
	initConfig(&Config);
    }

    setConfigDefaults(&Config, confDef);
    if (verifyConfig(&Config, confDef) != 0) {
	mlog("%s: verfiy of %s failed\n", __func__, filename);
	return CONFIG_ERROR;
    }

    /* make logging with debug mask available */
    int mask = getConfValueI(&Config, "DEBUG_MASK");
    if (mask) {
	mlog("%s: set psslurm debug mask '%i'\n", __func__, mask);
	maskLogger(mask);
    }

    char *confServer = getConfValueC(&Config, "SLURM_CONF_SERVER");
    if (confServer && strcmp(confServer, "none")) {
	/* request Slurm configuration files */
	return CONFIG_SERVER;
    }

    /* parse various Slurm configuration files if we start
     * with a local configuration */
    if (!parseSlurmConfigFiles(hash)) return CONFIG_ERROR;

    return CONFIG_SUCCESS;
}
