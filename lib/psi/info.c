/*
 *               ParaStation
 *
 * Copyright (C) 1999-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pscommon.h"
#include "psiinfo.h"

#include "info.h"

int INFO_request_countstatus(PSnodes_ID_t node, int32_t hwindex,
			     void *buf, size_t size, int verbose)
{
    int err;

    err = PSI_infoString(node, PSP_INFO_COUNTSTATUS, &hwindex,
			 buf, size, verbose);

    if (err) return err;

    ((char *)buf)[size-1] = '\0';

    return strlen(buf);
}

int INFO_request_hoststatus(void *buf, size_t size, int verbose)
{
    return PSI_infoList(-1, PSP_INFO_LIST_HOSTSTATUS, NULL,
			buf, size, verbose);
}

int INFO_request_nodelist(NodelistEntry_t *nodelist, size_t size, int verbose)
{
    PSnodes_ID_t node;
    static char *hostStatus=NULL;
    static uint16_t *totalJobs=NULL, *normalJobs=NULL, *CPUs=NULL;
    static uint32_t *hwStatus=NULL;
    static float *loads=NULL;
    int err;

    if (size < PSC_getNrOfNodes() * sizeof(*nodelist)) return -1;

    if (!hostStatus) {
	hostStatus = malloc(PSC_getNrOfNodes()*sizeof(*hostStatus));
	if (!hostStatus) return -1;
    }
    err = PSI_infoList(-1, PSP_INFO_LIST_HOSTSTATUS, NULL, hostStatus,
		       PSC_getNrOfNodes()*sizeof(*hostStatus), verbose);
    if (err<0) return -1;


    if (!totalJobs) {
	totalJobs = malloc(PSC_getNrOfNodes()*sizeof(*totalJobs));
	if (!totalJobs) return -1;
    }
    err = PSI_infoList(-1, PSP_INFO_LIST_ALLJOBS, NULL, totalJobs,
		       PSC_getNrOfNodes()*sizeof(*totalJobs), verbose);
    if (err<0) return -1;


    if (!normalJobs) {
	normalJobs = malloc(PSC_getNrOfNodes()*sizeof(*normalJobs));
	if (!normalJobs) return -1;
    }
    err = PSI_infoList(-1, PSP_INFO_LIST_NORMJOBS, NULL, normalJobs,
		       PSC_getNrOfNodes()*sizeof(*normalJobs), verbose);
    if (err<0) return -1;


    if (!CPUs) {
	CPUs = malloc(PSC_getNrOfNodes()*sizeof(*CPUs));
	if (!CPUs) return -1;
    }
    err = PSI_infoList(-1, PSP_INFO_LIST_VIRTCPUS, NULL, CPUs,
		       PSC_getNrOfNodes()*sizeof(*CPUs), verbose);
    if (err<0) return -1;


    if (!hwStatus) {
	hwStatus = malloc(PSC_getNrOfNodes()*sizeof(*hwStatus));
	if (!hwStatus) return -1;
    }
    err = PSI_infoList(-1, PSP_INFO_LIST_HWSTATUS, NULL, hwStatus,
		       PSC_getNrOfNodes()*sizeof(*hwStatus), verbose);
    if (err<0) return -1;


    if (!loads) {
	loads = malloc(PSC_getNrOfNodes()*3*sizeof(*loads));
	if (!loads) return -1;
    }
    err = PSI_infoList(-1, PSP_INFO_LIST_LOAD, NULL, loads,
		       PSC_getNrOfNodes()*3*sizeof(*loads), verbose);
    if (err<0) return -1;


    for (node=0; node<PSC_getNrOfNodes(); node++) {
	nodelist[node] = (NodelistEntry_t) {
	    .id = node,
	    .up = hostStatus[node],
	    .numCPU = CPUs[node],
	    .hwStatus = hwStatus[node],
	    .load = { loads[3*node+0], loads[3*node+1], loads[3*node+2] },
	    .totalJobs = totalJobs[node],
	    .normalJobs = normalJobs[node],
	    .maxJobs = -1 };
    }
    return PSC_getNrOfNodes() * sizeof(*nodelist);
}

int INFO_request_tasklist(PSnodes_ID_t node,
			  INFO_taskinfo_t taskinfo[], size_t size, int verbose)
{
    static uint16_t *totalJobs = NULL;
    int err;

    if (node<0 || node>=PSC_getNrOfNodes()) return -1;

    if (!totalJobs) {
	totalJobs = malloc(PSC_getNrOfNodes()*sizeof(*totalJobs));
	if (!totalJobs) return -1;
    }
    err = PSI_infoList(node, PSP_INFO_LIST_ALLJOBS, NULL,
		       totalJobs, PSC_getNrOfNodes()*sizeof(*totalJobs),
		       verbose);
    if (err<0) return -1;

    err = PSI_infoList(node, PSP_INFO_LIST_ALLTASKS, NULL,
		       taskinfo, size, verbose);
    if (err<0) return -1;

    return totalJobs[node];
}

int INFO_request_hwnum(int verbose)
{
    int num;
    int err = PSI_infoInt(-1, PSP_INFO_HWNUM, NULL, &num, verbose);

    if (err) return -1;

    return num;
}

int INFO_request_hwindex(char *hwType, int verbose)
{
    int index;
    int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hwType, &index, verbose);

    if (err) return -1;

    return index;
}

char *INFO_request_hwname(int32_t index, int verbose)
{
    static char hwname[80];
    int err = PSI_infoString(-1, PSP_INFO_HWNAME, &index,
			     hwname, sizeof(hwname), verbose);

    if (err) return NULL;

    return hwname;
}
