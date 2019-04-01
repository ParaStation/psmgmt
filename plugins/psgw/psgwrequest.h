/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSGW_REQUEST
#define __PSGW_REQUEST

#include "peloguetypes.h"

typedef struct {
    char *jobid;              /**< leader job ID */
    char *packID;             /**< pack ID of the allocation */
    PElogueResource_t *res;   /**< pelogue resources structure */
    int routeRes;	      /**< result of the routing script */
    pid_t routePID;	      /**< PID of the routing script */
    char *routeFile;	      /**< path to routing file */
    int prologueState;	      /**< status of the prologue */
    uint32_t numGWnodes;      /**< number of gateway nodes */
    uint32_t numGWstarted;    /**< number of psgwd started */
    pid_t *gwPIDs;	      /**< PIDs of psgwd */
    char **gwAddr;	      /**< IP and port of psgwd */
    PSnodes_ID_t *gwNodes;    /**< list of gateway nodes */
    int timerRouteScript;     /**< timer for route script */
    uid_t uid;		      /**< user ID of job owner */
    gid_t gid;		      /**< group ID of job owner */
    Forwarder_Data_t *fwdata; /**< psgw forwarder to request a partition */
    int timerPartReq;         /**< timer for partition request */
    bool cleanup;             /**< automatic cleanup of route file */
    list_t next;              /**< used to put into some request-lists */
} PSGW_Req_t;

/**
 * @brief Add a new request
 *
 * @param res The corresponding pelogue resource structure
 *
 * @param packID The pack ID of the allocation
 *
 * @return Returns the newly created request
 */
PSGW_Req_t *Request_add(PElogueResource_t *res, char *packID);

/**
 * @brief Add allocated gateway nodes to a request
 *
 * @param req The request to use
 *
 * @param nodes Array of the gateway node IDs to add
 *
 * @param numNodes Number of gateway nodes to add
 */
void Request_setNodes(PSGW_Req_t *req, PSnodes_ID_t *nodes, uint32_t numNodes);

/**
 * @brief Delete a request
 *
 * @param req Pointer to the request to delete
 */
void Request_delete(PSGW_Req_t *req);

/**
 * @brief Delete all remaining requests
 */
void Request_clear(void);

/**
 * @brief Find a request identified by its job ID
 *
 * @param jobid The job ID of the request to find
 *
 * @return Returns a pointer to the request or NULL
 * if it was not found
 */
PSGW_Req_t *Request_find(char *jobid);

/**
 * @brief Verify a request pointer
 *
 * Ensure the given request pointer is still valid and the
 * corresponding request was not deleted in the meantime
 *
 * @param reqPtr The pointer to verify
 *
 * @return If the pointer is still valid it is returned.
 * Otherwise NULL will be returned.
 */
PSGW_Req_t *Request_verify(PSGW_Req_t *reqPtr);

/**
 * @brief Find a request identified by its forwarder TID
 *
 * @param tid The task ID of the forwarder
 *
 * @return Returns a pointer to the request or NULL
 * if it was not found
 */
PSGW_Req_t *Request_findByFW(PStask_ID_t tid);

#endif
