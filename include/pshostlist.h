/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSHOSTLIST_H
#define __PSHOSTLIST_H

#include <stdbool.h>
#include <stdint.h>

#include "psnodes.h"

/**
 * @brief Host-list visitor function
 *
 * Visitor function used by @ref traverseHostList() in order to visit
 * each host of a given host-list.
 *
 * The parameters are as follows: @a host points to the host to
 * visit. @a info points to the additional information passed to @ref
 * traverseHostList() in order to be forwarded to each host.
 *
 * If the visitor function returns false the traversal will be
 * interrupted and @ref traverseHostList() will return to its calling
 * function.
 */
typedef bool HL_Visitor_t(char *host, void *info);

/**
 * @brief Traverse every host of a host-list
 *
 * Traverse all hosts of a given @a hostlist by calling @a visitor for each
 * host. In addition to a pointer to the current host @a info is
 * passed as additional information to @a visitor.
 *
 * @param hostlist The host-list to parse and traverse
 *
 * @param visitor Visitor function to be called for each host
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the hosts
 *
 * @return If the host-list cannot be parsed or a visitor returns false,
 * traversal will be stopped and false is returned. If parsing was successful
 * and no visitor returned false during the traversal true is returned.
 */
bool traverseHostList(const char *hostlist, HL_Visitor_t visitor, void *info);

/**
 * @brief Host resolve function
 *
 * Resolve function used by @ref convHLtoPSnodes() in order to resolve
 * a ParaStation node ID for a give host. This enables the use of @ref
 * convHLtoPSnodes() within and outside of the main psid process.
 * Plugins running within the main psid process can use @ref getNodeIDbyName()
 * as resolve function. Other processes may use @ref PSI_resolveNodeID() as
 * resolve function.
 *
 * The resolve function should return -1 if the host lookup failed. Otherwise
 * the ParaStation node ID is returned.
 */
typedef PSnodes_ID_t ResolveFunc_t(const char *host);

/**
 * @brief Convert a host-list to ParaStation node IDs
 *
 * Traverse all hosts of a given @a hostlist by calling the
 * resolve function @a resolveNID for each host in the list. Each
 * host has to be unique and may only appear once in the host-list.
 * The corresponding ParaStation node ID for every host resolved
 * by @a resolveNID is saved in @a nodes. The number of resolved
 * hosts is saved in @a nrOfNodes. The node array @a nodes
 * is allocated using malloc() and should be freed by the caller
 * after use.
 *
 * @param hostlist The host-list to convert
 *
 * @param resolveNID Function to resolve a single hostname
 *
 * @param nodes Array of ParaStation node IDs holding the result
 *
 * @param nrOfNodes Number of nodes in node-list
 *
 * @return Returns true if every host in the host-list could be converted to
 * a representing ParaStation node ID. Otherwise false is returned.
 */
bool convHLtoPSnodes(char *hostlist, ResolveFunc_t resolveNID,
		     PSnodes_ID_t **nodes, uint32_t *nrOfNodes);

#endif   /* __PSHOSTLIST_H */
