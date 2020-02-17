/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "psslurmpin.h"

#include "slurmcommon.h" /* bind type constants */

#include "psnodes.h" /* typedef of PSnodes_ID_t for stubs */

#include "list.h"
typedef list_t Config_t;

static int verbosity = 0;
static bool humanreadable = false;

enum output_level {
    ERROROUT,
    INFOOUT,
    DEBUGOUT
};

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
    printf("Usage: psslurmgetpin <sockets> <coresPerSocket> <threadsPerCore>"
	    " <options> : <srunOptions>\n"
	    "\n"
	    "Options:\n"
	    "   --help\n"
	    "          Print this help\n"
	    "   -v, --verbose\n"
	    "          Be verbose (twice for debugging)\n"
	    "   -h, --human-readable\n"
	    "          Print 0/1-blocks instead of hex masks\n"
	    "\n"
	    "Supported srun Options (see srun manpage):\n"
	    "   -N 1\n"
	    "   -n <tasks>\n"
	    "   -c <threadsPerTask>\n"
	    "   --cpu_bind=<cpuBindType>\n"
	    "   -m <distribution>, --distribution=<distribution>\n");
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
	char **cpuBindString) {
    if (strcmp(ptr, "none") == 0 || strcmp(ptr, "no") == 0) {
	*cpuBindType = CPU_BIND_NONE;
	*cpuBindString = NULL;
    }
    else if (strncmp(ptr, "map_cpu:", 8) == 0) {
	*cpuBindType = CPU_BIND_MAP;
	*cpuBindString = strdup(ptr+8);
    }
    else if (strncmp(ptr, "mask_cpu:", 9) == 0) {
	*cpuBindType = CPU_BIND_MASK;
	*cpuBindString = strdup(ptr+9);
    }
    else if (strncmp(ptr, "map_ldom:", 9) == 0) {
	*cpuBindType = CPU_BIND_LDMAP;
	*cpuBindString = strdup(ptr+9);
    }
    else if (strncmp(ptr, "mask_ldom:", 10) == 0) {
	*cpuBindType = CPU_BIND_LDMASK;
	*cpuBindString = strdup(ptr+10);
    }
    else if (strcmp(ptr, "boards") == 0) {
	*cpuBindType = CPU_BIND_TO_BOARDS;
	*cpuBindString = NULL;
    }
    else if (strcmp(ptr, "sockets") == 0) {
	*cpuBindType = CPU_BIND_TO_SOCKETS;
	*cpuBindString = NULL;
    }
    else if (strcmp(ptr, "ldoms") == 0) {
	*cpuBindType = CPU_BIND_TO_LDOMS;
	*cpuBindString = NULL;
    }
    else if (strcmp(ptr, "cores") == 0) {
	*cpuBindType = CPU_BIND_TO_CORES;
	*cpuBindString = NULL;
    }
    else if (strcmp(ptr, "threads") == 0) {
	*cpuBindType = CPU_BIND_TO_THREADS;
	*cpuBindString = NULL;
    }
    else if (strcmp(ptr, "rank") == 0) {
	*cpuBindType = CPU_BIND_RANK;
	*cpuBindString = NULL;
    }
    else if (strcmp(ptr, "ldrank") == 0) {
	*cpuBindType = CPU_BIND_LDRANK;
	*cpuBindString = NULL;
    }
    else {
	return false;
    }
    return true;
}

static bool readDistribution(char *ptr, uint32_t *taskDist) {

    /* looking for first colon */
    ptr = strchr(ptr, ':');

    if (!ptr) return false;

    ptr++;

    if (strncmp(ptr, "cyclic", 6) == 0 || *ptr == '*') {
	if (*ptr == '*') {
	    ptr += 1;
	}
	else {
	    ptr += 6;
	}

	if (strncmp(ptr, ":cyclic", 7) == 0
		|| strncmp(ptr, ":*", 2) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC_CYCLIC;
        }
	else if (strncmp(ptr, ":block", 6) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC_BLOCK;
        }
	else if (strncmp(ptr, ":fcyclic", 8) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC_CFULL;
        }
	else if (*ptr == '\0' || *ptr == ',') {
	    *taskDist = SLURM_DIST_BLOCK_CYCLIC;
	}
	else {
	    return false;
	}
    }
    else if (strncmp(ptr, "block", 5) == 0) {
	ptr += 5;

	if (strncmp(ptr, ":cyclic", 7) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK_CYCLIC;
        }
	else if (strncmp(ptr, ":block", 6) == 0
		|| strncmp(ptr, ":*", 2) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK_BLOCK;
        }
	else if (strncmp(ptr, ":fcyclic", 8) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK_CFULL;
        }
	else if (*ptr == '\0' || *ptr == ',') {
	    *taskDist = SLURM_DIST_BLOCK_BLOCK;
	}
	else {
	    return false;
	}
    }
    else if (strncmp(ptr, "fcyclic", 7) == 0) {
	ptr += 7;

	if (strncmp(ptr, ":cyclic", 7) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_CFULL_CYCLIC;
        }
	else if (strncmp(ptr, ":block", 6) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_CFULL_BLOCK;
        }
	else if (strncmp(ptr, ":fcyclic", 8) == 0
		|| strncmp(ptr, ":*", 2) == 0) {
	    *taskDist = SLURM_DIST_BLOCK_CFULL_CFULL;
        }
	else if (*ptr == '\0' || *ptr == ',') {
	    *taskDist = SLURM_DIST_BLOCK_CFULL;
	}
	else {
	    return false;
	}
    }
    else {
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

    /* node info */
    uint16_t socketCount = 0;
    uint16_t coresPerSocket = 0;
    uint16_t threadsPerCore = 0;

    socketCount = atoi(argv[1]);
    coresPerSocket = atoi(argv[2]);
    threadsPerCore = atoi(argv[3]);

    if (socketCount == 0) {
	outline(ERROROUT, "Invalid number of sockets.");
	return -1;
    }

    if (coresPerSocket == 0) {
	outline(ERROROUT, "Invalid number of cores per socket.");
	return -1;
    }

    if (threadsPerCore == 0) {
	outline(ERROROUT, "Invalid number of threads per Core.");
	return -1;
    }

    outline(INFOOUT, "node: %hu sockets, %hu cores per socket,"
	    " %hu threads per core, %hu threads in total",
	    socketCount, coresPerSocket, threadsPerCore,
	    socketCount * coresPerSocket * threadsPerCore);

    /* parse programm options */
    int i = 1;
    for (; i < argc; i++) {
	char *cur = argv[i];

	if (strcmp(cur, "--help") == 0) {
	    print_help();
	    return 0;
	}

	if (strcmp(cur, "--verbose") == 0 || strcmp(cur, "-v") == 0) {
	    verbosity++;
	}

	if (strcmp(cur, "--human-readable") == 0 || strcmp(cur, "-h") == 0) {
	    humanreadable = true;
	}

	if (strcmp(cur, ":") == 0) {
	    break;
	}
    }

    /* task info */
    uint32_t tasksPerNode = 0;
    uint16_t threadsPerTask = 0;

    /* pinning info */
    uint16_t cpuBindType = 0;
    char *cpuBindString = "";
    uint32_t taskDist = 0;
    bool nomultithread = false;


    /* parse srun options */
    for (i++; i < argc; i++) {
	char *cur = argv[i];
	char *val;

	if (strncmp(cur, "-N", 2) == 0) {
	    if (*(cur+2) == '\0') {
		if (++i == argc) {
		    outline(ERROROUT, "Syntax error reading value for -N.");
		    return -1;
		}
		val = argv[i];
	    } else {
		val = cur + 2;
	    }
	    outline(DEBUGOUT, "Reading -N value: \"%s\"", val);
	    if (atoi(val) != 1) {
		outline(ERROROUT, "Only supported value for -N option is 1.");
		return -1;
	    }
	}
	else if (strncmp(cur, "-n", 2) == 0) {
	    if (*(cur+2) == '\0') {
		if (++i == argc) {
		    outline(ERROROUT, "Syntax error reading value for -n.");
		    return -1;
		}
		val = argv[i];
	    } else {
		val = cur + 2;
	    }
	    outline(DEBUGOUT, "Reading -n value: \"%s\"", val);
	    tasksPerNode = atoi(val);
	    if (tasksPerNode == 0) {
		outline(ERROROUT, "Invalid number of tasks.");
		return -1;
	    }
	}
	else if (strncmp(cur, "-c", 2) == 0) {
	    if (*(cur+2) == '\0') {
		if (++i == argc) {
		    outline(ERROROUT, "Syntax error reading value for -c.");
		    return -1;
		}
		val = argv[i];
	    } else {
		val = cur + 2;
	    }
	    outline(DEBUGOUT, "Reading -c value: \"%s\"", val);
	    threadsPerTask = atoi(val);
	    if (threadsPerTask == 0) {
		outline(ERROROUT, "Invalid number of threads per task.");
		return -1;
	    }
	}
	else if (strncmp(cur, "--cpu-bind=", 11) == 0) {
	    outline(DEBUGOUT, "Reading --cpu-bind value: \"%s\"", cur+11);
	    if (!readCpuBindType(cur+11, &cpuBindType, &cpuBindString)) {
		outline(ERROROUT, "Invalid bind type.");
		return -1;
	    }
	}
	else if (strncmp(cur, "--distribution=", 15) == 0) {
	    outline(DEBUGOUT, "Reading --distribution value: \"%s\"", cur+15);
	    if (!readDistribution(cur+15, &taskDist)) {
		outline(ERROROUT, "Invalid distribution type.");
		return -1;
	    }
	}
	else if (strcmp(cur, "-m") == 0) {
	    if (++i == argc) {
		outline(ERROROUT, "Syntax error reading value for -m.");
		return -1;
	    }
	    outline(DEBUGOUT, "Reading -m value: \"%s\"", argv[i]);
	    if (!readDistribution(argv[i], &taskDist)) {
		outline(ERROROUT, "Invalid distribution type.");
		return -1;
	    }
	}
	else if (strcmp(cur, "--hint=nomultithread") == 0) {
	    outline(DEBUGOUT, "Read hint \"nomultithread\"");
	    nomultithread = true;
	}
	else {
	    outline(DEBUGOUT, "Invalid argument: \"%s\"", cur);
	    return -1;
	}
    }

    if (nomultithread) cpuBindType |= CPU_BIND_ONE_THREAD_PER_CORE;

    outline(INFOOUT, "job: %u tasks, %hu threads per task", tasksPerNode,
	    threadsPerTask);
    outline(INFOOUT, "cpuBindType = 0x%X - cpuBindString = \"%s\"", cpuBindType,
	    cpuBindString);
    outline(INFOOUT, "taskDist = 0x%X", taskDist);
    outline(INFOOUT, "");

    test_pinning(cpuBindType, cpuBindString, taskDist, socketCount,
	    coresPerSocket, threadsPerCore, tasksPerNode, threadsPerTask,
	    humanreadable);
}


static logger_t lt;
logger_t *psslurmlogger = &lt;
logger_t *pluginlogger = NULL;

void logger_print(logger_t* logger, int32_t key, const char* format, ...) {

    if (verbosity != DEBUGOUT) return;

    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    return;
}

typedef void Job_t;

uint32_t getLocalRankID(uint32_t rank, Step_t *step, uint32_t nodeId) {
    return 0;
}

Job_t *findJobById(uint32_t jobid) {
    return NULL;
}

short PSIDnodes_getVirtCPUs(PSnodes_ID_t id) {
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

char* PSC_printTID(PStask_ID_t tid) {
    return "<TID>";
}

pid_t PSC_getPID(PStask_ID_t tid) {
    return 0;
}

char *getConfValueC(Config_t *conf, char *key) {
    return key;
}

PSnodes_ID_t PSC_getMyID(void) {
    return 0;
}
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
