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
#include "psslurmconfig.h"

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <time.h>

#include "pscommon.h"
#include "pshostlist.h"

#include "pluginmalloc.h"
#include "pluginstrv.h"
#include "psidhw.h"

#include "psslurmlog.h"
#include "psslurmgres.h"
#include "psslurmtopo.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

/** psslurm plugin configuration list */
Config_t Config;

/** Slurm configuration list */
Config_t SlurmConfig;

/** Slurm cgroup configuration list */
Config_t SlurmCgroupConfig;

/** Hash value of the current Slurm configuration */
static uint32_t configHash = -1;

/** Time of Slurm configuration's last update */
static time_t configUpdateTime;

/** configuraton type */
typedef enum {
    CONFIG_TYPE_DEFAULT,
    CONFIG_TYPE_GRES,
    CONFIG_TYPE_TOPOLOGY
} config_type_t;

/** used to forward information to host visitor */
typedef struct {
    int count;		    /**< number of hosts parsed */
    char *options;	    /**< host options */
    config_type_t type;     /**< configuration type */
    bool result;	    /**< parsing result */
    bool useNodeAddr;	    /**< use NodeAddr option */
    int localHostIdx;	    /**< index of local host in host-list */
} Host_Info_t;

/** psslurm default configuration values */
const ConfDef_t confDef[] =
{
    { "SLURM_CONFIG_DIR", 0,
	"path",
	"/etc/slurm",
	"Path to the directory holding all Slurm configuration files" },
    { "SLURM_CONF", 0,
	"file",
	"slurm.conf",
	"Configuration file of Slurm" },
    { "SLURM_CONF_SERVER", 0,
	"ip[:port]",
	"none",
	"slurmctld to fetch configuration files from" },
    { "SLURM_CONF_BACKUP_SERVER", 0,
	"ip[:port]",
	"none",
	"slurmctld backup to fetch configuration files from" },
    { "SLURM_GRES_CONF", 0,
	"file",
	"gres.conf",
	"Gres configuration file of Slurm" },
    { "SLURM_SPANK_CONF", 0,
	"file",
	"plugstack.conf",
	"Default spank configuration file of Slurm" },
    { "SLURM_GATHER_CONF", 0,
	"file",
	"acct_gather.conf",
	"Default account gather configuration file of Slurm" },
    { "SLURM_TOPOLOGY_CONF", 0,
	"file",
	"topology.conf",
	"Default fabric topology configuration file of Slurm" },
    { "SLURM_CGROUP_CONF", 0,
	"file",
	"cgroup.conf",
	"Default cgroup configuration file of Slurm" },
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
    { "SRUN_BINARY", 0,
	"string",
	"/usr/bin/srun",
	"Absolute path to srun binary mainly used for spawning processes" },
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
    { "SLURM_CONF_CACHE", 0,
        "path",
        SPOOL_DIR "/slurm_conf",
        "Slurm config cache directory. Used to save Slurm configuration"
            " files in config-less mode" },
    { "SLURM_UPDATE_CONF_AT_STARTUP", 1,
	"bool",
	"1",
	"Always update Slurm configuration at startup in config-less mode" },
    { "CWD_PATTERN", 1,
	"bool",
	"1",
	"Apply filename patterns on job/steps current working directory" },
    { "SLURM_HC_TIMEOUT", 1,
	"int",
	"60",
	"Timeout in seconds for a Slurm health-check script" },
    { "SLURM_HC_STARTUP", 1,
	"bool",
	"1",
	"Execute Slurm health-check on psslurm startup" },
    { NULL, 0, NULL, NULL, NULL },
};

/** cgroup.conf default configuration values */
const ConfDef_t cgroupDef[] =
{
    { "ConstrainCores", 0,
	"string",
	"no",
	"constrain the jobs CPU cores" },
    { "ConstrainDevices", 0,
	"string",
	"no",
	"constrain the jobs GRES devices" },
    { "ConstrainKmemSpace", 0,
	"string",
	"no",
	"constrain the jobs kmem RAM usage" },
    { "ConstrainRAMSpace", 0,
	"string",
	"no",
	"constrain the jobs RAM usage" },
    { "ConstrainSwapSpace", 0,
	"string",
	"no",
	"constrain the jobs swap RAM usage" },
    { "CgroupPlugin", 0,
	"string",
	"ncgroup/v1",
	"version of cgroup subsystem to use" },
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
    if (!options) return false;

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
    if (!options) return false;

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
	} else if (!strncasecmp(next, "Files=", 6)) {
	    gres->file = ustrdup(next+6);
	} else if (!strncasecmp(next, "CPUs=", 5)) {
	    gres->cpus = ustrdup(next+5);
	} else if (!strncasecmp(next, "Cores=", 6)) {
	    gres->cores = ustrdup(next+6);
	} else if (!strncasecmp(next, "Type=", 5)) {
	    gres->type = ustrdup(next+5);
	} else if (!strncasecmp(next, "Flags=", 6)) {
	    gres->strFlags = ustrdup(next+6);
	} else if (!strncasecmp(next, "Link=", 5)) {
	    gres->links = ustrdup(next+5);
	} else if (!strncasecmp(next, "Links=", 6)) {
	    gres->links = ustrdup(next+6);
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
 * @brief Parse and add options to the topology configuration
 *
 * @param options The current topology options to add
 *
 * @param return Returns true on success or false on error
 */
static bool parseTopologyOptions(char *options)
{
    if (!options) return false;

    char *toksave, *next;
    const char delimiters[] =" \t\n";
    Topology_Conf_t *topo = ucalloc(sizeof(*topo));

    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!strncasecmp(next, "SwitchName=", 11)) {
	    topo->switchname = ustrdup(next+11);
	} else if (!strncasecmp(next, "Switches=", 9)) {
	    topo->switches = ustrdup(next+9);
	} else if (!strncasecmp(next, "Nodes=", 6)) {
	    topo->nodes = ustrdup(next+6);
	} else if (!strncasecmp(next, "LinkSpeed=", 10)) {
	    topo->linkspeed = ustrdup(next+10);
	} else {
	    flog("unknown topology option '%s'\n", next);
	    ufree(topo);
	    return false;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    topo = saveTopologyConf(topo);
    if (!topo) flog("saving topology configuration failed\n");

    return topo ? true : false;
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
	    if (PSC_isLocalIP(saddr->sin_addr.s_addr)) {
		freeaddrinfo(result);
		return true;
	    }
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

    if (hInfo->type == CONFIG_TYPE_GRES) {
	char *myHost = getConfValueC(&Config, "SLURM_HOSTNAME");
	if (myHost && !strcmp(myHost, host) && hInfo->options) {
	    res = parseGresOptions(hInfo->options);
	}
    } else {
	if (!strcmp(host, "DEFAULT")) {
	    flog("saved default host definition\n");
	    if (hInfo->options) {
		res = addHostOptions(hInfo->options);
	    }
	} else if (isLocalAddr(host)) {
	    flog("local addr: %s args: %s\n", host, hInfo->options);
	    if (!hInfo->useNodeAddr) {
		addConfigEntry(&Config, "SLURM_HOSTNAME", host);
	    }
	    hInfo->localHostIdx = hInfo->count;
	    if (hInfo->options) {
		res = addHostOptions(hInfo->options);
	    }
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
 * @param hostopt Optional host options to parse
 *
 * @param nodeAddr Optional node address
 *
 * @param type configurarion type
 *
 * @param return Returns true on success or false on error
 */
static bool setMyHostDef(char *hosts, char *hostopt, char *nodeAddr,
			 config_type_t type)
{
    Host_Info_t hInfo = {
	.count = 0,
	.options = hostopt,
	.type = type,
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
    if (!hostopt) return NULL;

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
 * @param type Configuration type
 *
 * @param return Returns true on success or false on error
 */
static bool parseNodeNameEntry(char *line, config_type_t type)
{
    char *hostopt = strchr(line, ' ');
    char *nodeAddr = NULL;
    if (hostopt) {
	hostopt[0] = '\0';
	hostopt++;
	nodeAddr = findNodeAddr(hostopt);
    }

    /* save all host definitions from default config except the default host */
    if (type == CONFIG_TYPE_DEFAULT && strcmp(line, "DEFAULT")) {
	saveNodeNameEntry(line, nodeAddr);
    }

    /* search for definition of the current host */
    bool res = setMyHostDef(line, hostopt, nodeAddr, type);

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

static void parseSlurmdParam(char *param)
{
    if (!param) return;

    char *toksave, *next;
    const char delimiters[] =" \t\n";

    next = strtok_r(param, delimiters, &toksave);
    while (next) {
	if (!strcasecmp(next, "config_overrides")) {
	    addConfigEntry(&Config, "SLURMD_CONF_OVERRIDES", "true");
	}
	if (!strcasecmp(next, "l3cache_as_socket")) {
	    addConfigEntry(&Config, "SLURMD_L3CACHE_AS_SOCK", "true");
	}
	if (!strcasecmp(next, "shutdown_on_reboot")) {
	    addConfigEntry(&Config, "SLURMD_SHUTDOWN_ON_REBOOT", "true");
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }
}

static void parseSlurmAccFreq(char *param)
{
    char *toksave, *next;
    const char delimiters[] =" \t\n,";

    next = strtok_r(param, delimiters, &toksave);
    while (next) {
	if (!strncasecmp(next, "network=", 8)) {
	    addConfigEntry(&Config, "SLURM_ACC_NETWORK", next+8);
	}
	if (!strncasecmp(next, "task=", 5)) {
	    addConfigEntry(&Config, "SLURM_ACC_TASK", next+5);
	}
	if (!strncasecmp(next, "energy=", 7)) {
	    addConfigEntry(&Config, "SLURM_ACC_ENERGY", next+7);
	}
	if (!strncasecmp(next, "filesystem=", 11)) {
	    addConfigEntry(&Config, "SLURM_ACC_FILESYSTEM", next+11);
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }
}

/**
 * @brief Parse a Slurm configuration pair
 *
 * @param key The key to parse
 *
 * @param value The value to parse
 *
 * @param info The type of the config to parse
 *
 * @param return Returns false on success or true on error
 */
static bool parseSlurmConf(char *key, char *value, const void *info)
{
    const config_type_t *type = info;
    switch(*type) {
    case CONFIG_TYPE_DEFAULT:
	if (!strcmp(key, "NodeName")) {
	    char *hostline = ustrdup(value);
	    if (!parseNodeNameEntry(hostline, *type)) {
		ufree(hostline);
		return true; /* an error occurred, true stops parsing */
	    }
	    ufree(hostline);
	} else if (!strcmp(key, "SlurmctldHost")) {
	    if (!saveCtldHost(value)) {
		return true; /* an error occurred, true stops parsing */
	    }
	} else if (!strcmp(key, "SlurmdParameters")) {
	    parseSlurmdParam(value);
	} else if (!strcmp(key, "JobAcctGatherFrequency")) {
	    parseSlurmAccFreq(value);
	}
	break;
    case CONFIG_TYPE_GRES:
	if (!strcmp(key, "NodeName")) {
	    char *hostline = ustrdup(value);
	    if (!parseNodeNameEntry(hostline, *type)) {
		ufree(hostline);
		return true; /* an error occurred, true stops parsing */
	    }
	    ufree(hostline);
	} else if (!strcmp(key, "Name")) {
	    char *tmp = umalloc(strlen(value) + 6);
	    snprintf(tmp, strlen(value) + 6, "Name=%s", value);
	    //mlog("%s: Gres single name '%s'\n", __func__, tmp);
	    parseGresOptions(tmp);
	    ufree(tmp);
	}
	break;
    case CONFIG_TYPE_TOPOLOGY:
	if (!strcmp(key, "SwitchName")) {
	    char *tmp = umalloc(strlen(value) + 12);
	    snprintf(tmp, strlen(value) + 12, "SwitchName=%s", value);
	    parseTopologyOptions(tmp);
	    ufree(tmp);
	}
	break;
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

/* forward declaration */
static bool parseSlurmPlugLine(char *key, char *value, const void *info);

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

/**
 * @brief Parse a Slurm plugstack configuration line
 *
 * @param key The key of the line to parse
 *
 * @param value The value of the line to parse
 *
 * @return Returns true on error to stop further parsing
 * and false otherwise
 */
static bool parseSlurmPlugLine(char *key, char *value, const void *info)
{
    const char delimiters[] =" \t\n";

    if (!key) {
	flog("no key provided\n");
	return true; /* an error occurred, return true to stop parsing */
    }

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
    if (arg1 && value) {
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

bool confHasOpt(Config_t *conf, char *key, char *option)
{
    if (!conf || !key || !option) {
	flog("called with empty parameters\n");
	return false;
    }

    char *value = getConfValueC(conf, key);
    if (!value || value[0] == '\0') return false;

    char *toksave, *next, *dup = ustrdup(value);
    const char delimiters[] =" ,";
    bool res = false;

    next = strtok_r(dup, delimiters, &toksave);
    while (next) {
	if (!strcasecmp(next, option)) {
	    res = true;
	    break;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    ufree(dup);
    return res;
}

/**
 * @brief Verify psslurm supports loaded Slurm plugins in slurm.conf
 *
 * @return Returns true on success otherwise false is returned
 **/
static bool verifySlurmPlugins(void)
{
    /* AcctGatherInterconnectType */
    char *val = getConfValueC(&SlurmConfig, "AcctGatherInterconnectType");
    if (val) {
	if (!strcasecmp(val, "acct_gather_interconnect/ofed")) {
	    fdbg(PSSLURM_LOG_SPLUGIN, "gather interconnect: ofed\n");
	} else if (!strcasecmp(val, "acct_gather_interconnect/none")) {
	    fdbg(PSSLURM_LOG_SPLUGIN, "gather interconnect: none\n");
	} else {
	    flog("unsupported AcctGatherInterconnectType %s in "
		 "slurm.conf\n", val);
	    return false;
	}
    }

    /* AcctGatherFilesystemType */
    val = getConfValueC(&SlurmConfig, "AcctGatherFilesystemType");
    if (val) {
	if (!strcasecmp(val, "acct_gather_filesystem/none")) {
	    fdbg(PSSLURM_LOG_SPLUGIN, "gather filesystem: none\n");
	} else if (!strcasecmp(val, "acct_gather_filesystem/lustre")) {
	    fdbg(PSSLURM_LOG_SPLUGIN, "gather filesystem: lustre\n");
	} else {
	    flog("unsupported AcctGatherFilesystemType %s in "
		 "slurm.conf\n", val);
	    return false;
	}
    }

    /* AcctGatherEnergyType */
    val = getConfValueC(&SlurmConfig, "AcctGatherEnergyType");
    if (val) {
	if (!strcasecmp(val, "acct_gather_energy/none")) {
	    fdbg(PSSLURM_LOG_SPLUGIN, "gather energy: none\n");
	} else if (!strcasecmp(val, "acct_gather_energy/rapl")) {
	    fdbg(PSSLURM_LOG_SPLUGIN, "gather energy: rapl\n");
	} else if (!strcasecmp(val, "acct_gather_energy/ipmi")) {
	    fdbg(PSSLURM_LOG_SPLUGIN, "gather energy: ipmi\n");
	} else {
	    flog("unsupported AcctGatherEnergyType %s in "
		 "slurm.conf\n", val);
	    return false;
	}
    }

    return true;
}

/**
 * @brief Do various sanity checks for a Slurm configuration
 *
 * @return Returns true on success or false on error
 */
static bool verifySlurmConf(void)
{
    /* ensure mandatory prologue is configured */
    char *prologue = getConfValueC(&SlurmConfig, "Prolog");
    if (!prologue || prologue[0] == '\0') {

	prologue = getConfValueC(&SlurmConfig, "PrologSlurmctld");
	if (!prologue || prologue[0] == '\0') {
	    flog("error: PrologSlurmctld is mandatory for psslurm\n");
	    /*
	    flog("error: Neither Prolog nor PrologSlurmctld is set "
		 "in slurm.conf. A prolog is mandatory for psslurm\n");
	    */
	    return false;
	}
    } else {
	/* disable slurmd prologue for now, remove to activate it later */
	flog("error: please use the PrologSlurmctld and disable Prolog in"
	     " slurm.conf\n");
	return false;

	/* ensure the prologue is run at job allocation */
	/*
	if (!confHasOpt(&SlurmConfig, "PrologFlags", "Alloc")) {
	    flog("error: option PrologFlags has Alloc not set in slurm.conf\n");
	    return false;
	}
	*/
    }

    if (!getConfValueC(&Config, "SLURM_HOSTNAME")) {
	flog("could not find my host address in slurm.conf\n");
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

/**
 * @brief Parse a Slurm cgroup configuration line
 *
 * @param key The key of the line to parse
 *
 * @param value The value of the line to parse
 *
 * @return Returns true on error to stop further parsing
 * and false otherwise
 */
static bool verifyCgroupConf(char *key, char *value, const void *info)
{
    if (!strcasecmp(key, "AllowedKmemSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup AllowedKmemSpace=%s\n", value);
    } else if (!strcasecmp(key, "ConstrainKmemSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup ConstrainKmemSpace=%s\n", value);
    } else if (!strcasecmp(key, "MaxKmemPercent")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup MaxKmemPercent=%s\n", value);
    } else if (!strcasecmp(key, "MinKmemSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup MinKmemSpace=%s\n", value);
    } else if (!strcasecmp(key, "AllowedRAMSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup AllowedRAMSpace=%s\n", value);
    } else if (!strcasecmp(key, "ConstrainRAMSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup ConstrainRAMSpace=%s\n", value);
    } else if (!strcasecmp(key, "MaxRAMPercent")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup MaxRAMPercent=%s\n", value);
    } else if (!strcasecmp(key, "MinRAMSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup MinRAMSpace=%s\n", value);
    } else if (!strcasecmp(key, "AllowedSwapSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup AllowedSwapSpace=%s\n", value);
    } else if (!strcasecmp(key, "ConstrainSwapSpace")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup ConstrainSwapSpace=%s\n", value);
    } else if (!strcasecmp(key, "MaxSwapPercent")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup MaxSwapPercent=%s\n", value);
    } else if (!strcasecmp(key, "MemorySwappiness")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup MemorySwappiness=%s\n", value);
    } else if (!strcasecmp(key, "ConstrainCores")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup ConstrainCores=%s\n", value);
    } else if (!strcasecmp(key, "ConstrainDevices")) {
	fdbg(PSSLURM_LOG_JAIL, "cgroup ConstrainDevices=%s\n", value);
    } else if (!strcasecmp(key, "CgroupPlugin")) {
	if (!strcasecmp(value, "cgroup/v2")) {
	    flog("error: CgroupPlugin has to be cgroup/v1\n");
	    /* parsing failed */
	    return true;
	}
    } else if (!strcasecmp(key, "CgroupAutomount")) {
	flog("warning: ignoring unsupported option CgroupAutomount=%s\n",
	     value);
    } else if (!strcasecmp(key, "CgroupMountpoint")) {
	flog("warning: ignoring unsupported option CgroupMountpoint=%s\n",
	     value);
    } else if (!strcasecmp(key, "IgnoreSystemd")) {
	flog("warning: ignoring unsupported option IgnoreSystemd=%s\n",
	     value);
    } else if (!strcasecmp(key, "IgnoreSystemdOnFailure")) {
	flog("warning: ignoring unsupported option IgnoreSystemdOnFailure=%s\n",
	     value);
    } else {
	fdbg(PSSLURM_LOG_WARN, "warning: ignoring unsupported cgroup "
	     "configuration %s\n", key);
    }

    /* parsing was successful, continue with next line */
    return false;
}

static bool parseAcctGatherConf(char *key, char *value, const void *info)
{
    if (!strcasecmp(key, "InfinibandOFEDPort")) {
	addConfigEntry(&SlurmConfig, "INFINIBAND_OFED_PORT", value);
    } else if (!strcasecmp(key, "EnergyIPMIFrequency")) {
	addConfigEntry(&SlurmConfig, "IPMI_FREQUENCY", value);
    } else if (!strcasecmp(key, "EnergyIPMICalcAdjustment")) {
	addConfigEntry(&SlurmConfig, "IPMI_ADJUSTMENT", value);
    } else if (!strcasecmp(key, "EnergyIPMIPowerSensors")) {
	addConfigEntry(&SlurmConfig, "IPMI_POWER_SENSORS", value);
    } else if (!strcasecmp(key, "EnergyIPMIUsername")) {
	addConfigEntry(&SlurmConfig, "IPMI_USERNAME", value);
    } else if (!strcasecmp(key, "EnergyIPMIPassword")) {
	addConfigEntry(&SlurmConfig, "IPMI_PASSWORD", value);
    }

    /* parsing was successful, continue with next line */
    return false;
}

bool parseSlurmConfigFiles(void)
{
    struct stat sbuf;
    config_type_t type = CONFIG_TYPE_DEFAULT;
    char cPath[PATH_MAX];

    char *confDir = getConfValueC(&Config, "SLURM_CONFIG_DIR");
    if (!confDir) {
	flog("Configuration value SLURM_CONFIG_DIR not found\n");
	return false;
    }

    /* parse Slurm config file */
    char *confFile = getConfValueC(&Config, "SLURM_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    registerConfigHashAccumulator(&configHash);
    configHash = 0;
    if (parseConfigFile(cPath, &SlurmConfig, true /*trimQuotes*/) < 0) {
	flog("Parsing Slurm configuration file %s failed\n", cPath);
	return false;
    }
    configUpdateTime = time(NULL);
    registerConfigHashAccumulator(NULL);

    if (traverseConfig(&SlurmConfig, parseSlurmConf, &type)) {
	flog("Traversing Slurm configuration failed\n");
	return false;
    }
    if (!verifySlurmConf()) return false;
    if (!verifySlurmPlugins()) return false;

    /* parse optional Slurm GRes config file */
    type = CONFIG_TYPE_GRES;
    confFile = getConfValueC(&Config, "SLURM_GRES_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_GRES_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    if (stat(cPath, &sbuf) != -1) {
	Config_t SlurmGresTmp;
	if (parseConfigFile(cPath, &SlurmGresTmp, true /*trimQuotes*/) < 0) {
	    flog("Parsing GRes configuration file %s failed\n", cPath);
	    return false;
	}

	if (traverseConfig(&SlurmGresTmp, parseSlurmConf, &type)) {
	    flog("Traversing GRes configuration failed\n");
	    return false;
	}
	freeConfig(&SlurmGresTmp);
    }

    /* parse optional Slurm account gather config file */
    confFile = getConfValueC(&Config, "SLURM_GATHER_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_GATHER_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    if (stat(cPath, &sbuf) != -1) {
	Config_t AcctGather;
	if (parseConfigFile(cPath, &AcctGather, true /*trimQuotes*/) < 0) {
	    flog("Parsing account gather configuration file %s failed\n",
		 cPath);
	    return false;
	}

	if (traverseConfig(&AcctGather, parseAcctGatherConf, NULL)) {
	    flog("Traversing account gather configuration failed\n");
	    return false;
	}
	freeConfig(&AcctGather);
    }

    /* parse optional Slurm topology config file */
    type = CONFIG_TYPE_TOPOLOGY;
    confFile = getConfValueC(&Config, "SLURM_TOPOLOGY_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_TOPOLOGY_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    if (stat(cPath, &sbuf) != -1) {
	Config_t SlurmTopoTmp;
	if (parseConfigFile(cPath, &SlurmTopoTmp, true /*trimQuotes*/) < 0) {
	    flog("Parsing topology configuration file %s failed\n", cPath);
	    return false;
	}

	if (traverseConfig(&SlurmTopoTmp, parseSlurmConf, &type)) {
	    flog("Traversing topology configuration failed\n");
	    return false;
	}
	freeConfig(&SlurmTopoTmp);
    }

    /* parse optional Slurm cgroup config file */
    confFile = getConfValueC(&Config, "SLURM_CGROUP_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_CGROUP_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);
    initConfig(&SlurmCgroupConfig);

    if (stat(cPath, &sbuf) != -1) {
	if (parseConfigFile(cPath, &SlurmCgroupConfig,
	    true /*trimQuotes*/) < 0) {
	    flog("Parsing cgroup configuration file %s failed\n", cPath);
	    return false;
	}

	setConfigDefaults(&SlurmCgroupConfig, cgroupDef);
	if (traverseConfig(&SlurmCgroupConfig, verifyCgroupConf, NULL)) {
	    flog("Traversing cgroup configuration failed\n");
	    return false;
	}
    }

#ifdef HAVE_SPANK
    Config_t SlurmPlugConf;

    /* parse optional plugstack.conf holding spank plugins */
    confFile = getConfValueC(&SlurmConfig, "PlugStackConfig");
    if (!confFile) {
	confFile = getConfValueC(&Config, "SLURM_SPANK_CONF");
	if (!confFile) {
	    flog("Configuration value SLURM_SPANK_CONF not found\n");
	    return false;
	}
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    int disabled = getConfValueU(&Config, "DISABLE_SPANK");
    if (!disabled && stat(cPath, &sbuf) != -1) {
	if (parseConfigFile(cPath, &SlurmPlugConf, true/*trimQuotes*/) < 0) {
	    flog("Parsing Spank configuration file %s failed\n", cPath);
	    return false;
	}

	if (traverseConfig(&SlurmPlugConf, parseSlurmPlugLine, NULL)) {
	    flog("Traversing Spank configuration failed\n");
	    return false;
	}
	freeConfig(&SlurmPlugConf);
    }
#endif

    return true;
}

int initPSSlurmConfig(char *filename)
{
    struct stat sbuf;

    initConfig(&Config);

    /* parse psslurm config file */
    if (stat(filename, &sbuf) != -1) {
	if (parseConfigFile(filename, &Config, false /*trimQuotes*/) < 0) {
	    flog("parsing '%s' failed\n", filename);
	    return CONFIG_ERROR;
	}
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
    if (!parseSlurmConfigFiles()) return CONFIG_ERROR;

    return CONFIG_SUCCESS;
}

bool updateSlurmConf(void)
{
    char cPath[PATH_MAX];

    char *confDir = getConfValueC(&Config, "SLURM_CONFIG_DIR");
    if (!confDir) {
	flog("Configuration value SLURM_CONFIG_DIR not found\n");
	return false;
    }

    char *confFile = getConfValueC(&Config, "SLURM_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    /* dry run to parse new configuration */
    Config_t newConf;
    registerConfigHashAccumulator(NULL);
    if (parseConfigFile(cPath, &newConf, true /*trimQuotes*/) < 0) {
	flog("Parsing updated Slurm configuration file %s failed\n", cPath);
	return false;
    }
    freeConfig(&newConf);

    /* update the original configuration now */
    freeConfig(&SlurmConfig);
    registerConfigHashAccumulator(&configHash);
    configHash = 0;
    if (parseConfigFile(cPath, &SlurmConfig, true /*trimQuotes*/) < 0) {
	flog("Parsing updated Slurm configuration file %s failed\n", cPath);
	return false;
    }
    configUpdateTime = time(NULL);
    registerConfigHashAccumulator(NULL);

    /* parse optional Slurm GRes config file */
    confFile = getConfValueC(&Config, "SLURM_GRES_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_GRES_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    struct stat sbuf;
    if (stat(cPath, &sbuf) != -1) {
	Config_t SlurmGresTmp;
	if (parseConfigFile(cPath, &SlurmGresTmp, true /*trimQuotes*/) < 0) {
	    flog("Parsing GRes configuration file %s failed\n", cPath);
	    return false;
	}

	/* remove old GRes configuration and rebuild it */
	clearGresConf();
	config_type_t type = CONFIG_TYPE_GRES;
	if (traverseConfig(&SlurmGresTmp, parseSlurmConf, &type)) {
	    flog("Traversing GRes configuration failed\n");
	    return false;
	}
	freeConfig(&SlurmGresTmp);
    }

    /* parse optional Slurm topology config file */
    confFile = getConfValueC(&Config, "SLURM_TOPOLOGY_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_TOPOLOGY_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    if (stat(cPath, &sbuf) != -1) {
	Config_t SlurmTopoTmp;
	if (parseConfigFile(cPath, &SlurmTopoTmp, true /*trimQuotes*/) < 0) {
	    flog("Parsing topology configuration file %s failed\n", cPath);
	    return false;
	}

	/* remove old topology configuration and rebuild it */
	clearTopologyConf();
	config_type_t type = CONFIG_TYPE_TOPOLOGY;
	if (traverseConfig(&SlurmTopoTmp, parseSlurmConf, &type)) {
	    flog("Traversing topology configuration failed\n");
	    return false;
	}
	freeConfig(&SlurmTopoTmp);
    }

    /* parse optional Slurm cgroup config file */
    confFile = getConfValueC(&Config, "SLURM_CGROUP_CONF");
    if (!confFile) {
	flog("Configuration value SLURM_CGROUP_CONF not found\n");
	return false;
    }
    snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);

    initConfig(&SlurmCgroupConfig);
    if (stat(cPath, &sbuf) != -1) {
	Config_t SlurmCgrpTmp;
	/* dry run to parse the new cgroup config */
	if (parseConfigFile(cPath, &SlurmCgrpTmp, true /*trimQuotes*/) < 0) {
	    flog("Parsing cgroup configuration file %s failed\n", cPath);
	    return false;
	}
	setConfigDefaults(&SlurmCgrpTmp, cgroupDef);
	if (traverseConfig(&SlurmCgrpTmp, verifyCgroupConf, NULL)) {
	    flog("Traversing cgroup configuration failed\n");
	    return false;
	}
	freeConfig(&SlurmCgrpTmp);

	/* remove old cgroup configuration and rebuild it */
	freeConfig(&SlurmCgroupConfig);
	if (parseConfigFile(cPath, &SlurmCgroupConfig,
	    true /*trimQuotes*/) < 0) {
	    flog("Parsing cgroup configuration file %s failed\n", cPath);
	    return false;
	}
	setConfigDefaults(&SlurmCgroupConfig, cgroupDef);
    }

    return true;
}

uint32_t getSlurmConfHash(void)
{
    return configHash;
}

char *getSlurmUpdateTime(void)
{
    static char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S",
	     localtime(&configUpdateTime));

    return timeStr;
}
