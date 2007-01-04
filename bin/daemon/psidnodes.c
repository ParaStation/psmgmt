/*
 *               ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
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
    list_t uid_list;       /**< Users this node is reserved to */
    list_t gid_list;       /**< Groups this node is reserved to */
    list_t admuid_list;    /**< AdminUser on this node */
    list_t admgid_list;    /**< AdminGroup on this node */
    int maxProcs;          /**< Number of processes this node will handle */
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
    node->version = 0;
    node->physCPU = 0;
    node->virtCPU = 0;
    node->isUp = 0;
    node->hwType = 0;
    node->hwStatus = 0;
    node->extraIP = INADDR_ANY;
    node->runJobs = 0;
    node->isStarter = 0;
    node->uid_list = LIST_HEAD_INIT(node->uid_list);
    node->gid_list = LIST_HEAD_INIT(node->gid_list);
    node->admuid_list = LIST_HEAD_INIT(node->admuid_list);
    node->admgid_list = LIST_HEAD_INIT(node->admgid_list);
    node->maxProcs = -1;
}

int PSIDnodes_init(PSnodes_ID_t num)
{
    int i;

    if (nodes) free(nodes);

    numNodes = num;

    nodes = malloc(sizeof(*nodes) * numNodes);

    if (!nodes) {
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

/**
 * @brief Test ParaStation ID
 *
 * Test the validity of the ParaStation ID @a id.
 *
 * @param id The ParaStation ID to test.
 *
 * @return If @a id is valid, 1 is returned. Or 0 if the ID is out of range.
 */
static int ID_ok(PSnodes_ID_t id)
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

    if (! ID_ok(id)) {
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
    if ((ntohl(addr) >> 24 ) == 0x7f)
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
    if (ID_ok(id)) {
	return nodes[id].addr;
    } else {
	return -1;
    }
}


int PSIDnodes_bringUp(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	nodes[id].isUp = 1;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_bringDown(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	nodes[id].isUp = 0;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_isUp(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].isUp;
    } else {
	return 0;
    }
}

/**********************************************************************/
/* @todo This does not really make sense, but is a good start.
   Actually each piece of information needs its own version number */
int PSIDnodes_setInfoVersion(PSnodes_ID_t id, unsigned int version)
{
    if (ID_ok(id)) {
	nodes[id].version = version;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getInfoVersion(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].version;
    } else {
	return -1;
    }
}
/**********************************************************************/

int PSIDnodes_setHWType(PSnodes_ID_t id, int hwType)
{
    if (ID_ok(id)) {
	nodes[id].hwType = hwType;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getHWType(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].hwType;
    } else {
	return -1;
    }
}

int PSIDnodes_setRunJobs(PSnodes_ID_t id, int runjobs)
{
    if (ID_ok(id)) {
	nodes[id].runJobs = runjobs;
	return 0;
    } else {
	return -1;
    }
}


int PSIDnodes_runJobs(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].runJobs;
    } else {
	return -1;
    }
}

int PSIDnodes_setIsStarter(PSnodes_ID_t id, int starter)
{
    if (ID_ok(id)) {
	nodes[id].isStarter = starter;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_isStarter(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].isStarter;
    } else {
	return -1;
    }
}

int PSIDnodes_setOverbook(PSnodes_ID_t id, PSnodes_overbook_t overbook)
{
    if (ID_ok(id)) {
	nodes[id].overbooking = overbook;
	return 0;
    } else {
	return -1;
    }
}

PSnodes_overbook_t PSIDnodes_overbook(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].overbooking;
    } else {
	return -1;
    }
}

int PSIDnodes_setExclusive(PSnodes_ID_t id, int exclusive)
{
    if (ID_ok(id)) {
	nodes[id].exclusive = exclusive;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_exclusive(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].exclusive;
    } else {
	return -1;
    }
}

int PSIDnodes_setExtraIP(PSnodes_ID_t id, in_addr_t addr)
{
    if (ID_ok(id)) {
	nodes[id].extraIP = addr;
	return 0;
    } else {
	return -1;
    }
}

in_addr_t PSIDnodes_getExtraIP(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].extraIP;
    } else {
	return -1;
    }
}

int PSIDnodes_setPhysCPUs(PSnodes_ID_t id, short numCPU)
{
    if (ID_ok(id)) {
	nodes[id].physCPU = numCPU;
	return 0;
    } else {
	return -1;
    }
}

short PSIDnodes_getPhysCPUs(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].physCPU;
    } else {
	return -1;
    }
}

int PSIDnodes_setVirtCPUs(PSnodes_ID_t id, short numCPU)
{
    if (ID_ok(id)) {
	nodes[id].virtCPU = numCPU;
	return 0;
    } else {
	return -1;
    }
}

short PSIDnodes_getVirtCPUs(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].virtCPU;
    } else {
	return -1;
    }
}

int PSIDnodes_setHWStatus(PSnodes_ID_t id, int hwStatus)
{
    if (ID_ok(id)) {
	nodes[id].hwStatus = hwStatus;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getHWStatus(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
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
    if (!ID_ok(id)) return NULL;

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

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid);

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

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid);

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
		     __func__, id, what, guid);
	    return -1;
	}
        if (!cmp_GUID(what, guent->id, guid)) {
	    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d): allready there\n",
		     __func__, id, what, guid);
	    return -1;
	}
    }

    guent = malloc(sizeof(*guent));
    guent->id = guid;
    list_add_tail(&guent->next, list);

    return 0;
}

int PSIDnodes_remGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid)
{
    list_t *list = get_GUID_list(id, what), *pos, *tmp;

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid);

    list_for_each_safe(pos, tmp, list) {
	PSIDnodes_GUent_t *guent = list_entry(pos, PSIDnodes_GUent_t, next);
        if (!cmp_GUID(what, guent->id, guid)) {
            list_del(pos);
	    free(guent);
	    return 0;
        }
    }

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d): not found\n", __func__,
	     id, what, guid);

    return -1;
}

int PSIDnodes_testGUID(PSnodes_ID_t id,
		       PSIDnodes_gu_t what, PSIDnodes_guid_t guid)
{
    list_t *list = get_GUID_list(id, what), *pos;
    PSIDnodes_guid_t any;

    PSID_log(PSID_LOG_NODES, "%s(%d, %d, %d)\n", __func__, id, what, guid);

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
	/* Set next option type */
	msg.opt[(int) msg.count].option = option;
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
    if (ID_ok(id)) {
	nodes[id].maxProcs = procs;
	return 0;
    } else {
	return -1;
    }
}

int PSIDnodes_getProcs(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].maxProcs;
    } else {
	return -1;
    }
}
