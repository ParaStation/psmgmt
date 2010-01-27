/*
 *               ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

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


/** Number of nodes currently handled. Set within PSIDnodes_init() */
static PSnodes_ID_t numNodes = -1;

/** Hashed host table for reverse lookup (ip-addr given, determine id) */
struct host_t {
    in_addr_t addr;
    int id;
    struct host_t *next;
};

/** Array (indexed by hashes) to store all known hosts */
static struct host_t *hosts[256];

/** Structure holding all known info available concerning a special node */
typedef struct {
    in_addr_t addr;        /**< IP address of that node */
    int protoVer;          /**< Node's PSprotocol version */
    int daemonProtoVer;    /**< Node's PSDaemonprotocol version */
    int version;           /**< Version of the config info from that node */
    short physCPU;         /**< Number of physical CPUs in that node */
    short virtCPU;         /**< Number of virtual CPUs in that node */
    char isUp;             /**< Actual status of that node */
    unsigned int hwType;   /**< Communication hardware on that node */
    unsigned int hwStatus; /**< Corresponding stati of the hardware */
    in_addr_t extraIP;     /**< Additional IP address of that node */
    char runJobs;          /**< Flag to mark that node to run jobs */
    char isStarter;        /**< Flag to allow to start jobs from that node */
    char overbooking;      /**< Flag to allow overbooking that node */
    char exclusive;        /**< Flag to assign this node exclusively */
    char pinProcs;         /**< Flag to mark that node to pin processes */
    char bindMem;          /**< Flag to mark that node to bind memory */
    short *CPUmap;         /**< Map to match virt. CPU slots to phys. cores */
    size_t CPUmapSize;     /**< Current size of @ref CPUmap */
    size_t CPUmapMaxSize;  /**< Allocated size of @ref CPUmap */
    list_t uid_list;       /**< Users this node is reserved to */
    list_t gid_list;       /**< Groups this node is reserved to */
    list_t admuid_list;    /**< AdminUser on this node */
    list_t admgid_list;    /**< AdminGroup on this node */
    int maxProcs;          /**< Number of processes this node will handle */
    int acctPollInterval;  /**< Interval in sec for polling on accnting info */
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
    node->version = 0;
    node->physCPU = 0;
    node->virtCPU = 0;
    node->isUp = 0;
    node->hwType = 0;
    node->hwStatus = 0;
    node->extraIP = INADDR_ANY;
    node->runJobs = 0;
    node->isStarter = 0;
    node->pinProcs = 0;
    node->bindMem = 0;
    node->CPUmap = NULL;
    node->CPUmapSize = 0;
    node->CPUmapMaxSize = 0;
    node->uid_list = LIST_HEAD_INIT(node->uid_list);
    node->gid_list = LIST_HEAD_INIT(node->gid_list);
    node->admuid_list = LIST_HEAD_INIT(node->admuid_list);
    node->admgid_list = LIST_HEAD_INIT(node->admgid_list);
    node->maxProcs = -1;
    node->acctPollInterval = 0;
    node->supplGrps = 0;
    node->maxStatTry = 1;
}

int PSIDnodes_init(PSnodes_ID_t num)
{
    int i;

    if (nodes) free(nodes);

    numNodes = num;

    nodes = malloc(sizeof(*nodes) * numNodes);

    if (!nodes) {
	PSID_warn(-1, ENOMEM, "%s", __func__);
	return -1;
    }

    /* Clear nodes */
    for (i=0; i<numNodes; i++) nodeInit(&nodes[i]);

    return 0;
}

PSnodes_ID_t PSIDnodes_getNum(void)
{
    return numNodes;
}

int PSIDnodes_validID(PSnodes_ID_t id)
{
    if (PSIDnodes_getNum() == -1 || id < 0 || id >= PSIDnodes_getNum()) {
	/* id out of Range */
	return 0;
    }

    return 1;
}

int PSIDnodes_register(PSnodes_ID_t id, in_addr_t addr)
{
    unsigned int hostno;
    struct host_t *host;

    if (! PSIDnodes_validID(id)) {
	return -1;
    }

    if (PSIDnodes_lookupHost(addr)!=-1) {
	/* duplicated host */
	return -1;
    }

    if (PSIDnodes_getAddr(id) != INADDR_ANY) { /* duplicated PS-ID */
	return -1;
    }

    /* install hostname */
    nodes[id].addr = addr;

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

    return 0;
}

PSnodes_ID_t PSIDnodes_lookupHost(in_addr_t addr)
{
    unsigned int hostno;
    struct host_t *host;

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
    if (PSIDnodes_validID(id)) {
	return nodes[id].addr;
    } else {
	return -1;
    }
}

int PSIDnodes_bringUp(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].isUp = 1;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_bringDown(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].isUp = 0;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_isUp(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].isUp;
    } else {
	return 0;
    }
}

/**********************************************************************/
/* @todo This does not really make sense, but is a good start.
   Actually each piece of information needs its own version number */
int PSIDnodes_setInfoVersion(PSnodes_ID_t id, int version)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].version = version;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getInfoVersion(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].version;
    } else {
	return -1;
    }
}

/**********************************************************************/
int PSIDnodes_setProtoVersion(PSnodes_ID_t id, int version)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].protoVer = version;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getProtoVersion(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].protoVer;
    } else {
	return -1;
    }
}

int PSIDnodes_setDaemonProtoVersion(PSnodes_ID_t id, int version)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].daemonProtoVer = version;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getDaemonProtoVersion(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].daemonProtoVer;
    } else {
	return -1;
    }
}

/**********************************************************************/

int PSIDnodes_setHWType(PSnodes_ID_t id, int hwType)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].hwType = hwType;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getHWType(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].hwType;
    } else {
	return -1;
    }
}

int PSIDnodes_setRunJobs(PSnodes_ID_t id, int runjobs)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].runJobs = runjobs;
	return 0;
    } else {
	return -1;
    }
}


int PSIDnodes_runJobs(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].runJobs;
    } else {
	return -1;
    }
}

int PSIDnodes_setIsStarter(PSnodes_ID_t id, int starter)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].isStarter = starter;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_isStarter(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].isStarter;
    } else {
	return -1;
    }
}

int PSIDnodes_setOverbook(PSnodes_ID_t id, PSnodes_overbook_t overbook)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].overbooking = overbook;
	return 0;
    } else {
	return -1;
    }
}

PSnodes_overbook_t PSIDnodes_overbook(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].overbooking;
    } else {
	return -1;
    }
}

int PSIDnodes_setExclusive(PSnodes_ID_t id, int exclusive)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].exclusive = exclusive;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_exclusive(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].exclusive;
    } else {
	return -1;
    }
}

int PSIDnodes_setPinProcs(PSnodes_ID_t id, int pinProcs)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].pinProcs = pinProcs;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_pinProcs(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].pinProcs;
    } else {
	return -1;
    }
}

int PSIDnodes_setBindMem(PSnodes_ID_t id, int bindMem)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].bindMem = bindMem;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_bindMem(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].bindMem;
    } else {
	return -1;
    }
}

short PSIDnodes_mapCPU(PSnodes_ID_t id, short cpu)
{
    if (PSIDnodes_validID(id) && cpu >= 0 && (unsigned)cpu<nodes[id].CPUmapSize
	&& cpu<PSIDnodes_getVirtCPUs(id)) {
	return nodes[id].CPUmap[cpu];
    } else {
	return -1;
    }
}


int PSIDnodes_clearCPUMap(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].CPUmapSize = 0;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_appendCPUMap(PSnodes_ID_t id, short cpu)
{
    if (!PSIDnodes_validID(id)) {
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
    int i, mapEntries = (int)myNode->CPUmapSize < myNode->virtCPU ?
	(int)myNode->CPUmapSize : myNode->virtCPU;

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


int PSIDnodes_setExtraIP(PSnodes_ID_t id, in_addr_t addr)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].extraIP = addr;
	return 0;
    } else {
	return -1;
    }
}

in_addr_t PSIDnodes_getExtraIP(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].extraIP;
    } else {
	return -1;
    }
}

int PSIDnodes_setPhysCPUs(PSnodes_ID_t id, short numCPU)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].physCPU = numCPU;
	return 0;
    } else {
	return -1;
    }
}

short PSIDnodes_getPhysCPUs(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].physCPU;
    } else {
	return -1;
    }
}

int PSIDnodes_setVirtCPUs(PSnodes_ID_t id, short numCPU)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].virtCPU = numCPU;
	return 0;
    } else {
	return -1;
    }
}

short PSIDnodes_getVirtCPUs(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].virtCPU;
    } else {
	return -1;
    }
}

int PSIDnodes_setHWStatus(PSnodes_ID_t id, int hwStatus)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].hwStatus = hwStatus;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getHWStatus(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
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
    if (!PSIDnodes_validID(id)) return NULL;

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
	    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d): allready there\n",
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

void send_GUID_OPTIONS(PStask_ID_t dest, PSIDnodes_gu_t what, int compat)
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
	    msg.opt[(int) msg.count].option = option =
		compat ? PSP_OP_UIDLIMIT : PSP_OP_UID;
	} else {
	    msg.opt[(int) msg.count].option = PSP_OP_SET_UID;
	    option = PSP_OP_ADD_UID;
	}
	break;
    case PSIDNODES_GROUP:
	PSID_log(PSID_LOG_VERB, " %s", "PSIDNODES_GROUP");
	if (PSC_getPID(dest)) {
	    msg.opt[(int) msg.count].option = option =
		compat ? PSP_OP_GIDLIMIT : PSP_OP_GID;
	} else {
	    msg.opt[(int) msg.count].option = PSP_OP_SET_GID;
	    option = PSP_OP_ADD_GID;
	}
	break;
    case PSIDNODES_ADMUSER:
	PSID_log(PSID_LOG_VERB, " %s", "PSIDNODES_ADMUSER");
	if (PSC_getPID(dest)) {
	    msg.opt[(int) msg.count].option = option =
		compat ? PSP_OP_ADMINUID : PSP_OP_ADMUID;
	} else {
	    msg.opt[(int) msg.count].option = PSP_OP_SET_ADMUID;
	    option = PSP_OP_ADD_ADMUID;
	}
	break;
    case PSIDNODES_ADMGROUP:
	PSID_log(PSID_LOG_VERB, " %s", "PSIDNODES_ADMGROUP");
	if (PSC_getPID(dest)) {
	    msg.opt[(int) msg.count].option = option =
		compat ? PSP_OP_ADMINGID : PSP_OP_ADMGID;
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
	if (compat) break;
	if (msg.count == DDOptionMsgMax) {
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    }
	    msg.count = 0;
	}
    }

    if (!compat) {
	msg.opt[(int) msg.count].option = PSP_OP_LISTEND;
	msg.opt[(int) msg.count].value = 0;
	msg.count++;
    }
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }
}

int PSIDnodes_setProcs(PSnodes_ID_t id, int procs)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].maxProcs = procs;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getProcs(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].maxProcs;
    } else {
	return -1;
    }
}

int PSIDnodes_setAcctPollI(PSnodes_ID_t id, int interval)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].acctPollInterval = interval;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_acctPollI(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].acctPollInterval;
    } else {
	return -1;
    }
}

int PSIDnodes_setSupplGrps(PSnodes_ID_t id, int supplGrps)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].supplGrps = supplGrps;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_supplGrps(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].supplGrps;
    } else {
	return -1;
    }
}

int PSIDnodes_setMaxStatTry(PSnodes_ID_t id, int tries)
{
    if (PSIDnodes_validID(id)) {
	nodes[id].maxStatTry = tries;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_maxStatTry(PSnodes_ID_t id)
{
    if (PSIDnodes_validID(id)) {
	return nodes[id].maxStatTry;
    } else {
	return -1;
    }
}
