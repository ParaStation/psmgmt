/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
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

#include "pshostlist.h"

#include "pluginmalloc.h"

#include "psslurmlog.h"
#include "psslurmgres.h"

#include "psslurmconfig.h"

const ConfDef_t CONFIG_VALUES[] =
{
    { "SLURM_CONF", 0,
	"file",
	"/etc/slurm/slurm.conf",
	"Configuration file of slurm" },
    { "SLURM_GRES_CONF", 0,
	"file",
	"/etc/slurm/gres.conf",
	"Gres configuration file of slurm" },
    { "DIR_SCRIPTS", 0,
	"path",
	SPOOL_DIR "/scripts",
	"Directory to search for prologue/epilogue scripts" },
    { "DIR_JOB_FILES", 0,
	"path",
	SPOOL_DIR "/jobs",
	"Directory to store jobscripts" },
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
	"Enforce jobs to use the Batchsystem, only admin user may use mpiexec "
	    "directly" },
    { "PELOGUE_ENV_FILTER", 0,
	"list",
	"SLURM_*",
	"Positive filter which will allow forwarding of selected "
	"environment variables to prologue/epilogue." },
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
    { "SLURM_PROTO_VERSION", 1,
	"string",
	"17.02",
	"Slurm protocol version as string (e.g. 17.11)" },
    { "DISABLE_PROLOGUE", 1,
	"bool",
	"0",
	"Disable the execution of the parallel prologue" },
    { "DISABLE_EPILOGUE", 1,
	"bool",
	"0",
	"Disable the execution of the parallel epilogue" },
    { "WEAK_NODEID_CHECK", 1,
	"bool",
	"0",
	"Allow nodes in slurm.conf to have no PS node ID" },
    { "MEASURE_MUNGE", 1,
	"bool",
	"0",
	"Measure execution times of libmunge calls" },
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
    const char delimiters[] =" \n";

    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!(strncasecmp(next, "Sockets=", 8))) {
	    addConfigEntry(&Config, "SLURM_SOCKETS", next+8);
	} else if (!(strncasecmp(next, "CoresPerSocket=", 15))) {
	    addConfigEntry(&Config, "SLURM_CORES_PER_SOCKET", next+15);
	} else if (!(strncasecmp(next, "ThreadsPerCore=", 15))) {
	    addConfigEntry(&Config, "SLURM_THREADS_PER_CORE", next+15);
	} else if (!(strncasecmp(next, "CPUs=", 5))) {
	    addConfigEntry(&Config, "SLURM_CPUS", next+5);
	} else if (!(strncasecmp(next, "Feature=", 8))) {
	    addConfigEntry(&Config, "SLURM_FEATURE", next+8);
	} else if (!(strncasecmp(next, "Gres=", 5))) {
	    addConfigEntry(&Config, "SLURM_GRES", next+5);
	} else if (!(strncasecmp(next, "State=", 6))) {
	    addConfigEntry(&Config, "SLURM_STATE", next+6);
	} else if (!(strncasecmp(next, "Procs=", 6))) {
	    addConfigEntry(&Config, "SLURM_PROCS", next+6);
	} else if (!(strncasecmp(next, "Weight=", 7))) {
	    addConfigEntry(&Config, "SLURM_WEIGHT", next+7);
	} else if (!(strncasecmp(next, "RealMemory=", 11))) {
	    addConfigEntry(&Config, "SLURM_REAL_MEMORY", next+11);
	} else if (!(strncasecmp(next, "Boards=", 7))) {
	    addConfigEntry(&Config, "SLURM_BOARDS", next+7);
	} else if (!(strncasecmp(next, "NodeAddr=", 9))) {
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
    char *toksave, *next;
    const char delimiters[] =" \n";
    char *name, *count, *file, *cpus;

    name = count = file = cpus = NULL;
    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!(strncasecmp(next, "Name=", 5))) {
	    name = ustrdup(next+5);
	} else if (!(strncasecmp(next, "Count=", 6))) {
	    count = ustrdup(next+6);
	} else if (!(strncasecmp(next, "File=", 5))) {
	    file = ustrdup(next+5);
	} else if (!(strncasecmp(next, "CPUs=", 5))) {
	    cpus = ustrdup(next+5);
	} else {
	    mlog("%s: unknown gres option '%s'\n", __func__, next);
	    return false;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    addGresConf(name, count, file, cpus);
    ufree(name);
    ufree(count);
    ufree(file);
    ufree(cpus);
    return true;
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
	mlog("%s: unknown addr %s: %s\n", __func__, addr, gai_strerror(rc));
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
    char *hostlist, *host, *toksave;
    uint32_t numHosts;
    const char delimiters[] =", \n";
    int index = -1, count = 0;

    if (nodeAddr) {
	if (!(hostlist = expandHostList(nodeAddr, &numHosts))) {
	    mlog("%s: expanding NodeName '%s' failed\n", __func__, hosts);
	    return false;
	}
    } else {
	if (!(hostlist = expandHostList(hosts, &numHosts))) {
	    mlog("%s: expanding NodeName '%s' failed\n", __func__, hosts);
	    return false;
	}
    }

    bool res = true;
    host = strtok_r(hostlist, delimiters, &toksave);
    char *myHost = getConfValueC(&Config, "SLURM_HOSTNAME");
    while (host) {
	count++;
	if (gres) {
	    if (myHost && !strcmp(myHost, host)) {
		res = parseGresOptions(hostopt);
		break;
	    }
	} else {
	    if (!(strcmp(host, "DEFAULT"))) {
		mlog("%s: found the default host definition\n", __func__);
		res = addHostOptions(hostopt);
	    } else if (isLocalAddr(host)) {
		mlog("%s: found my addr: %s args: %s\n", __func__, host,
			hostopt);
		if (!nodeAddr) addConfigEntry(&Config, "SLURM_HOSTNAME", host);
		index = count;
		res = addHostOptions(hostopt);
		break;
	    }
	}
	host = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(hostlist);

    /* set missing SLURM_HOSTNAME if nodeAddr is used */
    if (nodeAddr && index != -1) {
	if (!(hostlist = expandHostList(hosts, &numHosts))) {
	    mlog("%s: expanding NodeName '%s' failed\n", __func__, hosts);
	    return false;
	}

	host = strtok_r(hostlist, delimiters, &toksave);
	count = 1;
	while (host) {
	    if (count++ == index) {
		addConfigEntry(&Config, "SLURM_HOSTNAME", host);
		break;
	    }
	    host = strtok_r(NULL, delimiters, &toksave);
	}
	ufree(hostlist);
    }

    return res;
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
    const char delimiters[] =" \n";
    char *toksave, *next, *nodeAddr = NULL, *res = NULL;
    char *options = ustrdup(hostopt);

    /* find optional node address */
    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!(strncasecmp(next, "NodeAddr=", 9))) {
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
    char *hostopt, *nodeAddr;
    bool res;

    if (!(hostopt = strchr(line, ' '))) {
	mlog("%s: invalid node definition '%s'\n", __func__, line);
	return false;
    }

    hostopt[0] = '\0';
    hostopt++;
    nodeAddr = findNodeAddr(hostopt);

    /* save all host definitions except for gres and the default host */
    if (!gres && !!strcmp(line, "DEFAULT")) {
	saveNodeNameEntry(line, nodeAddr);
    }

    /* search for definition of the current host */
    res = setMyHostDef(line, hostopt, nodeAddr, gres);

    ufree(nodeAddr);

    return res;
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
    char *hostline, *tmp;
    const int *gres = info;

    /* parse all NodeName entries */
    if (!(strcmp(key, "NodeName"))) {
	hostline = ustrdup(value);
	if (!(parseNodeNameEntry(hostline, *gres))) {
	    ufree(hostline);
	    /* an error occured, return true to stop parsing */
	    return true;
	}
	ufree(hostline);
    } else if (*gres && !(strcmp(key, "Name"))) {
	tmp = umalloc(strlen(value) +6 + 1);
	snprintf(tmp, strlen(value) +6, "Name=%s", value);
	//mlog("%s: Gres single name '%s'\n", __func__, tmp);
	parseGresOptions(tmp);
	ufree(tmp);
    }
    /* parsing was successful, continue with next line */
    return false;
}

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
    if (!getConfValueC(&Config, "SLURM_CPUS")) {
	mlog("%s: invalid SLURM_CPUS\n", __func__);
	return false;
    }
    if (!getConfValueC(&Config, "SLURM_SOCKETS")) {
	mlog("%s: invalid SLURM_SOCKETS\n", __func__);
	return false;
    }
    if (!getConfValueC(&Config, "SLURM_CORES_PER_SOCKET")) {
	mlog("%s: invalid SLURM_CORES_PER_SOCKET\n", __func__);
	return false;
    }
    if (!getConfValueC(&Config, "SLURM_THREADS_PER_CORE")) {
	mlog("%s: invalid SLURM_THREADS_PER_CORE\n", __func__);
	return false;
    }
    if (!getConfValueC(&Config, "SLURM_BOARDS")) {
	/* set default boards */
	addConfigEntry(&Config, "SLURM_BOARDS", "1");
    }
    return true;
}

int initConfig(char *filename, uint32_t *hash)
{
    char *confFile;
    struct stat sbuf;
    int gres = 0;

    /* parse psslurm config file */
    if (parseConfigFile(filename, &Config, false /*trimQuotes*/) < 0) return 0;
    setConfigDefaults(&Config, CONFIG_VALUES);
    if (verifyConfig(&Config, CONFIG_VALUES) != 0) {
	mlog("%s: verfiy of %s failed\n", __func__, filename);
	return 0;
    }

    /* parse slurm config file */
    if (!(confFile = getConfValueC(&Config, "SLURM_CONF"))) return 0;
    registerConfigHashAccumulator(hash);
    if (parseConfigFile(confFile, &SlurmConfig, true /*trimQuotes*/) < 0)
	return 0;
    registerConfigHashAccumulator(NULL);
    if (traverseConfig(&SlurmConfig, parseSlurmConf, &gres)) return 0;
    if (!(verifySlurmConf())) return 0;

    /* parse optional slurm gres config file */
    INIT_LIST_HEAD(&SlurmGresConfig);
    gres = 1;
    if (!(confFile = getConfValueC(&Config, "SLURM_GRES_CONF"))) return 0;
    if (stat(confFile, &sbuf) == -1) return 1;
    if (parseConfigFile(confFile, &SlurmGresTmp, true /*trimQuotes*/) < 0)
	return 0;

    if (traverseConfig(&SlurmGresTmp, parseSlurmConf, &gres)) return 0;
    freeConfig(&SlurmGresTmp);

    return 1;
}
