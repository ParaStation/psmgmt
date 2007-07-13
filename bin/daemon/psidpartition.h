/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Helper functions in order to setup and handle partitions.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDPARTITION_H
#define __PSIDPARTITION_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Handle a PSP_CD_CREATEPART message.
 *
 * Handle the message @a inmsg of type PSP_CD_CREATEPART.
 *
 * With this kind of message a client will request for a partition of
 * nodes. Besides forwarding this kind of message to the master node
 * as a PSP_DD_GETPART message it will be stored locally in order to
 * allow resending it, if the master changes.
 *
 * Depending on the acutal request, a PSP_CD_CREATEPART message might
 * be followed by one or more PSP_CD_CREATEPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_CREATEPART(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_CREATEPARTNL message.
 *
 * Handle the message @a inmsg of type PSP_CD_CREATEPARTNL.
 *
 * This follow up message contains (part of) the nodelist connected to
 * the partition request in the leading PSP_CD_CREATEPART message.
 *
 * Depending on the actual request, a PSP_CD_CREATEPART message might
 * be followed by further PSP_CD_CREATEPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_CREATEPARTNL(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_GETPART message.
 *
 * Handle the message @a inmsg of type PSP_DD_GETPART.
 *
 * PSP_DD_GETPART messages are the daemon-daemon version of the
 * original PSP_CD_CREATEPART message of the client sent to its local
 * daemon.
 *
 * Depending on the actual request the master waits for following
 * PSP_DD_GETPARTNL messages, tries to immediately allocate a
 * partition or enqueues the request to the queue of pending requests.
 *
 * If a partition could be allocated successfully, the actual
 * partition will be send to the client's local daemon process via
 * PSP_CD_PROVIDEPART and PSP_CD_PROVIDEPARTSL messages.
 *
 * Depending on the actual request, a PSP_DD_GETPART message might
 * be followed by one or more PSP_DD_GETPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETPART(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_GETPARTNL message.
 *
 * Handle the message @a inmsg of type PSP_DD_GETPARTNL.
 *
 * PSP_DD_GETPARTNL messages are the daemon-daemon version of the
 * original PSP_CD_CREATEPARTNL message of the client sent to its
 * local daemon.
 *
 * Depending on the actual request the master waits for further
 * PSP_DD_GETPARTNL messages, tries to immediately allocate a
 * partition or enqueues the request to the queue of pending requests.
 *
 * If a partition could be allocated successfully, the actual
 * partition will be send to the client's local daemon process via
 * PSP_CD_PROVIDEPART and PSP_CD_PROVIDEPARTSL messages.
 *
 * Depending on the actual request, a PSP_DD_GETPARTNL message might
 * be followed by further PSP_DD_GETPARTNL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETPARTNL(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_PROVIDEPART message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDEPART.
 *
 * This kind of messages is used by the master process in order to
 * provide actually allocated partitions to the requesting client's
 * local daemon process. This message will be followed by one or more
 * PSP_DD_PROVIDEPARTSL messages containing the partitions actual
 * slotlist.
 *
 * The client's local daemon will store the partition to the
 * corresponding task structure and wait for following
 * PSP_DD_PROVIDEPARTSL messages.
 *
 * Furthermore this message might contain an error message reporting
 * the final failure on the attempt to allocate a partition. In this
 * case a PSP_CD_PARTITIONRES message reporting this error to the
 * requesting client is generated.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_PROVIDEPART(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_PROVIDEPARTSL message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDEPARTSL.
 *
 * Follow up message to a PSP_DD_PROVIDEPART containing the
 * partition's actual slots. These slots will be stored to the
 * requesting client's task structure. Upon successful receive of the
 * partition's last slot a PSP_CD_PARTITIONRES message is send to the
 * requesting client.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_PROVIDEPARTSL(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_CD_GETNODES message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETNODES.
 *
 * This kind of message is used by clients of the local daemon in
 * order to actually get nodes from the pool of nodes stored within
 * the partition requested from the master node.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETNODES(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_NODESRES message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETNODES.
 *
 * This kind of message is used as an answer to a PSP_CD_GETNODES
 * message. The daemon of the requesting client will store the answer
 * in the @ref spawnNodes member of the client's task structure.
 *
 * This is needed for transparent process-pinning.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_NODESRES(DDBufferMsg_t *inmsg);

/**
 * @brief Send a PSP_DD_GETTASKS message.
 *
 * Send a PSP_DD_GETTASKS message to the node with ParaStation ID @a
 * node. This message on the one hand requests a list of all running
 * tasks on the receiving node, on the other hand a list of all
 * pending partition request on the receiving node.
 *
 * Thus, this message will be answered by one or more
 * PSP_DD_PROVIDETASK messages and zero or more PSP_DD_GETPART
 * messages.
 *
 * Usually this message is generated by a new master node in order to
 * get an overview on the cluster status.
 *
 * @param node The the node requested to send a list of active tasks
 * and pending partition requests.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_GETTASKS message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
int send_GETTASKS(PSnodes_ID_t node);

/**
 * @brief Handle a PSP_DD_GETTASKS message.
 *
 * Handle the message @a inmsg of type PSP_DD_GETTASKS.
 *
 * Send a list of all running processes partition info and pending
 * partition requests to the sending node. While for the running
 * processes PSP_DD_PROVIDETASK and PSP_DD_PROVIDETASKSL messages are
 * used, the latter reuse the PSP_DD_GETPART and PSP_DD_GETPARTNL
 * messages used to forward the original client request
 * messages. Actually for a new master there is no difference if the
 * message is directly from the requesting client or if it was
 * buffered within the client's local daemon.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETTASKS(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_PROVIDETASK message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDETASK.
 *
 * This is part of the answer to a PSP_DD_GETTASKS message. For each
 * running task whose root process is located on the sending node a
 * PSP_DD_PROVIDETASK message is generated and send to the master
 * process. It provides all the information necessary for the master
 * to handle partition requests despite apart from the list of slots
 * building the corresponding partition. This message will be followed
 * by one or more PSP_DD_PROVIDETASKSL messages containing this
 * slotlist.
 *
 * The client's local daemon will store the partition to the
 * corresponding partition request structure and wait for following
 * PSP_DD_PROVIDETASKSL messages.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_PROVIDETASK(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_PROVIDETASKSL message.
 *
 * Handle the message @a inmsg of type PSP_DD_PROVIDETASKSL.
 *
 * Follow up message to a PSP_DD_PROVIDETASK containing the
 * partition's actual slots. These slots will be stored to the
 * client's partition request structure.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_PROVIDETASKSL(DDBufferMsg_t *inmsg);

/**
 * @brief Send a PSP_DD_TASKDEAD message.
 *
 * Send a PSP_DD_TASKDEAD message to the current master node.  This
 * message informs the master node on the exit of the root process
 * with task ID @a tid and thus allows to free the corresponding
 * partition.
 *
 * @param tid The task ID of the root process that exited.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_TASKDEAD message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
int send_TASKDEAD(PStask_ID_t tid);

/**
 * @brief Handle a PSP_DD_TASKDEAD message.
 *
 * Handle the message @a msg of type PSP_DD_TASKDEAD.
 *
 * A PSP_DD_TASKDEAD message informs the master process upon exit of
 * the tasks root process. This enables the master to free the
 * resources allocated by the corresponding task.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_TASKDEAD(DDMsg_t *msg);

/**
 * @brief Send a PSP_DD_TASKSUSPEND message.
 *
 * Send a PSP_DD_TASKSUSPEND message to the current master node. This
 * message informs the master node on the suspension of the root
 * process with task ID @a tid and thus probably allows to temporarily
 * free the corresponding partition.
 *
 * @param tid The task ID of the root process that was suspended.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_TASKSUSPEND message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
int send_TASKSUSPEND(PStask_ID_t tid);

/**
 * @brief Handle a PSP_DD_TASKSUSPEND message.
 *
 * Handle the message @a msg of type PSP_DD_TASKSUSPEND.
 *
 * A PSP_DD_TASKSUSPEND message informs the master process upon
 * suspension of the tasks root process. This enables the master to
 * temporarily free the resources allocated by the corresponding task
 * if requested.
 *
 * If the resources of a task are actually freed upon suspension is
 * steered by the @ref freeOnSuspend member of the @ref config
 * structure. This can be modified on the one hand by the
 * 'freeOnSuspend' keyword within the daemon's configuration file or
 * on the other hand during run-time by the 'set freeOnSuspend'
 * directive of the ParaStation administration tool psiadmin.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_TASKSUSPEND(DDMsg_t *msg);

/**
 * @brief Send a PSP_DD_TASKRESUME message.
 *
 * Send a PSP_DD_TASKRESUME message to the current master node. This
 * message informs the master node on the continuation of the root
 * process with task ID @a tid and thus probably to realloc the
 * corresponding temporarily freed partition.
 *
 * @param tid The task ID of the root process that continues to run.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_TASKRESUME message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
int send_TASKRESUME(PStask_ID_t tid);

/**
 * @brief Handle a PSP_DD_TASKRESUME message.
 *
 * Handle the message @a msg of type PSP_DD_TASKRESUME.
 *
 * A PSP_DD_TASKRESUME message informs the master process upon
 * continuation of the suspended tasks root process. This enables the
 * master to realloc the temporarily freed resources allocated by the
 * corresponding task during startup.
 *
 * If the resources of a task were actually freed upon suspension is
 * steered by the @ref freeOnSuspend member of the @ref config
 * structure. This can be modified on the one hand by the
 * 'freeOnSuspend' keyword within the daemon's configuration file or
 * on the other hand during run-time by the 'set freeOnSuspend'
 * directive of the ParaStation administration tool psiadmin.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_TASKRESUME(DDMsg_t *msg);

/**
 * @brief Send a PSP_DD_CANCELPART message.
 *
 * Send a PSP_DD_CANCELPART message to the current master node.  This
 * message informs the master node on the exit of the root process
 * with task ID @a tid and thus allows to cancel the corresponding
 * partition request.
 *
 * @param tid The task ID of the root process that exited.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_CANCELPART message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
int send_CANCELPART(PStask_ID_t tid);

/**
 * @brief Handle a PSP_CD_CANCELPART message.
 *
 * Handle the message @a inmsg of type PSP_CD_CANCELPART.
 *
 * A PSP_CD_CANCELPART message informs the master process upon exit of
 * the tasks root process. This enables the master to remove partition
 * requests sent from this task from the queue of pending partition
 * requests.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_CANCELPART(DDBufferMsg_t *inmsg);

/**
 * @brief Initialized partition handler.
 *
 * Initialize the partition handler machinery. This includes allocating
 * memory used to store centralized information on the allocated
 * partitions and pending partition requests.
 *
 * Usually this function is called upon detection that the local
 * daemon has to act as a master within the cluster.
 *
 * @return No return value.
 */
void initPartHandler(void);

/**
 * @brief Handle partition requests.
 *
 * Actually handle partition requests stored within in the queue of
 * pending requests. Furthermore the requests queue will be cleaned up
 * in order to remove requests marked to get deleted from within the
 * @ref cleanupRequests() function.
 *
 * @return No return value.
 */
void handlePartRequests(void);

/**
 * @brief Remove requests.
 *
 * Remove all requests originating from node @a node from the queue of
 * pending processes and from the queue of running processes.
 *
 * Within this function, the partition requests are actually not
 * removed from the queue, but only marked to get deleted within the
 * next rund of handlePartRequests(). This is necessary to make this
 * function robust enough the get used from within a RDP callback.
 *
 * @param node The node whose request are going to be cleaned up.
 *
 * @return No return value.
 */
void cleanupRequests(PSnodes_ID_t node);

/**
 * @brief Shutdown partition handler.
 *
 * Shut down the partition handler machinery. This includes
 * freeing memory used to store centralized information on the
 * allocated partitions and pending partition requests as allocated
 * within @ref initPartHandler().
 *
 * Usually this function is called upon detection that the local
 * daemon is freed from the burden of acting as the master within the
 * cluster.
 *
 * @return No return value.
 */
void exitPartHandler(void);

/**
 * @brief Number of assigned jobs.
 *
 * Return the number of job slots assigned to node @a node.
 *
 * @param node The node to request.
 *
 * @return On success, the number of jobs slots assigned to the
 * requested node is returned, or 0, if an error occurred. Be aware of
 * the fact, that an error cannot be distinguished from an empty node.
 */
unsigned short getAssignedJobs(PSnodes_ID_t node);

/**
 * @brief The nodes exclusive flag.
 *
 * Return the flag marking node @a node to be used by its current job
 * exclusively.
 *
 * @param node The node to request.
 *
 * @return On success, the nodes exclusive flag is returned, i.e. the
 * flag marking the node to be used by its current job exclusively. Or
 * 0, if an error occurred. Be aware of the fact, that an error cannot
 * be distinguished from a node not used exclusively.
 */
int getIsExclusive(PSnodes_ID_t node);


/**
 * @brief Send list of requests.
 *
 * Send a list of partition-requests registered within the master
 * daemon. Dependings on the flags set within @a opt, only pending,
 * running or suspended requests might be send to the @a requester.
 *
 * If @a ref PART_LIST_NODES is set in @a opt, also a list of the
 * processor slots allocated to the request is sent.
 *
 * @param requester Task ID of process waiting for answer.
 *
 * @param opt Option flags marking which type of requests to send and
 * format of answer (with/without list of slots).
 *
 * @return No return value.
 */
void sendRequestLists(PStask_ID_t requester, PSpart_list_t opt);
    
#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDPARTITION_H */
