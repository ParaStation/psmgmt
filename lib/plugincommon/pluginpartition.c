/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "psidtask.h"
#include "pscommon.h"
#include "psidnodes.h"
#include "psidstatus.h"
#include "psidcomm.h"
#include "psdaemonprotocol.h"

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginpartition.h"

/** Structure to hold a nodelist */
typedef struct {
    int size;             /**< Actual number of valid entries within nodes[] */
    int maxsize;          /**< Maximum number of entries within nodes[] */
    PSnodes_ID_t *nodes;  /**< ParaStation IDs of the requested nodes. */
} nodelist_t;

int isPSAdminUser(uid_t uid, gid_t gid)
{
    if (!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
		(PSIDnodes_guid_t){.u=uid})
	    && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
		(PSIDnodes_guid_t){.g=gid})) {
	return 0;
    }
    return 1;
}

void rejectPartitionRequest(PStask_ID_t dest)
{
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_PARTITIONRES,
		.dest = dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
    sendMsg(&msg);
}

/**
 * @brief Send the ParaStation nodelist to the master psid.
 *
 * @param nodelist The nodelist to send.
 *
 * @param msg The received message to forward.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int sendNodelist(nodelist_t *nodelist, DDBufferMsg_t *msg)
{
    int num = nodelist->size, offset = 0;

    msg->header.type = PSP_DD_GETPARTNL;
    msg->header.dest = PSC_getTID(getMasterID(), 0);

    while (offset < num) {
	int chunk = (num-offset > NODES_CHUNK) ?  NODES_CHUNK : num-offset;
	char *ptr = msg->buf;
	msg->header.len = sizeof(msg->header);

	*(int16_t*)ptr = chunk;
	ptr += sizeof(int16_t);
	msg->header.len += sizeof(int16_t);

	memcpy(ptr, nodelist->nodes+offset, chunk * sizeof(*nodelist->nodes));
	msg->header.len += chunk * sizeof(*nodelist->nodes);
	offset += chunk;
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    pluginwarn(errno, "%s: PSI_sendMsg() : ", __func__);
	    return -1;
	}
    }
    return 0;
}

int injectNodelist(DDBufferMsg_t *inmsg, int32_t nrOfNodes, PSnodes_ID_t *nodes)
{
    PStask_t *task;
    nodelist_t *nodelist = NULL;

    /* find task */
    if (!(task = PStasklist_find(&managedTasks, inmsg->header.sender))) {
	pluginlog("%s: task for message sender '%s' not found\n", __func__,
	    PSC_printTID(inmsg->header.sender));
	errno = EACCES;
	goto error;
    }

    /* we don't change the nodelist for admin users */
    if ((isPSAdminUser(task->uid, task->gid))) return 1;

    if (!nrOfNodes || !nodes) {
	/* we did not find the corresponding batch job */
	pluginlog("%s: denying access to mpiexec for non admin user "
		    "with uid '%i'\n", __func__, task->uid);

	errno = EACCES;
	goto error;
    }

    if (!task->request) {
	pluginlog("%s: request for task is empty\n", __func__);
	errno = EACCES;
	goto error;
    }

    /* change partition options */
    task->request->num = nrOfNodes;
    task->request->sort = 0;
    task->request->options |= PART_OPT_NODEFIRST;
    task->request->options |= PART_OPT_EXACT;

    if (!knowMaster()) {
	pluginlog("%s: master is unknown, cannot send partition request\n",
		__func__);
	errno = EACCES;
	goto error;
    }

    /* forward partition request to master */
    inmsg->header.len = sizeof(inmsg->header)
	+ PSpart_encodeReq(inmsg->buf, sizeof(inmsg->buf), task->request,
		PSIDnodes_getDmnProtoV(getMasterID()));

    inmsg->header.type = PSP_DD_GETPART;
    inmsg->header.dest = PSC_getTID(getMasterID(), 0);

    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	goto error;
    }

    task->request->numGot = 0;

    /* realloc space for nodelist */
    task->request->nodes =
	realloc(task->request->nodes,
		sizeof(*task->request->nodes) * nrOfNodes);

    /* save nodelist in task struct */
    memcpy(task->request->nodes, nodes,
	    sizeof(*task->request->nodes) * nrOfNodes);

    task->request->numGot = nrOfNodes;

    nodelist = umalloc(sizeof(nodelist_t));
    nodelist->size = nodelist->maxsize = nrOfNodes;
    nodelist->nodes = nodes;

    /* send complete node list for partition request to master */
    if ((sendNodelist(nodelist, inmsg)) == -1) {
	goto error;
    }

    ufree(nodelist);

    return 0;


    error:
    {
	ufree(nodelist);

	if (task && task->request) {
	    PSpart_delReq(task->request);
	    task->request = NULL;
	}
	rejectPartitionRequest(inmsg->header.sender);

	return 0;
    }
}
