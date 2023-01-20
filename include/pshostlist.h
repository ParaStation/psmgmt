/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
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
 * @brief Compressed list visitor function
 *
 * Visitor function used by @ref traverseCompList() in order to visit
 * each expanded item of a given compressed list.
 *
 * The parameters are as follows: @a item points to the expanded item
 * to visit. @a info points to the additional information passed to
 * @ref traverseCompList() in order to be forwarded to each item.
 *
 * If the visitor function returns false, the traversal will be
 * interrupted and @ref traverseCompList() will return to its calling
 * function.
 */
typedef bool CL_Visitor_t(char *host, void *info);

/**
 * @brief Traverse each item of a compressed list
 *
 * Traverse all expanded items of a given compressed list @a compList
 * by calling @a visitor for each item. In addition to a pointer to
 * the current item @a info is passed as additional information to the
 * @a visitor.
 *
 * @param compList The compressed list to parse and traverse
 *
 * @param visitor Visitor function to be called for each expanded item
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the expanded items
 *
 * @return If the compressed list cannot be parsed or a visitor
 * returns false, traversal will be stopped and false is returned;
 * otherwise true is returned
 */
bool traverseCompList(const char *compList, CL_Visitor_t visitor, void *info);

/**
 * @brief Host resolve function
 *
 * Resolve function used by @ref convHLtoPSnodes() in order to resolve
 * ParaStation node IDs for the hosts in a compressed list. This
 * enables the use of @ref convCLtoPSnodes() within and outside of the
 * main psid process. While plugins running within the context of the
 * main psid process might use @ref getNodeIDbyName() as the resolve
 * function, other processes can utilize @ref PSI_resolveNodeID()
 * instead. Nevertheless, custom functions might be used, too.
 *
 * The resolve function should return -1 if the host lookup
 * failed. Otherwise the ParaStation node ID must be returned.
 */
typedef PSnodes_ID_t ResolveFunc_t(const char *host);

/**
 * @brief Convert hosts in a compressed list to ParaStation node IDs
 *
 * Traverse all hosts of a given compressed (host-)list @a compList by
 * calling the resolve function @a resolveNID for each host in the
 * list. Each host has to be unique and shall appear only once in the
 * compressed list.
 *
 * The corresponding ParaStation node ID for every host is resolved
 * via @a resolveNID and is saved in @a nodes. The number of resolved
 * hosts is saved in @a nrOfNodes. The node array @a nodes is
 * allocated using malloc() and must be freed by the caller after use.
 *
 * @param hostlist The compressed (host-)list to convert
 *
 * @param resolveNID Function to resolve a single expanded hostname
 *
 * @param nodes Array of ParaStation node IDs holding the result
 *
 * @param nrOfNodes Number of nodes in @a nodes after return
 *
 * @return Returns true if every item in the expandable-list could be
 * converted to a representing ParaStation node ID; otherwise false is
 * returned
 */
bool convHLtoPSnodes(char *hostlist, ResolveFunc_t resolveNID,
		     PSnodes_ID_t **nodes, uint32_t *nrOfNodes);

#endif   /* __PSHOSTLIST_H */
