/*
 *               ParaStation3
 * info.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.h,v 1.19 2003/09/12 13:53:36 eicker Exp $
 *
 */
/**
 * @file
 * info: Functions for information retrieving from ParaStation daemon
 *
 * $Id: info.h,v 1.19 2003/09/12 13:53:36 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __INFO_H
#define __INFO_H

#include <sys/types.h>
#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Documentation */

/*****************************
 *
 * request_rdpstatus(int nodeno)
 *
 * requests the status of RDP on the local PSID to the node nodeno
 * RETURN: filled buffer
 *
 */
int INFO_request_rdpstatus(int nodeno, void* buffer, size_t size, int verbose);

/*****************************
 *
 * request_mcaststatus(int nodeno)
 *
 * requests the status of MCast on the local PSID to the node nodeno
 * RETURN: filled buffer
 *
 */
int INFO_request_mcaststatus(int nodeno,
			     void* buffer, size_t size, int verbose);

/*****************************
 *
 * request_countstatus(int nodeno)
 *
 */
int INFO_request_countheader(int nodeno, int hwindex,
			     void* buffer, size_t size, int verbose);

int INFO_request_countstatus(int nodeno, int hwindex,
			     void* buffer, size_t size, int verbose);

/*****************************
 *
 * request_hoststatus(void *buffer, int size)
 *
 * requests the status of all hosts on the local PSID
 * RETURN: filled buffer
 *
 */
int INFO_request_hoststatus(void* buffer, size_t size, int verbose);

/*****************************
 *
 * request_hostlist(void *buffer, int size)
 *
 * requests a list of all nodes
 * RETURN: filled buffer
 *
 */
int INFO_request_nodelist(NodelistEntry_t *buffer, size_t size, int verbose);

/*****************************
 *
 * request_partition(void *buffer, int size)
 *
 * requests a list of nodes conforming hwtype
 * RETURN: filled buffer
 *
 */
int INFO_request_partition(unsigned int hwtype,
			   NodelistEntry_t *buffer, size_t size, int verbose);

int INFO_request_rankID(unsigned int rank, int verbose);

int INFO_request_taskSize(int verbose);

/*****************************
 *
 * request_host(unsigned int address)
 *
 * requests the PS id for host with IP-address address
 * RETURN: the PS id
 *
 */
int INFO_request_host(unsigned int addr, int verbose);

/*****************************
 *
 * request_node(int node)
 *
 * requests the IP-address for host with PS id node
 * RETURN: the IP-address
 *
 */
unsigned int INFO_request_node(int node, int verbose);

typedef struct {
    long tid;
    long ptid;
    long loggertid;
    uid_t uid;
    long group;
    int rank;
    int connected;
} INFO_taskinfo_t;

/*****************************
 *
 * request_countstatus(int nodeno)
 * size in byte!
 * Liest solange nach taskinfo, bis array voll, zählt dann aber weiter.
 * Gibt Anzahl der tasks zurück.
 *
 */
int INFO_request_tasklist(int nodeno, INFO_taskinfo_t taskinfo[], size_t size,
			  int verbose);

int INFO_request_nrofnodes(int verbose);

char *INFO_request_instdir(int verbose);

char *INFO_request_psidver(int verbose);

int INFO_request_hwnum(int verbose);

int INFO_request_hwindex(char *type, int verbose);

char *INFO_request_hwname(int index, int verbose);

char *INFO_printHWType(unsigned int hwType);

/**
 * Type of taskinfo request
 */
typedef enum {
    INFO_ISALIVE = 0x02,    /**< check if the tid is alive */
    INFO_PTID = 0x03,       /**< get the parents TID */
    INFO_LOGGERTID = 0x04,  /**< get the loggers TID */
    INFO_UID = 0x05,        /**< get the uid of the task */
    INFO_RANK = 0x06        /**< get the rank of the task */
} INFO_info_t;

/*----------------------------------------------------------------------*/
/*
 * INFO_request_taskinfo(PSTID tid,what)
 *
 *  gets the user id of the given task identifier tid
 *  @todo Das stimmt nicht, es gibt verschiedene Aufgaben.
 *  RETURN the uid of the task
 */
long INFO_request_taskinfo(long tid, INFO_info_t what, int verbose);

int INFO_request_option(unsigned short node, int num, long option[],
			long value[], int verbose);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __INFO_H */
