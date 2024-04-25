/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sched.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif

#include "slurmcommon.h" /* bind type constants */

#include "pscommon.h"  /* typedef of PSnodes_ID_t for stubs */
#include "pscpu.h"
#include "psenv.h"
#include "pslog.h"

#include "pluginconfig.h" /* read configuration file */

#include "psidpin.h"
#include "psslurmpin.h"
#include "psslurmstep.h"

#define MAX_SUPPORTED_SLURM_VERSION 2302

static int verbosity = 0;
static bool humanreadable = false;
static bool printmembind = false;

/* < 23.02 only:
 * --extra-node-info, --hint, --threads-per-core, and --ntasks-per-core */
static bool mutually_exclusive = false;

enum output_level {
    ERROROUT,
    INFOOUT,
    DEBUGOUT
};

/* slurm verion in format YYMM */
int slurm_version = 0;


static void outline(enum output_level lvl, const char* format, ...) {
    FILE *stream = stdout;

    switch(lvl) {
	case ERROROUT:
	    stream = stderr;
	    break;
	case INFOOUT:
	    if (verbosity < 1) return;
	    break;
	case DEBUGOUT:
	    if (verbosity < 2) return;
	    break;
    }

    va_list ap;
    va_start(ap, format);
    vfprintf(stream, format, ap);
    va_end(ap);
    printf("\n");
}

static void print_help() {
    printf("Usage: psslurmgetbind <sockets> <coresPerSocket> <threadsPerCore>"
	    " <options> : <srunOptions>\n"
	    "\n"
	    "Options:\n"
	    "   --help\n"
	    "          Print this help\n"
	    "   -v, --verbose\n"
	    "          Be verbose (twice for debugging)\n"
	    "   -h, --human-readable\n"
	    "          Print 0/1-blocks instead of hex masks\n"
	    "   -m, --membind\n"
	    "          Print memory binding, too\n"
	    "   -M,  --cpumap <CPUMAP>\n"
	    "          Use different cpumap\n"
	    "\n"
	    "Supported srun Options (see srun manpage):\n"
	    "   -N 1\n"
	    "   -n <tasks>\n"
	    "   -c <threadsPerTask>\n"
	    "   --cpu-bind=<cpuBindType>\n"
	    "   -m <distribution>, --distribution=<distribution>\n"
	    "   -B <resources>, --extra-node-info=<resources>\n"
	    "   --mem-bind=<memBindType>\n"
	    "   -O, --overcommit\n"
	    "   --exact\n"
	    "   --threads-per-core=<threads>\n");
}

static unsigned int atoui(char* in) {
    int num;
    num = atoi(in);
    return (num < 0) ? 0 : (unsigned) num;
}

/*
 * bitmask cpuBindType:
 * CPU_BIND_ONE_THREAD_PER_CORE - use only one hardware thread per core
 * CPU_BIND_NONE                - no pinning / pin to all threads
 * CPU_BIND_MAP                 - pin according to cpuBindString
 * CPU_BIND_MASK                - pin according to cpuBindString
 * CPU_BIND_LDMAP               - pin according to cpuBindString
 * CPU_BIND_LDMASK              - pin according to cpuBindString
 * CPU_BIND_TO_BOARDS           - not really supported
 * CPU_BIND_TO_SOCKETS          - pin to whole sockets
 * CPU_BIND_TO_LDOMS            - pin to whole sockets
 * CPU_BIND_TO_CORES            - pin to whole cores
 * CPU_BIND_TO_THREADS          - pin to threads (default)
 * CPU_BIND_LDRANK              - ...
 * CPU_BIND_RANK                - ...
*/

static bool readCpuBindType(char *ptr, uint16_t *cpuBindType,
			    char **cpuBindString)
{
    free(*cpuBindString);
    *cpuBindString = NULL;

    if (!strcmp(ptr, "none") || !strcmp(ptr, "no")) {
	*cpuBindType = CPU_BIND_NONE;
    } else if (!strncmp(ptr, "map_cpu:", 8)) {
	*cpuBindType = CPU_BIND_MAP;
	*cpuBindString = strdup(ptr+8);
    } else if (!strncmp(ptr, "mask_cpu:", 9)) {
	*cpuBindType = CPU_BIND_MASK;
	*cpuBindString = strdup(ptr+9);
    } else if (!strncmp(ptr, "map_ldom:", 9)) {
	*cpuBindType = CPU_BIND_LDMAP;
	*cpuBindString = strdup(ptr+9);
    } else if (!strncmp(ptr, "mask_ldom:", 10)) {
	*cpuBindType = CPU_BIND_LDMASK;
	*cpuBindString = strdup(ptr+10);
    } else if (!strcmp(ptr, "boards")) {
	*cpuBindType = CPU_BIND_TO_BOARDS;
    } else if (!strcmp(ptr, "sockets")) {
	*cpuBindType = CPU_BIND_TO_SOCKETS;
    } else if (!strcmp(ptr, "ldoms")) {
	*cpuBindType = CPU_BIND_TO_LDOMS;
    } else if (!strcmp(ptr, "cores")) {
	*cpuBindType = CPU_BIND_TO_CORES;
    } else if (!strcmp(ptr, "threads")) {
	*cpuBindType = CPU_BIND_TO_THREADS;
    } else if (!strcmp(ptr, "rank")) {
	*cpuBindType = CPU_BIND_RANK;
    } else if (!strcmp(ptr, "rank_ldom")) {
	*cpuBindType = CPU_BIND_LDRANK;
    } else {
	return false;
    }
    return true;
}

static bool readDistribution(char *ptr, uint32_t *taskDist) {

    /* looking for first colon */
    ptr = strchr(ptr, ':');

    if (!ptr) return false;

    ptr++;

    if (!strncmp(ptr, "cyclic", 6) || *ptr == '*') {
	if (*ptr == '*') {
	    ptr += 1;
	} else {
	    ptr += 6;
	}

	if (!strncmp(ptr, ":cyclic", 7) || !strncmp(ptr, ":*", 2)) {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC_CYCLIC;
	} else if (!strncmp(ptr, ":block", 6)) {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC_BLOCK;
	} else if (!strncmp(ptr, ":fcyclic", 8)) {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC_CFULL;
	} else if (*ptr == '\0' || *ptr == ',') {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC;
	} else {
	    return false;
	}
    } else if (!strncmp(ptr, "block", 5)) {
	ptr += 5;

	if (!strncmp(ptr, ":cyclic", 7)) {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK_CYCLIC;
	} else if (!strncmp(ptr, ":block", 6) || !strncmp(ptr, ":*", 2)) {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK_BLOCK;
	} else if (!strncmp(ptr, ":fcyclic", 8)) {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK_CFULL;
	} else if (*ptr == '\0' || *ptr == ',') {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK;
	} else {
	    return false;
	}
    } else if (!strncmp(ptr, "fcyclic", 7)) {
	ptr += 7;

	if (!strncmp(ptr, ":cyclic", 7)) {
	    *taskDist = SLURM_DIST_BLOCK_CFULL_CYCLIC;
	} else if (!strncmp(ptr, ":block", 6)) {
	    *taskDist = SLURM_DIST_BLOCK_CFULL_BLOCK;
	} else if (!strncmp(ptr, ":fcyclic", 8) || !strncmp(ptr, ":*", 2)) {
	    *taskDist = SLURM_DIST_BLOCK_CFULL_CFULL;
	} else if (*ptr == '\0' || *ptr == ',') {
	    *taskDist = SLURM_DIST_BLOCK_CFULL;
	} else {
	    return false;
	}
    } else {
	return false;
    }

    return true;
}

static bool readMemBindType(char *ptr, uint16_t *memBindType,
			    char **memBindString)
{
    free(*memBindString);
    *memBindString = NULL;

    if (!strcmp(ptr, "none") || !strcmp(ptr, "no")) {
	*memBindType = MEM_BIND_NONE;
    } else if (!strncmp(ptr, "map_mem:", 8)) {
	*memBindType = MEM_BIND_MAP;
	*memBindString = strdup(ptr+8);
    } else if (!strncmp(ptr, "mask_mem:", 9)) {
	*memBindType = MEM_BIND_MASK;
	*memBindString = strdup(ptr+9);
    } else if (!strcmp(ptr, "local")) {
	*memBindType = MEM_BIND_LOCAL;
    } else if (!strcmp(ptr, "rank")) {
	*memBindType = MEM_BIND_RANK;
    } else {
	return false;
    }
    return true;
}

static void handleExtraNodeInfo(char *value, uint16_t *cpuBindType,
			        uint16_t *useThreadsPerCore)
{
    /* never override settings from --cpu-bind */
    if (*cpuBindType) return;

    /* sockets given */
    *cpuBindType = CPU_BIND_TO_SOCKETS;
    char *cur = strstr(value, ":");
    if (cur) {
	/* cores given */
	*cpuBindType = CPU_BIND_TO_CORES;
	if ((cur = strstr(cur+1, ":"))) {
	    /* threads given */
	    *cpuBindType = CPU_BIND_TO_THREADS;
	    *useThreadsPerCore = atoi(cur+1);
	}
    }
}

static void handleThreadsPerCore(char *value, uint16_t *cpuBindType,
			        uint16_t *useThreadsPerCore)
{
    /* never override settings from --cpu-bind */
    if (*cpuBindType) return;

    *cpuBindType = CPU_BIND_TO_THREADS;
    *useThreadsPerCore = atoi(value);
}

#define PSSLURM_CONFIG_FILE  PLUGINDIR "/psslurm.conf"

const ConfDef_t confDef[] =
{
    { "SLURM_CONF_SERVER", 0,
	"ip[:port]",
	"none",
	"slurmctld to fetch configuration files from" },
    { "SLURM_PROTO_VERSION", 0,
	"string",
	"auto",
	"Slurm protocol version as string or auto (e.g. 17.11)" },
    { "SINFO_BINARY", 0,
	"string",
	"/usr/bin/sinfo",
	"Path to the sinfo binary used for automatic protocol detection" },
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
    { NULL, 0, NULL, NULL, NULL },
};

Config_t Config = NULL;

bool readConfigFile(void)
{
    initConfig(&Config);
    /* parse psslurm config file */
    if (parseConfigFile(PSSLURM_CONFIG_FILE, Config) < 0) return false;
    setConfigDefaults(Config, confDef);

    return true;
}

const char *autoDetectSlurmVersion(void)
{
    static char autoVer[32] = { '\0' };

    const char *sinfo = getConfValueC(Config, "SINFO_BINARY");
    if (*sinfo == '\0' ) {
	outline(DEBUGOUT, "no SINFO_BINARY provided");
	return NULL;
    }

    struct stat sb;
    if (stat(sinfo, &sb) == -1) {
	outline(ERROROUT, "%s: stat('%s')", __func__, sinfo);
	return NULL;
    } else if (!(sb.st_mode & S_IXUSR)) {
	outline(INFOOUT, "'%s' not executable", sinfo);
	return NULL;
    }

    char sinfoCmd[128];
    char *server = getConfValueC(Config, "SLURM_CONF_SERVER");
    if (!strcmp(server, "none")) {
	snprintf(sinfoCmd, sizeof(sinfoCmd), "%s --version", sinfo);
    } else {
	snprintf(sinfoCmd, sizeof(sinfoCmd), "SLURM_CONF_SERVER=%s "
		 "%s --version", server, sinfo);
    }
    FILE *fp = popen(sinfoCmd, "r");
    if (!fp) {
	outline(ERROROUT, "%s: popen('%s')", __func__, sinfoCmd);
	return NULL;
    }

    char *line = NULL;
    size_t len = 0;
    while (getline(&line, &len, fp) != -1) {
	if (strncmp(line, "slurm ", 6)) continue;
	if (sscanf(line, "slurm %31s", autoVer) == 1) {
	    char *dash = strchr(autoVer, '-');
	    if (dash) *dash = '\0';
	    break;
	} else {
	    char *nl = strchr(line, '\n');
	    if (nl) *nl = '\0';
	   outline(INFOOUT, "invalid slurm version string: '%s'", line);
	}
    }
    free(line);
    pclose(fp);

    if (!strlen(autoVer)) {
	outline(INFOOUT, "could not autodetect Slurm version");
	return NULL;
    }
    return autoVer;
}

/* node info */
uint16_t socketCount = 0;
uint16_t coresPerSocket = 0;
uint16_t threadsPerCore = 0;

short *cpumap = NULL;
size_t cpumap_size;
size_t cpumap_maxsize;

static bool addCPUmapEnt(char *token)
{
    char *end;
    int val = (int)strtol(token, &end, 0);
    if (*end) {
	outline(ERROROUT, "Invalid cpumap entry.");
	return false;
    }

    if (cpumap == NULL) {
	cpumap_maxsize = 16;
	cpumap = malloc(cpumap_maxsize * sizeof(*cpumap));
    } else if (cpumap_size == cpumap_maxsize) {
	cpumap_maxsize *= 2;
	cpumap = realloc(cpumap, cpumap_maxsize * sizeof(*cpumap));
    }
    if (cpumap == NULL) {
	outline(ERROROUT, "No memory for cpumap.");
	return false;
    }
    cpumap[cpumap_size] = val;
    cpumap_size++;

    return true;
}

static bool parse_cpumap(char *mapStr, size_t threadCount)
{
    char *delim = "\n ,";
    for (char *tok = strtok(mapStr, delim); tok; tok = strtok(NULL, delim)) {
	if (!addCPUmapEnt(tok)) return false;
    }
    if (cpumap_size != threadCount) {
	outline(ERROROUT, "Length of cpumap does not match total threads.");
	return false;
    }
    return true;
}

int main(int argc, char *argv[])
{

    if (argc < 4) {
	print_help();
	return -1;
    }

    socketCount = atoui(argv[1]);
    coresPerSocket = atoui(argv[2]);
    threadsPerCore = atoui(argv[3]);

    if (!socketCount) {
	outline(ERROROUT, "Invalid number of sockets.");
	return -1;
    }

    if (!coresPerSocket) {
	outline(ERROROUT, "Invalid number of cores per socket.");
	return -1;
    }

    if (!threadsPerCore) {
	outline(ERROROUT, "Invalid number of threads per Core.");
	return -1;
    }

    size_t threadCount = socketCount * coresPerSocket * threadsPerCore;

    /* parse programm options */
    int i = 4;
    while (i < argc) {
	char *cur = argv[i++];

	if (!strcmp(cur, "--help")) {
	    print_help();
	    return 0;
	}

	if (!strcmp(cur, "--verbose") || !strcmp(cur, "-v")) {
	    verbosity++;
	    continue;
	}

	if (!strcmp(cur, "--human-readable") || !strcmp(cur, "-h")) {
	    humanreadable = true;
	    continue;
	}

	if (!strcmp(cur, "--membind") || !strcmp(cur, "-m")) {
	    printmembind = true;
	    continue;
	}

	if (!strcmp(cur, "--cpumap") || !strcmp(cur, "-M")) {
	    if (i == argc) {
		outline(ERROROUT, "Missing argument");
		return -1;
	    }
	    if (!parse_cpumap(argv[i++], threadCount)) return -1;
	    continue;
	}

	if (!strcmp(cur, ":")) {
	    break;
	}
    }

    if (!cpumap) {
	char *mapStr = getenv("__PSI_CPUMAP");
	if (mapStr) {
	    outline(INFOOUT, "Environment sets cpumap (__PSI_CPUMAP).");
	    if (!parse_cpumap(mapStr, threadCount)) return -1;
	}
    }

    outline(INFOOUT, "node: %hu sockets, %hu cores per socket,"
	    " %hu threads per core, %zu threads in total",
	    socketCount, coresPerSocket, threadsPerCore, threadCount);

    if (humanreadable && cpumap) {
	outline(ERROROUT, "Warning: --human-readable and --cpumap used together"
		" probably makes no sense.");
    }

    if (printmembind) {
#ifdef HAVE_LIBNUMA
	int maxnodes = sizeof(*((struct bitmask *)0)->maskp) * 8;
	if (socketCount > maxnodes) {
	    outline(ERROROUT, "Membind printing not supported for more than"
		    " %d sockets", maxnodes);
	    return -1;
	}
#else
	outline(ERROROUT, "Membind printing unsupported w/out NUMA support\n");
	return -1;
#endif
    }

    if (!readConfigFile()) {
	outline(ERROROUT, "Error reading psslurm.conf.");
	exit(-1);
    }

    const char *slurmver = getenv("SLURM_VERSION");
    if (!slurmver) {
	slurmver = getConfValueC(Config, "SLURM_PROTO_VERSION");
	if (!slurmver || !strcmp(slurmver, "auto")) {
	    slurmver = autoDetectSlurmVersion();
	    if (slurmver) outline(INFOOUT, "SLURM_VERSION autodetected");
	} else {
	    outline(INFOOUT, "Take SLURM_PROTO_VERSION from psslurm.conf as"
		    " SLURM_VERSION");
	}
    }
    if (slurmver) {
	char *sv = strdup(slurmver);
	if (strlen(sv) >= 6) sv[5] = '\0'; /* cut release part */
	char *mm = sv;
	char *yy = strsep(&mm, ".");
	slurm_version = atol(yy) * 100 + atol(mm);
	if (slurm_version < 2108) {
	    outline(ERROROUT, "Not supporting Slurm versions before 21.08");
	    free(sv);
	    return -1;
	}
	if ((slurm_version < 2311 && (slurm_version != 2108
				     && slurm_version != 2205
				     && slurm_version != 2302))
	    || (slurm_version >= 2311 && (slurm_version % 100 != 5
					  && slurm_version % 100 != 11))) {
	    outline(ERROROUT, "Invalid slurm version '%s.%s' in SLURM_VERSION",
		    yy, mm);
	    free(sv);
	    return -1;
	}
	free(sv);
    } else {
	slurm_version = 2302;
    }

    outline(INFOOUT, "Simulating Slurm version %02d.%02d", slurm_version / 100,
	    slurm_version % 100);

    if (slurm_version > MAX_SUPPORTED_SLURM_VERSION) {
	outline(ERROROUT, "Warning: Slurm version %02d.%02d not yet supported",
		slurm_version / 100, slurm_version % 100);
    }

    /* task info */
    uint32_t tasksPerNode = 0;
    uint16_t threadsPerTask = 1;

    /* pinning info */
    uint16_t cpuBindType = 0;
    char *cpuBindString = NULL;
    uint32_t taskDist = 0;
    bool nomultithread = false;
    bool overcommit = false;
    bool exact = false;
    uint16_t useThreadsPerCore = 0;

    /* membind info */
    uint16_t memBindType = 0;
    char *memBindString = NULL;

    /* parse srun options */
    while (i < argc) {
	char *cur = argv[i++];
	char *val;

	if (!strncmp(cur, "-N", 2)) {
	    if (*(cur+2) == '\0') {
		if (i == argc) {
		    outline(ERROROUT, "Syntax error reading value for -N.");
		    exit(-1);
		}
		val = argv[i++];
	    } else {
		val = cur + 2;
	    }
	    outline(DEBUGOUT, "Reading -N value: \"%s\"", val);
	    if (atoi(val) != 1) {
		outline(ERROROUT, "Only supported value for -N option is 1.");
		exit(-1);
	    }
	} else if (!strncmp(cur, "-n", 2)) {
	    if (*(cur+2) == '\0') {
		if (i == argc) {
		    outline(ERROROUT, "Syntax error reading value for -n.");
		    exit(-1);
		}
		val = argv[i++];
	    } else {
		val = cur + 2;
	    }
	    outline(DEBUGOUT, "Reading -n value: \"%s\"", val);
	    tasksPerNode = atoui(val);
	    if (!tasksPerNode) {
		outline(ERROROUT, "Invalid number of tasks.");
		exit(-1);
	    }
	} else if (!strncmp(cur, "-c", 2)) {
	    if (*(cur+2) == '\0') {
		if (i == argc) {
		    outline(ERROROUT, "Syntax error reading value for -c.");
		    exit(-1);
		}
		val = argv[i++];
	    } else {
		val = cur + 2;
	    }
	    outline(DEBUGOUT, "Reading -c value: \"%s\"", val);
	    threadsPerTask = atoui(val);
	    if (!threadsPerTask) {
		outline(ERROROUT, "Invalid number of threads per task.");
		exit(-1);
	    }
	    /* starting with Slurm 22.05 -c implies --exact */
	    if (slurm_version >= 2205) {
		exact = true;
		outline(INFOOUT, "Slurm >= 22.05: '-c' implies '--exact'");
	    }
	} else if (!strncmp(cur, "--cpu-bind=", 11)) {
	    outline(DEBUGOUT, "Reading --cpu-bind value: \"%s\"", cur+11);
	    if (!readCpuBindType(cur+11, &cpuBindType, &cpuBindString)) {
		outline(ERROROUT, "Invalid cpu bind type.");
		exit(-1);
	    }
	} else if (!strncmp(cur, "--distribution=", 15)) {
	    outline(DEBUGOUT, "Reading --distribution value: \"%s\"", cur+15);
	    if (!readDistribution(cur+15, &taskDist)) {
		outline(ERROROUT, "Invalid distribution type.");
		exit(-1);
	    }
	} else if (!strcmp(cur, "-m")) {
	    if (i == argc) {
		outline(ERROROUT, "Syntax error reading value for -m.");
		exit(-1);
	    }
	    outline(DEBUGOUT, "Reading -m value: \"%s\"", argv[i]);
	    if (!readDistribution(argv[i++], &taskDist)) {
		outline(ERROROUT, "Invalid distribution type.");
		exit(-1);
	    }
	} else if (!strncmp(cur, "--extra-node-info=", 18)) {
	    if (mutually_exclusive && slurm_version < 2302) {
		outline(ERROROUT, "srun options -B|--extra-node-info, --hint,"
			" --threads-per-core (and --ntasks-per-core) are"
			" mutually exclusive.");
		exit(-1);
	    }
	    mutually_exclusive = true;
	    outline(DEBUGOUT, "Reading --extra-node-info value: \"%s\"",
		    cur+18);
	    handleExtraNodeInfo(cur+18, &cpuBindType, &useThreadsPerCore);
	} else if (!strcmp(cur, "-B")) {
	    if (i == argc) {
		outline(ERROROUT, "Syntax error reading value for -B.");
		exit(-1);
	    }
	    if (mutually_exclusive && slurm_version < 2302) {
		outline(ERROROUT, "srun options -B|--extra-node-info, --hint,"
			" --threads-per-core (and --ntasks-per-core) are"
			" mutually exclusive.");
		exit(-1);
	    }
	    mutually_exclusive = true;
	    outline(DEBUGOUT, "Reading -B value: \"%s\"", argv[i]);
	    handleExtraNodeInfo(argv[i++], &cpuBindType, &useThreadsPerCore);
	} else if (!strcmp(cur, "--hint=nomultithread")) {
	    if (mutually_exclusive && slurm_version < 2302) {
		outline(ERROROUT, "srun options -B|--extra-node-info, --hint,"
			" --threads-per-core (and --ntasks-per-core) are"
			" mutually exclusive.");
		exit(-1);
	    }
	    mutually_exclusive = true;
	    outline(DEBUGOUT, "Read hint \"nomultithread\"");
	    nomultithread = true;
	} else if (!strncmp(cur, "--mem-bind=", 11)) {
	    outline(DEBUGOUT, "Reading --mem-bind value: \"%s\"", cur+11);
	    if (!readMemBindType(cur+11, &memBindType, &memBindString)) {
		outline(ERROROUT, "Invalid memory bind type.");
		exit(-1);
	    }
	} else if (!strcmp(cur, "--overcommit") || !strcmp(cur, "-O")) {
	    outline(DEBUGOUT, "Read option \"overcommit\"");
	    overcommit = true;
	} else if (!strcmp(cur, "--exact")) {
	    outline(DEBUGOUT, "Read option \"exact\"");
	    exact = true;
	} else if (!strncmp(cur, "--threads-per-core=", 19)) {
	    if (mutually_exclusive && slurm_version < 2302) {
		outline(ERROROUT, "srun options -B|--extra-node-info, --hint,"
			" --threads-per-core (and --ntasks-per-core) are"
			" mutually exclusive.");
		exit(-1);
	    }
	    mutually_exclusive = true;
	    outline(DEBUGOUT, "Reading --threads-per-core value: \"%s\"",
		    cur+19);
	    handleThreadsPerCore(cur+19, &cpuBindType, &useThreadsPerCore);
	} else {
	    outline(ERROROUT, "Invalid argument: \"%s\"", cur);
	    exit(-1);
	}
    }

    if (!cpuBindString) cpuBindString = "";
    if (!memBindString) memBindString = "";

    /* creating env containing hints */
    env_t env = envNew(NULL);
    if (nomultithread) envSet(env, "PSSLURM_HINT", "nomultithread");

    if (!tasksPerNode) {
	outline(ERROROUT, "Invalid number of tasks per node.");
	exit(-1);
    }

    if (!threadsPerTask) {
	outline(ERROROUT, "Invalid number of threads per task.");
	exit(-1);
    }

    outline(INFOOUT, "job: %u tasks, %hu threads per task, "
	    "using %hu threads per core", tasksPerNode, threadsPerTask,
	    useThreadsPerCore ? useThreadsPerCore : threadsPerCore);
    outline(INFOOUT, "cpuBindType = 0x%X - cpuBindString = \"%s\"", cpuBindType,
	    cpuBindString);
    outline(INFOOUT, "taskDist = 0x%X", taskDist);
    if (cpumap) {
	    size_t maxout = 8 + threadCount * 4;
	    char out[maxout];
	    char *ptr = out;
	    ptr += snprintf(ptr, maxout, "cpumap: ");
	    for (size_t i = 0; i < cpumap_size; i++) {
		ptr += snprintf(ptr, maxout - (out - ptr), "%d ", cpumap[i]);
	    }
	    outline(INFOOUT, out);
    }
    outline(INFOOUT, "");

    test_pinning(socketCount, coresPerSocket, threadsPerCore, tasksPerNode,
		 threadsPerTask, cpuBindType, cpuBindString, taskDist,
		 memBindType, memBindString, env, humanreadable, printmembind,
		 overcommit, exact, useThreadsPerCore);
}


static logger_t lt;
logger_t *psslurmlogger = &lt;
logger_t *pluginlogger = NULL;

logger_t* logger_init(const char* tag, FILE* logfile)
{
    logger_t* logger = (logger_t*)malloc(sizeof(*logger));
    return logger;
}

void logger_finalize(logger_t* logger)
{
    free(logger);
}

int32_t logger_getMask(logger_t* logger)
{
    return 0;
}

void logger_setMask(logger_t* logger, int32_t mask)
{
    return;
}

void logger_exit(logger_t* logger, int eno, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    /* @todo print eno */
    vprintf(format, ap);
    va_end(ap);
    exit(-1);
}

void logger_warn(logger_t* logger, int32_t key, int eno,
		 const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    /* @todo print eno */
    vprintf(format, ap);
    va_end(ap);
}

void logger_print(logger_t* logger, int32_t key, const char* format, ...)
{
    if (verbosity != DEBUGOUT) return;

    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

void logger_funcprint(logger_t* logger, const char *func, int32_t key,
		      const char* format, ...)
{
    if (verbosity != DEBUGOUT) return;

    static char fmtStr[1024];
    const char *fmt = format;

    size_t len = snprintf(fmtStr, sizeof(fmtStr), "%s: %s", func, format);
    if (len + 1 <= sizeof(fmtStr)) fmt = fmtStr;

    va_list ap;
    va_start(ap, format);
    vprintf(fmt, ap);
    va_end(ap);
}

typedef void Job_t;

uint32_t __getLocalRankID(uint32_t rank, Step_t *step,
			  const char *caller, const int line)
{
    return rank;
}

Job_t *findJobById(uint32_t jobid) {
    return NULL;
}

short PSIDnodes_getNumThrds(PSnodes_ID_t id) {
    return 0;
}

void printChildMessage(Step_t *step, char *plMsg, uint32_t msgLen,
		       uint8_t type, int64_t taskid) {
    return;
}

int PSIDnodes_bindMem(PSnodes_ID_t id) {
    return 1;
}

PSnodes_ID_t PSIDnodes_lookupHost(in_addr_t addr) {
    return 0;
}

in_addr_t PSIDnodes_getAddr(PSnodes_ID_t id) {
    return 0;
}

bool PSIDfwd_inForwarder(void) {
    /* used to decide about using fprintf() or PSIDfw_printMsgf() for output */
    return false;
}

int PSIDfwd_printMsgf(PSLog_msg_t type, const char *format, ...) {
    /* this should never been actually called since PSIDfwd_inForwarder()
     * always returns false */
    return 0;
}

void fwCMD_printMsg(Job_t *job, Step_t *step, char *plMsg, uint32_t msgLen,
		    uint8_t type, int32_t rank) {
    fprintf(stderr, "%d: CPU binding: %s", rank < 0 ? 0 : rank, plMsg);
    return;
}

char *trim_quotes(char *string) {
    return string;
}

char *trim(char *string) {
    if (!string) return NULL;

    /* remove leading whitespaces */
    while (string[0] == ' ') {
	string++;
    }

    /* remove trailing whitespaces */
    size_t len = strlen(string);
    while (len >0 && (string[len-1] == ' ' || string[len-1] == '\n')) {
	string[len-1] = '\0';
	len--;
    }

    return string;
}

int numa_available(void) {
    return 0;
}

#ifdef HAVE_LIBNUMA
struct bitmask *numa_allocate_nodemask(void) {
    struct bitmask *b = malloc(sizeof(*b));
    b->maskp = calloc(1, sizeof(unsigned long));
    b->size = sizeof(unsigned long) * 8;
    return b;
}

void numa_bitmask_free(struct bitmask *b) {
    free(b->maskp);
    free(b);
}

int numa_bitmask_isbitset(const struct bitmask *b, unsigned int n) {
    if (b == NULL) return 1;
    return (*(b->maskp) & (1 << n)) ? 1 : 0;
}

struct bitmask *numa_bitmask_setbit(struct bitmask *b, unsigned int n) {
    *(b->maskp) |= 1 << n;
    return b;
}

int numa_max_node(void) {
    return socketCount - 1;
}

struct bitmask *numa_get_mems_allowed(void) {
    return NULL;
}

void numa_set_membind(struct bitmask *nodemask) {
    return;
}

struct bitmask *numa_bitmask_setall(struct bitmask *b) {
    memset(b->maskp, 0xff, sizeof(*b->maskp));
    return b;
}

struct bitmask *numa_get_membind(void) {
    return NULL;
}
#endif

short PSIDnodes_mapCPU(PSnodes_ID_t id, short cpu)
{
    if (!cpumap) return cpu;

    if (cpu < 0 || (unsigned)cpu >= cpumap_size) return -1;

    return cpumap[cpu];
}

short PSIDnodes_unmapCPU(PSnodes_ID_t id, short hwthread)
{
    if (!cpumap) return hwthread;

    for (unsigned short i = 0; i < cpumap_size; i++) {
	if (cpumap[i] == hwthread) return i;
    }
    return -1;
}

short PSIDnodes_numGPUs(PSnodes_ID_t id) {
    return 0;
}

bool PSIDpin_getCloseDevs(PSnodes_ID_t id, cpu_set_t *CPUs, PSCPU_set_t *GPUs,
			  uint16_t closeGPUs[], size_t *closeCnt,
			  uint16_t localGPUs[], size_t *localCnt,
			  PSIDpin_devType_t type) {
    return true;
}

cpu_set_t *PSIDpin_mapCPUs(PSnodes_ID_t id, PSCPU_set_t set) {
    return NULL;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
