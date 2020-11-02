/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>

#include "list.h"

#include "pscommon.h"
#include "psprotocol.h"

#include "psidutil.h"
#include "psidcomm.h"

#include "psidnodes.h"

/** Number of nodes currently handled. Adapted within PSIDnodes_grow() */
static PSnodes_ID_t numNodes = -1;

/** Maximum ID currently in use, i.e. registered via PSIDnodes_register() */
static PSnodes_ID_t maxID = -1;

/** Hashed host table for reverse lookup (ip-addr given, determine id) */
struct host_t {
    in_addr_t addr;
    int id;
    struct host_t *next;
};

/** Array (indexed by hashes) to store all known hosts */
static struct host_t *hosts[256];

/** Structure holding all known info available concerning a specific node */
typedef struct {
    in_addr_t addr;        /**< IP address of that node */
    int protoVer;          /**< Node's PSprotocol version */
    int daemonProtoVer;    /**< Node's PSDaemonprotocol version */
    short numCores;        /**< Number of physical processor cores */
    short numThrds;        /**< Number of hardware threads */
    bool isUp;             /**< Actual status of that node */
    unsigned int hwType;   /**< Communication hardware on that node */
    unsigned int hwStatus; /**< Corresponding statuses of the hardware */
    in_addr_t extraIP;     /**< Additional IP address of that node */
    char runJobs;          /**< Flag to mark that node to run jobs */
    char isStarter;        /**< Flag to allow to start jobs from that node */
    char overbooking;      /**< Flag to allow overbooking that node */
    char exclusive;        /**< Flag to assign this node exclusively */
    char pinProcs;         /**< Flag to mark that node to pin processes */
    char bindMem;          /**< Flag to mark that node to bind memory */
    char bindGPUs;         /**< Flag to mark that node to bind GPUs */
    short *CPUmap;         /**< Map virt. procs. slots to hardware threads */
    size_t CPUmapSize;     /**< Current size of @ref CPUmap */
    size_t CPUmapMaxSize;  /**< Allocated size of @ref CPUmap */
    char allowUserMap;     /**< Flag to allow users to influence mapping */
    list_t uid_list;       /**< Users this node is reserved to */
    list_t gid_list;       /**< Groups this node is reserved to */
    list_t admuid_list;    /**< AdminUser on this node */
    list_t admgid_list;    /**< AdminGroup on this node */
    int maxProcs;          /**< Number of processes this node will handle */
    int acctPollInterval;  /**< Interval in sec for polling on accnting info */
    int killDelay;         /**< Seconds between relatives' signal and SIGKILL */
    char supplGrps;        /**< Set supplementary groups for new tasks */
    char maxStatTry;       /**< Number of tries to stat() executable to spawn */
} node_t;

/** Array (indexed by node number) to store all known nodes */
static node_t *nodes = NULL;

/**
 * @brief Init node structure
 *
 * Initialize the node structure @a node.
 *
 * @param node The node to initialize.
 *
 * @return No return value.
 */
static void nodeInit(node_t *node)
{
    node->addr = INADDR_ANY;
    node->protoVer = 0;
    node->daemonProtoVer = 0;
    node->numCores = 0;
    node->numThrds = 0;
    node->isUp = false;
    node->hwType = 0;
    node->hwStatus = 0;
    node->extraIP = INADDR_ANY;
    node->runJobs = 0;
    node->isStarter = 0;
    node->pinProcs = 0;
    node->bindMem = 0;
    node->bindGPUs = 0;
    node->CPUmap = NULL;
    node->CPUmapSize = 0;
    node->CPUmapMaxSize = 0;
    node->allowUserMap = 0;
    INIT_LIST_HEAD(&node->uid_list);
    INIT_LIST_HEAD(&node->gid_list);
    INIT_LIST_HEAD(&node->admuid_list);
    INIT_LIST_HEAD(&node->admgid_list);
    node->maxProcs = -1;
    node->acctPollInterval = 0;
    node->killDelay = 0;
    node->supplGrps = 0;
    node->maxStatTry = 1;
}

static void initHash(void)
{
    unsigned int i;

    for (i=0; i < sizeof(hosts)/sizeof(*hosts); i++) hosts[i] = NULL;
}

static inline void fixList(list_t *list, list_t *oldHead)
{
    if (list->next == oldHead) {
	/* list was empty */
	INIT_LIST_HEAD(list);
    } else {
	/* fix reverse pointers */
	list->next->prev = list;
	list->prev->next = list;
    }
}

int PSIDnodes_grow(PSnodes_ID_t num)
{
    PSnodes_ID_t oldNum = PSIDnodes_getNum();
    node_t *oldNodes = nodes;
    int i;

    if (PSIDnodes_getNum() >= num) return 0; /* don't shrink */

    if (PSIDnodes_getNum() < 0) {
	initHash();
	oldNum = 0;
    }

    numNodes = num;

    nodes = realloc(nodes, sizeof(*nodes) * numNodes);
    if (!nodes) {
	PSID_warn(-1, ENOMEM, "%s", __func__);
	nodes=oldNodes;
	numNodes = oldNum;
	return -1;
    }

    /* Restore old lists if necessary */
    if (nodes != oldNodes) {
	for (i=0; i<oldNum; i++) {
	    fixList(&nodes[i].uid_list, &oldNodes[i].uid_list);
	    fixList(&nodes[i].gid_list, &oldNodes[i].gid_list);
	    fixList(&nodes[i].admuid_list, &oldNodes[i].admuid_list);
	    fixList(&nodes[i].admgid_list, &oldNodes[i].admgid_list);
	}
    }

    /* Initialize new nodes */
    for (i=oldNum; i<numNodes; i++) {
	nodeInit(&nodes[i]);
    }

    return 0;
}

int PSIDnodes_init(PSnodes_ID_t num)
{
    return PSIDnodes_grow(num);
}

PSnodes_ID_t PSIDnodes_getNum(void)
{
    return numNodes;
}

PSnodes_ID_t PSIDnodes_getMaxID(void)
{
    return maxID;
}

static bool validID(PSnodes_ID_t id)
{
    if (PSIDnodes_getNum() == -1 || id < 0 || id >= PSIDnodes_getNum()) {
	/* id out of Range */
	return false;
    }

    return true;
}

#define GROW_CHUNK 64

int PSIDnodes_register(PSnodes_ID_t id, in_addr_t addr)
{
    unsigned int hostno;
    struct host_t *host;

    if (id < 0) {
	return -1;
    }

    if (PSIDnodes_lookupHost(addr)!=-1) {
	/* duplicated host */
	return -1;
    }

    if (PSIDnodes_getAddr(id) != INADDR_ANY) { /* duplicated PS-ID */
	return -1;
    }

    if (id >= PSIDnodes_getNum()
	&& PSIDnodes_grow(GROW_CHUNK*(id/GROW_CHUNK + 1)) == -1) {
	/* failed to grow nodes */		\
	PSID_log(-1, "%s(id=%d): failed to grow nodes\n", __func__, id);
	return -1;
    }

    hostno = ntohl(addr) & 0xff;

    host = (struct host_t*) malloc(sizeof(struct host_t));
    if (!host) {
	PSID_warn(-1, ENOMEM, "%s", __func__);
	return -1;
    }

    host->addr = addr;
    host->id = id;
    host->next = hosts[hostno];
    hosts[hostno] = host;

    /* install hostname */
    nodes[id].addr = addr;

    if (id > PSIDnodes_getMaxID()) maxID = id;

    return 0;
}

PSnodes_ID_t PSIDnodes_lookupHost(in_addr_t addr)
{
    unsigned int hostno;
    struct host_t *host;

    if (PSIDnodes_getNum() < 0) return -1;

    /* loopback address */
    if ((ntohl(addr) >> 24 ) == IN_LOOPBACKNET)
	return PSC_getMyID();

    /* other addresses */
    hostno = ntohl(addr) & 0xff;
    for (host = hosts[hostno]; host; host = host->next) {
	if (host->addr == addr) {
	    return host->id;
	}
    }

    return -1;
}

in_addr_t PSIDnodes_getAddr(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].addr;
    } else {
	return INADDR_ANY;
    }
}

int PSIDnodes_bringUp(PSnodes_ID_t id)
{
    if (validID(id)) {
	nodes[id].isUp = true;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_bringDown(PSnodes_ID_t id)
{
    if (validID(id)) {
	nodes[id].isUp = false;
	return 0;
    } else {
	return -1;
    }
}

bool PSIDnodes_isUp(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].isUp;
    } else {
	return false;
    }
}

/**********************************************************************/
int PSIDnodes_setProtoV(PSnodes_ID_t id, int version)
{
    if (validID(id)) {
	nodes[id].protoVer = version;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getProtoV(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].protoVer;
    } else {
	return -1;
    }
}

int PSIDnodes_setDmnProtoV(PSnodes_ID_t id, int version)
{
    if (validID(id)) {
	nodes[id].daemonProtoVer = version;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getDmnProtoV(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].daemonProtoVer;
    } else {
	return -1;
    }
}

/**********************************************************************/

int PSIDnodes_setHWType(PSnodes_ID_t id, int hwType)
{
    if (validID(id)) {
	nodes[id].hwType = hwType;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getHWType(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].hwType;
    } else {
	return -1;
    }
}

int PSIDnodes_setRunJobs(PSnodes_ID_t id, int runjobs)
{
    if (validID(id)) {
	nodes[id].runJobs = runjobs;
	return 0;
    } else {
	return -1;
    }
}


int PSIDnodes_runJobs(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].runJobs;
    } else {
	return -1;
    }
}

int PSIDnodes_setIsStarter(PSnodes_ID_t id, int starter)
{
    if (validID(id)) {
	nodes[id].isStarter = starter;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_isStarter(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].isStarter;
    } else {
	return -1;
    }
}

int PSIDnodes_setOverbook(PSnodes_ID_t id, PSnodes_overbook_t overbook)
{
    if (validID(id)) {
	nodes[id].overbooking = overbook;
	return 0;
    } else {
	return -1;
    }
}

PSnodes_overbook_t PSIDnodes_overbook(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].overbooking;
    } else {
	return -1;
    }
}

int PSIDnodes_setExclusive(PSnodes_ID_t id, int exclusive)
{
    if (validID(id)) {
	nodes[id].exclusive = exclusive;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_exclusive(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].exclusive;
    } else {
	return -1;
    }
}

int PSIDnodes_setPinProcs(PSnodes_ID_t id, int pinProcs)
{
    if (validID(id)) {
	nodes[id].pinProcs = pinProcs;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_pinProcs(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].pinProcs;
    } else {
	return -1;
    }
}

int PSIDnodes_setBindMem(PSnodes_ID_t id, int bindMem)
{
    if (validID(id)) {
	nodes[id].bindMem = bindMem;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_bindMem(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].bindMem;
    } else {
	return -1;
    }
}

int PSIDnodes_setBindGPUs(PSnodes_ID_t id, int bindGPUs)
{
    if (validID(id)) {
	nodes[id].bindGPUs = bindGPUs;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_bindGPUs(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].bindGPUs;
    } else {
	return -1;
    }
}

short PSIDnodes_mapCPU(PSnodes_ID_t id, short cpu)
{
    if (validID(id) && cpu >= 0 && (unsigned)cpu<nodes[id].CPUmapSize
	&& cpu < PSIDnodes_getNumThrds(id)) {
	return nodes[id].CPUmap[cpu];
    } else {
	return -1;
    }
}

short PSIDnodes_unmapCPU(PSnodes_ID_t id, short hwthread)
{
    if (validID(id)) {
	for (short i = 0; (unsigned)i < nodes[id].CPUmapSize; i++) {
	    if (nodes[id].CPUmap[i] == hwthread) return i;
	}
    }
    return -1;
}

int PSIDnodes_clearCPUMap(PSnodes_ID_t id)
{
    if (validID(id)) {
	nodes[id].CPUmapSize = 0;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_appendCPUMap(PSnodes_ID_t id, short cpu)
{
    if (!validID(id)) {
	return -1;
    }
    if (nodes[id].CPUmapSize == nodes[id].CPUmapMaxSize) {
	if (nodes[id].CPUmapMaxSize) {
	    nodes[id].CPUmapMaxSize *= 2;
	} else {
	    nodes[id].CPUmapMaxSize = 8;
	}
	nodes[id].CPUmap = realloc(nodes[id].CPUmap, nodes[id].CPUmapMaxSize
				   * sizeof(*nodes[id].CPUmap));
	if (!nodes[id].CPUmap) PSID_exit(ENOMEM, "%s", __func__);
    }
    nodes[id].CPUmap[nodes[id].CPUmapSize] = cpu;
    nodes[id].CPUmapSize++;

    return 0;
}

void send_CPUMap_OPTIONS(PStask_ID_t dest)
{
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg) },
	.count = 0,
	.opt = {{ .option = 0, .value = 0 }} };
    node_t *myNode = &nodes[PSC_getMyID()];
    short *CPUmap = myNode->CPUmap;
    int i, mapEntries = (int)myNode->CPUmapSize < myNode->numThrds ?
	(int)myNode->CPUmapSize : myNode->numThrds;

    PSID_log(PSID_LOG_VERB, "%s: %s", __func__, PSC_printTID(dest));

    for (i=0; i<mapEntries; i++) {
	msg.opt[(int) msg.count].option = PSP_OP_CPUMAP;
	msg.opt[(int) msg.count].value = CPUmap[i];

	msg.count++;
	if (msg.count == DDOptionMsgMax) {
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    }
	    msg.count = 0;
	}
    }

    msg.opt[(int) msg.count].option = PSP_OP_LISTEND;
    msg.opt[(int) msg.count].value = 0;
    msg.count++;
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }
}

int PSIDnodes_setAllowUserMap(PSnodes_ID_t id, int allowMap)
{
    if (validID(id)) {
	nodes[id].allowUserMap = allowMap;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_allowUserMap(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].allowUserMap;
    } else {
	return -1;
    }
}


int PSIDnodes_setExtraIP(PSnodes_ID_t id, in_addr_t addr)
{
    if (validID(id)) {
	nodes[id].extraIP = addr;
	return 0;
    } else {
	return -1;
    }
}

in_addr_t PSIDnodes_getExtraIP(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].extraIP;
    } else {
	return -1;
    }
}

int PSIDnodes_setNumCores(PSnodes_ID_t id, short numCores)
{
    if (validID(id)) {
	nodes[id].numCores = numCores;
	return 0;
    } else {
	return -1;
    }
}

short PSIDnodes_getNumCores(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].numCores;
    } else {
	return -1;
    }
}

int PSIDnodes_setNumThrds(PSnodes_ID_t id, short numThrds)
{
    if (validID(id)) {
	nodes[id].numThrds = numThrds;
	return 0;
    } else {
	return -1;
    }
}

short PSIDnodes_getNumThrds(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].numThrds;
    } else {
	return -1;
    }
}

int PSIDnodes_setHWStatus(PSnodes_ID_t id, int hwStatus)
{
    if (validID(id)) {
	nodes[id].hwStatus = hwStatus;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getHWStatus(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].hwStatus;
    } else {
	return -1;
    }
}

/** List type to store group/user entries */
typedef struct {
    struct list_head next;
    PSIDnodes_guid_t id;
} PSIDnodes_GUent_t;

/**
 * @brief Compare GUIDs
 *
 * Compare GUIDs @a guid1 and @a guid2 of type @a what.
 *
 * @param what The GUID's internal type.
 *
 * @param guid1 One GUID.
 *
 * @param guid2 The other GUID.
 *
 * @return If the GUIDs are identical, 0 is returned. Otherwise 1 is
 * returned.
 */
static inline int cmp_GUID(PSIDnodes_gu_t what,
			   PSIDnodes_guid_t guid1, PSIDnodes_guid_t guid2)
{
    switch (what) {
    case PSIDNODES_USER:
    case PSIDNODES_ADMUSER:
	if (guid1.u == guid2.u) return 0; else return 1;
	break;
    case PSIDNODES_GROUP:
    case PSIDNODES_ADMGROUP:
	if (guid1.g == guid2.g) return 0; else return 1;
	break;
    }
    return 1;
}

/**
 * @brief Fetch GUID list
 *
 * Determine to GUID list holding the GUIDs of type @a what for the
 * node with ID @a id.
 *
 * @param id ParaStation ID of the node of interest.
 *
 * @param what The type of GUIDs of interest.
 *
 * @return On success, i.e. if @a id is valid and @a what is known, a
 * pointer to the list is returned. Or NULL otherwise.
 */
static list_t * get_GUID_list(PSnodes_ID_t id, PSIDnodes_gu_t what)
{
    if (!validID(id)) return NULL;

    switch (what) {
    case PSIDNODES_USER:
	return &nodes[id].uid_list;
	break;
    case PSIDNODES_GROUP:
	return &nodes[id].gid_list;
	break;
    case PSIDNODES_ADMUSER:
	return &nodes[id].admuid_list;
	break;
    case PSIDNODES_ADMGROUP:
	return &nodes[id].admgid_list;
	break;
    }

    return NULL;
}


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
static void clear_GUID_list(list_t *list)
{
    list_t *pos, *tmp;

    PSID_log(PSID_LOG_VERB, "%s(%p)\n", __func__, list);

    list_for_each_safe(pos, tmp, list) {
	PSIDnodes_GUent_t *guent = list_entry(pos, PSIDnodes_GUent_t, next);
	list_del(pos);
	free(guent);
    }
}

int PSIDnodes_setGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid)
{
    list_t *list = get_GUID_list(id, what);

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid.u);

    if (!list) return -1;

    clear_GUID_list(list);

    return PSIDnodes_addGUID(id, what, guid);
}

int PSIDnodes_addGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid)
{
    PSIDnodes_GUent_t *guent;
    PSIDnodes_guid_t any;
    list_t *list = get_GUID_list(id, what), *pos, *tmp;

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid.u);

    if (!list) return -1;

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

    if (!cmp_GUID(what, guid, any)) clear_GUID_list(list);

    list_for_each_safe(pos, tmp, list) {
	guent = list_entry(pos, PSIDnodes_GUent_t, next);
	if (!cmp_GUID(what, guent->id, any)) {
	    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d): ANY found\n",
		     __func__, id, what, guid.u);
	    return -1;
	}
	if (!cmp_GUID(what, guent->id, guid)) {
	    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d): already there\n",
		     __func__, id, what, guid.u);
	    return -1;
	}
    }

    guent = malloc(sizeof(*guent));
    if (!guent) PSID_exit(ENOMEM, "%s", __func__);
    guent->id = guid;
    list_add_tail(&guent->next, list);

    return 0;
}

int PSIDnodes_remGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid)
{
    list_t *list = get_GUID_list(id, what), *pos, *tmp;

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid.u);

    list_for_each_safe(pos, tmp, list) {
	PSIDnodes_GUent_t *guent = list_entry(pos, PSIDnodes_GUent_t, next);
	if (!cmp_GUID(what, guent->id, guid)) {
	    list_del(pos);
	    free(guent);
	    return 0;
	}
    }

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d): not found\n", __func__,
	     id, what, guid.u);

    return -1;
}

int PSIDnodes_testGUID(PSnodes_ID_t id,
		       PSIDnodes_gu_t what, PSIDnodes_guid_t guid)
{
    list_t *list = get_GUID_list(id, what), *pos;
    PSIDnodes_guid_t any;

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid.u);

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

    list_for_each(pos, list) {
	PSIDnodes_GUent_t *guent = list_entry(pos, PSIDnodes_GUent_t, next);
	if (!cmp_GUID(what, guent->id, guid)
	    || !cmp_GUID(what, guent->id, any)) return 1;
    }

    return 0;
}

void send_GUID_OPTIONS(PStask_ID_t dest, PSIDnodes_gu_t what)
{
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg) },
	.count = 0,
	.opt = {{ .option = 0, .value = 0 }} };
    list_t *list = get_GUID_list(PSC_getMyID(), what), *pos;
    PSP_Option_t option = PSP_OP_UNKNOWN;

    PSID_log(PSID_LOG_VERB, "%s: %s", __func__, PSC_printTID(dest));
    switch (what) {
    case PSIDNODES_USER:
	PSID_log(PSID_LOG_VERB, " %s", "PSIDNODES_USER");
	if (PSC_getPID(dest)) {
	    msg.opt[(int) msg.count].option = option = PSP_OP_UID;
	} else {
	    msg.opt[(int) msg.count].option = PSP_OP_SET_UID;
	    option = PSP_OP_ADD_UID;
	}
	break;
    case PSIDNODES_GROUP:
	PSID_log(PSID_LOG_VERB, " %s", "PSIDNODES_GROUP");
	if (PSC_getPID(dest)) {
	    msg.opt[(int) msg.count].option = option = PSP_OP_GID;
	} else {
	    msg.opt[(int) msg.count].option = PSP_OP_SET_GID;
	    option = PSP_OP_ADD_GID;
	}
	break;
    case PSIDNODES_ADMUSER:
	PSID_log(PSID_LOG_VERB, " %s", "PSIDNODES_ADMUSER");
	if (PSC_getPID(dest)) {
	    msg.opt[(int) msg.count].option = option = PSP_OP_ADMUID;
	} else {
	    msg.opt[(int) msg.count].option = PSP_OP_SET_ADMUID;
	    option = PSP_OP_ADD_ADMUID;
	}
	break;
    case PSIDNODES_ADMGROUP:
	PSID_log(PSID_LOG_VERB, " %s", "PSIDNODES_ADMGROUP");
	if (PSC_getPID(dest)) {
	    msg.opt[(int) msg.count].option = option = PSP_OP_ADMGID;
	} else {
	    msg.opt[(int) msg.count].option = PSP_OP_SET_ADMGID;
	    option = PSP_OP_ADD_ADMGID;
	}
	break;
    default:
	PSID_log(PSID_LOG_VERB, " unknown");
	return;
    }

    list_for_each(pos, list) {
	PSIDnodes_GUent_t *guent = list_entry(pos, PSIDnodes_GUent_t, next);

	msg.opt[(int) msg.count].option = option;
	switch (what) {
	case PSIDNODES_USER:
	case PSIDNODES_ADMUSER:
	    msg.opt[(int) msg.count].value = guent->id.u;
	    break;
	case PSIDNODES_GROUP:
	case PSIDNODES_ADMGROUP:
	    msg.opt[(int) msg.count].value = guent->id.u;
	    break;
	default:
	    return;
	}
	msg.count++;
	if (msg.count == DDOptionMsgMax) {
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    }
	    msg.count = 0;
	}
    }
    msg.opt[(int) msg.count].option = PSP_OP_LISTEND;
    msg.opt[(int) msg.count].value = 0;
    msg.count++;

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }
}

int PSIDnodes_setProcs(PSnodes_ID_t id, int procs)
{
    if (validID(id)) {
	nodes[id].maxProcs = procs;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getProcs(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].maxProcs;
    } else {
	return -1;
    }
}

int PSIDnodes_setAcctPollI(PSnodes_ID_t id, int interval)
{
    if (validID(id)) {
	nodes[id].acctPollInterval = interval;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_acctPollI(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].acctPollInterval;
    } else {
	return -1;
    }
}

int PSIDnodes_setKillDelay(PSnodes_ID_t id, int delay)
{
    if (validID(id) && delay >= 0) {
	nodes[id].killDelay = delay;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_killDelay(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].killDelay;
    } else {
	return -1;
    }
}

int PSIDnodes_setSupplGrps(PSnodes_ID_t id, int supplGrps)
{
    if (validID(id)) {
	nodes[id].supplGrps = supplGrps;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_supplGrps(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].supplGrps;
    } else {
	return -1;
    }
}

int PSIDnodes_setMaxStatTry(PSnodes_ID_t id, int tries)
{
    if (validID(id)) {
	nodes[id].maxStatTry = tries;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_maxStatTry(PSnodes_ID_t id)
{
    if (validID(id)) {
	return nodes[id].maxStatTry;
    } else {
	return -1;
    }
}

void PSIDnodes_clearMem(void)
{
    int h, n;

    for (h=0; h<256; h++) {
	struct host_t *host = hosts[h];
	while (host) {
	    struct host_t *next = host->next;
	    free(host);
	    host = next;
	}
    }

    for (n=0; n<PSIDnodes_getNum(); n++) {
	clear_GUID_list(&nodes[n].uid_list);
	clear_GUID_list(&nodes[n].gid_list);
	clear_GUID_list(&nodes[n].admuid_list);
	clear_GUID_list(&nodes[n].admgid_list);
	if (nodes[n].CPUmap) free(nodes[n].CPUmap);
    }

    free(nodes);
    nodes = NULL;
}
