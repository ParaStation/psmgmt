/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "pscommon.h"

#include "pshostlist.h"

/** Used by @ref convHLtoPSnodes() to pass information to the visitor */
typedef struct {
    uint32_t nrOfNodes;		/**< number of resolved nodes */
    uint32_t size;		/**< actual size of @a nodes array */
    PSnodes_ID_t *nodes;	/**< all resovled node IDs */
    ResolveFunc_t *resolveNID;	/**< resolver function to be used */
} PSnodeList_t;

/**
 * @brief Visit every node of a host-range
 *
 * Expand a host-range and visit each host calling the @a visitor function.
 * The @a range is a describing string of the form "<first>[-<last>]".
 * <first> must be smaller or equal to <last>. Each host visited is
 * preceded by * @a prefix.
 *
 * @param prefix Prefix repented to each host visited
 *
 * @param range Range describing hosts visited "<first>[-<last>]"
 *
 * @param visitor Visitor function to be called for each host
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the hosts
 *
 * @return Returns true on success otherwise false.
 */
static bool visitHostRange(char *prefix, char *range, HL_Visitor_t visitor,
			   void *info)
{
    char *sep = strchr(range, '-');
    unsigned int i, min, max;
    int pad;
    char tmp[1024];

    if (!sep) {
	snprintf(tmp, sizeof(tmp), "%s%s", prefix ? prefix : "", range);
	if (!visitor(tmp, info)) return false;
	return true;
    }

    if (sscanf(range, "%u%n-%u", &min, &pad, &max) != 2) {
	PSC_log(-1, "%s: invalid range '%s'\n", __func__, range);
	return false;
    }

    if (min>max) {
	PSC_log(-1, "%s: invalid range '%s'\n", __func__, range);
	return false;
    }

    for (i=min; i<=max; i++) {
	snprintf(tmp, sizeof(tmp), "%s%0*u", prefix ? prefix : "", pad, i);
	if (!visitor(tmp, info)) return false;
    }

    return true;
}

bool traverseHostList(char *hostlist, HL_Visitor_t visitor, void *info)
{
    const char delimiters[] =", \n";
    char *saveptr, *range, *prefix = NULL;
    bool isOpen = false;

    char *duphl = strdup(hostlist);
    if (!duphl) PSC_exit(errno, "%s: strdup(hostlist)", __func__);

    char *next = strtok_r(duphl, delimiters, &saveptr);
    while (next) {
	char *openBrk = strchr(next, '[');
	char *closeBrk = strchr(next, ']');

	if (openBrk && !closeBrk) {
	    if (isOpen) {
		PSC_log(-1, "%s: error two open bracket found\n", __func__);
		goto expandError;
	    }

	    range = openBrk +1;
	    *openBrk = '\0';
	    prefix = strdup(next);
	    if (!prefix) PSC_exit(errno, "%s: strdup(next)", __func__);

	    if (!visitHostRange(prefix, range, visitor, info)) {
		goto expandError;
	    }
	    isOpen = true;
	} else if (openBrk && closeBrk) {
	    if (isOpen) {
		PSC_log(-1, "%s: error two open bracket found\n", __func__);
		goto expandError;
	    }

	    range = openBrk +1;
	    *closeBrk = *openBrk = '\0';

	    if (!visitHostRange(next, range, visitor, info)) {
		goto expandError;
	    }
	} else if (closeBrk) {
	    if (!isOpen) {
		PSC_log(-1, "%s: error no open bracket found\n", __func__);
		goto expandError;
	    }
	    if (!prefix) {
		PSC_log(-1, "%s: error invalid prefix\n", __func__);
		goto expandError;
	    }

	    *closeBrk = '\0';
	    if (!visitHostRange(prefix, next, visitor, info)) {
		goto expandError;
	    }

	    if (prefix) free(prefix);
	    prefix = NULL;
	    isOpen = false;
	} else if (isOpen) {
	    if (!prefix) {
		PSC_log(-1, "%s: error invalid prefix\n", __func__);
		goto expandError;
	    }
	    if (!visitHostRange(prefix, next, visitor, info)) {
		goto expandError;
	    }
	} else {
	    if (!visitor(next, info)) goto expandError;
	}
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    free(duphl);
    free(prefix);
    return true;

expandError:
    free(duphl);
    free(prefix);
    return false;
}

/**
 * @brief Convert a single host to ParaStation node ID
 *
 * @param host The host to convert
 *
 * @param info Pointer holding a PSnodeList_t structure holding
 * the result
 *
 * @return Returns true on success otherwise false
 */
static bool hostToPSnode(char *host, void *info)
{
    PSnodeList_t *psNL = info;

    if (psNL->nrOfNodes >= psNL->size) {
	PSC_log(-1, "%s: no space left (max %i)\n", __func__, psNL->size);
	return false;
    }

    PSnodes_ID_t nodeID = psNL->resolveNID(host);
    if (nodeID == -1) {
	PSC_log(-1, "%s: no PS node ID for host %s\n", __func__, host);
	return false;
    }
    psNL->nodes[psNL->nrOfNodes++] = nodeID;

    return true;
}

bool convHLtoPSnodes(char *hostlist, ResolveFunc_t resolveNID,
		     PSnodes_ID_t **nodes, uint32_t *nrOfNodes)
{
    PSnodeList_t psNL = {
	.nrOfNodes = 0,
	.size = PSC_getNrOfNodes(),
	.resolveNID = resolveNID };

    *nrOfNodes = 0;
    *nodes = NULL;

    psNL.nodes = malloc(psNL.size * sizeof(*psNL.nodes));
    if (!psNL.nodes) {
	PSC_log(-1, "%s: no memory for nodelist\n", __func__);
	return false;
    }

    if (!traverseHostList(hostlist, hostToPSnode, &psNL)) {
	free(psNL.nodes);
	return false;
    }

    *nodes = psNL.nodes;
    *nrOfNodes = psNL.nrOfNodes;

    return true;
}
