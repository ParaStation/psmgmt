/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSHOSTLIST_H
#define __PSHOSTLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "psnodes.h"

/**
 * @brief Create list from range
 *
 * Create a comma-separated list of entries defined by @a range. @a
 * range is a describing string of the form
 * "<first>[-<last>]". <first> must be smaller or equal to <last>. The
 * list is appended to the dynamical character array @a list is
 * pointing to using @ref str2Buf(). @a size points to the initial
 * size of this array. Each entry appended to @a list is preceded by
 * @a prefix. @a count will return the number of entries appended to
 * @a list.
 *
 * If <first> contains leading 0's each entry will contain the same
 * number of digits as <first>.
 *
 * @param prefix Prefix repented to each entry created
 *
 * @param range Range describing entries created "<first>[-<last>]"
 *
 * @param list Character array holding the entries after return
 *
 * @param size Size of @a list
 *
 * @param count Number of entries created
 *
 * @return Upon success true is returned. Or false in case of an
 * error. In the latter case @a list and @a size remain untouched.
 */
bool range2List(char *prefix, char *range, char **list, size_t *size,
		uint32_t *count);

/**
 * @brief Expand hostlist
 *
 * Expand the hostlist given in @a hostlist by replacing ranges by a
 * comma-separated list of simple hosts. In order to expand a range
 * @ref range2List() is used. The expanded hostlist is stored into a
 * new dynamical character array that shall be free()ed after use. @a
 * count will provide the total number of entries of the hostlist
 * after expansion.
 *
 * @param hostlist Hostlist to expand
 *
 * @param count Total number of entries created
 *
 * @return Upon success the expanded, comma-separated hostlist is
 * returned as a dynamical character array. Otherwise NULL is
 * returned.
 */
char *expandHostList(char *hostlist, uint32_t *count);

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
bool traverseHostList(char *hostlist, HL_Visitor_t visitor, void *info);

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
