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
#include "pshostlist.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "pscommon.h"

/** Used by @ref convHLtoPSnodes() to pass information to the visitor */
typedef struct {
    uint32_t nrOfNodes;		/**< number of resolved nodes */
    uint32_t size;		/**< actual size of @a nodes array */
    PSnodes_ID_t *nodes;	/**< all resovled node IDs */
    ResolveFunc_t *resolveNID;	/**< resolver function to be used */
} PSnodeList_t;

/**
 * @brief Visit every item of an item-range
 *
 * Expand an item-range and visit each item calling the @a visitor
 * function. The @a range is a describing string of the form
 * "<first>[-<last>]". <first> must be smaller or equal to
 * <last>. Each item visited is preceded by @a prefix.
 *
 * @param prefix Prefix prepended to each item visited
 *
 * @param range Range describing items to be visited "<first>[-<last>]"
 *
 * @param visitor Visitor function to be called for each item
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the items
 *
 * @return Returns true on success otherwise false
 */
static bool visitItemRange(char *prefix, char *range, CL_Visitor_t visitor,
			   void *info)
{
    char *sep = strchr(range, '-');
    if (!sep) {
	// single item
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s%s", prefix ? prefix : "", range);
	if (!visitor(tmp, info)) return false;
	return true;
    }

    unsigned int min, max;
    int pad;
    if (sscanf(range, "%u%n-%u", &min, &pad, &max) != 2) {
	PSC_log(-1, "%s: invalid range '%s'\n", __func__, range);
	return false;
    }

    if (min > max) {
	PSC_log(-1, "%s: invalid range '%s'\n", __func__, range);
	return false;
    }

    for (unsigned int i = min; i <= max; i++) {
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s%0*u", prefix ? prefix : "", pad, i);
	if (!visitor(tmp, info)) return false;
    }

    return true;
}

bool traverseCompList(const char *compList, CL_Visitor_t visitor, void *info)
{
    const char delimiters[] =", \n";
    bool isOpen = false, ret = false;

    char *dupCL = strdup(compList);
    if (!dupCL) PSC_exit(errno, "%s: strdup(compList)", __func__);

    char *saveptr, *prefix = NULL;
    char *next = strtok_r(dupCL, delimiters, &saveptr);
    while (next) {
	char *openBrk = strchr(next, '[');
	char *closeBrk = strchr(next, ']');

	if (openBrk && isOpen) {
	    PSC_log(-1, "%s: error nested brackets found\n", __func__);
	    goto error;
	}

	if (openBrk && closeBrk) {
	    // whole bracket
	    char *range = openBrk + 1;
	    *openBrk = *closeBrk = '\0';
	    if (!visitItemRange(next, range, visitor, info)) goto error;
	} else if (openBrk && !closeBrk) {
	    // start of a new bracket
	    char *range = openBrk + 1;
	    *openBrk = '\0';
	    prefix = next;
	    if (!visitItemRange(prefix, range, visitor, info)) goto error;
	    isOpen = true;
	} else if (closeBrk) {
	    // end of the bracket
	    if (!isOpen) {
		PSC_log(-1, "%s: error no open bracket found\n", __func__);
		goto error;
	    }
	    if (!prefix) {
		PSC_log(-1, "%s: error invalid prefix\n", __func__);
		goto error;
	    }

	    *closeBrk = '\0';
	    if (!visitItemRange(prefix, next, visitor, info)) {
		goto error;
	    }
	    prefix = NULL;
	    isOpen = false;
	} else if (isOpen) {
	    // inside the bracket
	    if (!prefix) {
		PSC_log(-1, "%s: error invalid prefix\n", __func__);
		goto error;
	    }
	    if (!visitItemRange(prefix, next, visitor, info)) {
		goto error;
	    }
	} else {
	    // no bracket at all
	    if (!visitor(next, info)) goto error;
	}
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    ret = true;

error:
    free(dupCL);
    return ret;
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

    if (!traverseCompList(hostlist, hostToPSnode, &psNL)) {
	free(psNL.nodes);
	return false;
    }

    *nodes = psNL.nodes;
    *nrOfNodes = psNL.nrOfNodes;

    return true;
}
