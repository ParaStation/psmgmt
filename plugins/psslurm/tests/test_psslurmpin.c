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

/*
 * bitmask cpuBindType:
 * CPU_BIND_ONE_THREAD_PER_CORE - use only one hardware thread per core
 * CPU_BIND_NONE                - no pinning / pin to all threads
 * CPU_BIND_TO_BOARDS           - not really supported
 * CPU_BIND_MAP                 - pin according to cpuBindString
 * CPU_BIND_MASK                - pin according to cpuBindString
 * CPU_BIND_LDMAP               - pin according to cpuBindString
 * CPU_BIND_LDMASK              - pin according to cpuBindString
 * CPU_BIND_TO_SOCKETS          - pin to whole sockets
 * CPU_BIND_TO_LDOMS            - pin to whole sockets (same as CPU_BIND_TO_SOCKETS)
 * CPU_BIND_LDRANK              - pin to as many threads as needed inside sockets
 * CPU_BIND_RANK                - concerned as default
 * CPU_BIND_TO_THREADS          - concerned as default (same as CPU_BIND_RANK)
*/
int pinning(int argc, char *argv[])
{
    /* node info */
    uint16_t socketCount = 3;
    uint16_t coresPerSocket = 5;
    uint16_t threadsPerCore = 3;

    /* task info */
    uint32_t tasksPerNode = 3;
    uint16_t threadsPerTask = 12;

    /* pinning info */
    uint16_t cpuBindType = CPU_BIND_LDRANK;
    char *cpuBindString = "";
    uint32_t taskDist;

    if (argc != 11) {
	printf("Usage: test_psslurmpin pinning <sockets> <coresPerSocket>"
		" <threadsPerCore> <tasks> <threadsPerTask> <bindType>"
		" <bindString> <distribution> <oneThreadPerCore>\n");
	return -1;
    }

    socketCount = atoi(argv[2]);
    coresPerSocket = atoi(argv[3]);
    threadsPerCore = atoi(argv[4]);

    /* task info */
    tasksPerNode = atoi(argv[5]);
    threadsPerTask = atoi(argv[6]);

    /* pinning info */
    if (strcmp(argv[7], "none") == 0) cpuBindType = CPU_BIND_NONE;
    else if (strcmp(argv[7], "map") == 0) cpuBindType = CPU_BIND_MAP;
    else if (strcmp(argv[7], "mask") == 0) cpuBindType = CPU_BIND_MASK;
    else if (strcmp(argv[7], "ldmap") == 0) cpuBindType = CPU_BIND_LDMAP;
    else if (strcmp(argv[7], "ldmask") == 0) cpuBindType = CPU_BIND_LDMASK;
    else if (strcmp(argv[7], "boards") == 0) cpuBindType = CPU_BIND_TO_BOARDS;
    else if (strcmp(argv[7], "sockets") == 0) cpuBindType = CPU_BIND_TO_SOCKETS;
    else if (strcmp(argv[7], "ldoms") == 0) cpuBindType = CPU_BIND_TO_LDOMS;
    else if (strcmp(argv[7], "cores") == 0) cpuBindType = CPU_BIND_TO_CORES;
    else if (strcmp(argv[7], "threads") == 0) cpuBindType = CPU_BIND_TO_THREADS;
    else if (strcmp(argv[7], "ldrank") == 0) cpuBindType = CPU_BIND_LDRANK;
    else if (strcmp(argv[7], "rank") == 0) cpuBindType = CPU_BIND_RANK;
    else {
	printf("Unknown bind type: '%s'.\n", argv[7]);
	return -1;
    }

    cpuBindString = argv[8];

    if (strcmp(argv[9], "") == 0) taskDist = 0;
    else if (strcmp(argv[9], "cyclic") == 0) taskDist = SLURM_DIST_BLOCK_CYCLIC;
    else if (strcmp(argv[9], "cyclic:cyclic") == 0) taskDist = SLURM_DIST_BLOCK_CYCLIC_CYCLIC;
    else if (strcmp(argv[9], "cyclic:block") == 0) taskDist = SLURM_DIST_BLOCK_CYCLIC_BLOCK;
    else if (strcmp(argv[9], "cyclic:fcyclic") == 0) taskDist = SLURM_DIST_BLOCK_CYCLIC_CFULL;
    else if (strcmp(argv[9], "block") == 0) taskDist = SLURM_DIST_BLOCK_BLOCK;
    else if (strcmp(argv[9], "block:cyclic") == 0) taskDist = SLURM_DIST_BLOCK_BLOCK_CYCLIC;
    else if (strcmp(argv[9], "block:block") == 0) taskDist = SLURM_DIST_BLOCK_BLOCK_BLOCK;
    else if (strcmp(argv[9], "block:fcyclic") == 0) taskDist = SLURM_DIST_BLOCK_BLOCK_CFULL;
    else if (strcmp(argv[9], "fcyclic") == 0) taskDist = SLURM_DIST_BLOCK_CFULL;
    else if (strcmp(argv[9], "fcyclic:cyclic") == 0) taskDist = SLURM_DIST_BLOCK_CFULL_CYCLIC;
    else if (strcmp(argv[9], "fcyclic:block") == 0) taskDist = SLURM_DIST_BLOCK_CFULL_BLOCK;
    else if (strcmp(argv[9], "fcyclic:fcyclic") == 0) taskDist = SLURM_DIST_BLOCK_CFULL_CFULL;
    else {
	printf("Unknown distribution: '%s'.\n", argv[9]);
	return -1;
    }

    if (atoi(argv[10]) != 0) cpuBindType |= CPU_BIND_ONE_THREAD_PER_CORE;


    printf("PINNING TEST\n");
    printf("cpuBindType = 0x%X - cpuBindString = \"%s\"\n", cpuBindType,
	    cpuBindString);
    printf("taskDist = 0x%X\n", taskDist);
    printf("node: %d sockets, %d cores per socket, %d threads per core\n",
	    socketCount, coresPerSocket, threadsPerCore);
    printf("job: %u tasks, %d threads per task\n", tasksPerNode,
	    threadsPerTask);
    printf("\n");

    test_pinning(cpuBindType, cpuBindString, taskDist, socketCount,
	    coresPerSocket, threadsPerCore, tasksPerNode, threadsPerTask, true);

    return 0;
}

int iteration(int argc, char *argv[])
{
    /* node info */
    uint16_t socketCount = 3;
    uint16_t coresPerSocket = 5;
    uint16_t threadsPerCore = 3;

    /* strategy info */
    uint8_t strategy = 0;

    if (argc != 6) {
	printf("Usage: test_psslurmpin iteration <sockets> <coresPerSocket>"
		" <threadsPerCore> <strategy>\n");
	return -1;
    }

    socketCount = atoi(argv[2]);
    coresPerSocket = atoi(argv[3]);
    threadsPerCore = atoi(argv[4]);

    strategy = atoi(argv[5]);

    printf("ITERATION TEST\n");
    printf("node: %d sockets, %d cores per socket, %d threads per core\n",
	    socketCount, coresPerSocket, threadsPerCore);
    printf("strategy: %hhu\n", strategy);
    printf("\n");


    test_thread_iterator(socketCount, coresPerSocket, threadsPerCore,
	    strategy);

    return 0;
}

int main(int argc, char *argv[])
{

    if (argc < 2) {
	printf("First argument has to be \"pinning\" or \"iteration\".\n");
	return -1;
    }

    /* select function */
    if (strcmp(argv[1], "pinning") == 0) return pinning(argc, argv);
    else if (strcmp(argv[1], "iteration") == 0) return iteration(argc, argv);
    else {
	printf("First argument has to be \"pinning\" or \"iteration\".\n");
	return -1;
    }
}


#ifdef VERBOSE
static logger_t lt;
logger_t *psslurmlogger = &lt;
#else
logger_t *psslurmlogger = NULL;
#endif
logger_t *pluginlogger = NULL;

void logger_print(logger_t* logger, int32_t key, const char* format, ...) {
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
