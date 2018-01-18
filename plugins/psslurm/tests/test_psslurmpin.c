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

typedef struct {
    uint8_t *usedHwThreads;   /* boolean array of hardware threads already assigned */
    int16_t lastSocket;       /* number of the socket used last */
} pininfo_t;

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

int main(int argc, char *argv[])
{
    int i;

    /* node info */
    uint16_t socketCount = 3;
    uint16_t coresPerSocket = 5;
    uint16_t threadsPerCore = 3;
    uint32_t threadCount = socketCount * coresPerSocket * threadsPerCore;

    /* task info */
    uint32_t tasksPerNode = 3;
    uint16_t threadsPerTask = 12;

    /* pinning info */
    uint16_t cpuBindType = CPU_BIND_LDRANK;
    char *cpuBindString = "";
    uint8_t *coreMap = NULL;
    uint32_t coreMapIndex = 0;

    /* temporary variables */
    uint32_t local_tid;
    int32_t lastCpu;
    int thread;
    pininfo_t pininfo;

    /* output variables */
    PSCPU_set_t CPUset;

    if (argc != 9) {
	printf("Usage: test_psslurmpin <sockets> <coresPerSocket>"
		" <threadsPerCore> <tasks> <threadsPerTask> <bindType>"
		" <bindString> <oneThreadPerCore>\n");
	return -1;
    }

    socketCount = atoi(argv[1]);
    coresPerSocket = atoi(argv[2]);
    threadsPerCore = atoi(argv[3]);
    threadCount = socketCount * coresPerSocket * threadsPerCore;

    /* task info */
    tasksPerNode = atoi(argv[4]);
    threadsPerTask = atoi(argv[5]);

    /* pinning info */
    if (strcmp(argv[6], "none") == 0) cpuBindType = CPU_BIND_NONE;
    else if (strcmp(argv[6], "map") == 0) cpuBindType = CPU_BIND_MAP;
    else if (strcmp(argv[6], "mask") == 0) cpuBindType = CPU_BIND_MASK;
    else if (strcmp(argv[6], "ldmap") == 0) cpuBindType = CPU_BIND_LDMAP;
    else if (strcmp(argv[6], "ldmask") == 0) cpuBindType = CPU_BIND_LDMASK;
    else if (strcmp(argv[6], "sockets") == 0) cpuBindType = CPU_BIND_TO_SOCKETS;
    else if (strcmp(argv[6], "ldoms") == 0) cpuBindType = CPU_BIND_TO_LDOMS;
    else if (strcmp(argv[6], "ldrank") == 0) cpuBindType = CPU_BIND_LDRANK;
    else if (strcmp(argv[6], "rank") == 0) cpuBindType = CPU_BIND_RANK;
    else {
	printf("Unknown bind type: '%s'.\n", argv[6]);
	return -1;
    }

    cpuBindString = argv[7];

    if (atoi(argv[8]) != 0) {
	cpuBindType |= CPU_BIND_ONE_THREAD_PER_CORE;
    }

    coreMap = malloc(threadCount * sizeof(*coreMap));
    for (i = 0; i < threadCount; coreMap[i++] = 1);


    printf("PINNING TEST\n");
    printf("cpuBindType = 0x%X - cpuBindString = \"%s\"\n", cpuBindType,
	    cpuBindString);
    printf("node: %d sockets, %d cores per socket, %d threads per core\n",
	    socketCount, coresPerSocket, threadsPerCore);
    printf("job: %d tasks, %d threads per task\n", tasksPerNode,
	    threadsPerTask);
    printf("flags: oneThreadPerCore %s\n", atoi(argv[8]) != 0 ? "set" : "unset");
    printf("\n");

    lastCpu = -1; /* no cpu assigned yet */
    thread = 0;

    /* initialize pininfo struct (currently only used for RANK_LDOM) */
    pininfo.usedHwThreads = calloc(
	    socketCount * coresPerSocket * threadsPerCore,
	    sizeof(*pininfo.usedHwThreads));
    pininfo.lastSocket = -1;

    /* set node and cpuset for every task on this node */
    for (local_tid=0; local_tid < tasksPerNode; local_tid++) {

        PSCPU_clrAll(CPUset);

	/* calc CPUset */
	test_pinning(&CPUset, cpuBindType, cpuBindString, coreMap,
		coreMapIndex, socketCount, coresPerSocket, threadsPerCore,
		tasksPerNode, threadsPerTask, local_tid, &lastCpu, &thread,
		&pininfo);

	printf("%2d: ", local_tid);
	for (i = 0; i < threadCount; i++) {
	    if (i % coresPerSocket == 0) printf(" ");
	    if (i % (socketCount * coresPerSocket) == 0) printf("\n    ");
	    printf("%d", PSCPU_isSet(CPUset, i));
	}
	printf("\n");

    }

    free(coreMap);
    free(pininfo.usedHwThreads);

    return 0;

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
