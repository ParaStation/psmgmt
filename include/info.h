/*
 *               ParaStation
 * info.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.h,v 1.23 2003/11/26 17:10:58 eicker Exp $
 *
 */
/**
 * @file
 * info: Deprecated functions for information retrieving from
 * ParaStation daemon
 *
 * $Id: info.h,v 1.23 2003/11/26 17:10:58 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#warning "Obsolete library. Will be removed soon."

#ifndef __INFO_H
#define __INFO_H

#include <stdint.h>

#include "psnodes.h"
#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * Structure returned by @ref INFO_request_tasklist(). This is just
 * handed over from @c psprotocol.h
 */
typedef PSP_taskInfo_t INFO_taskinfo_t;

/**
 * @brief Get a up to date counter status.
 *
 * Get a up to date counter status of the hardware @a hwindex from
 * node @a node. The counter status string created is stored to the
 * buffer @a buf.
 *
 * @param node The ParaStation ID of the node to request 
 *
 * @param hwindex Hardware index of the hardware to request. 
 *
 * @param buf Buffer to store the requested counter string in.
 *
 * @param size Size of the buffer @a buf.
 *
 * @param verbose Flag to be more verbose, if something within the
 * information retrival went wrong.
 *
 * @return On success, the length of the string received is
 * returned. Otherwise, e.g. if the space provided within buffer is to
 * small or some other error occurred, -1 is returned.
 *
 * @deprecated Better use @ref
 * PSI_infoString(..,PSP_INFO_COUNTSTATUS,...) in order to get the
 * requested information.
 */
int INFO_request_countstatus(PSnodes_ID_t node, int32_t hwindex,
			     void* buf, size_t size, int verbose);

/**
 * @brief Get a up to date host status.
 *
 * Get a up to date host status. Each host is represented by a
 * character within the buffer @a buf, indexed by the hosts
 * ParaStation ID.
 *
 * @param buf Buffer to store the requested host status in.
 *
 * @param size Size of the buffer @a buf. Size has to be at least the
 * number of nodes within the ParaStation cluster.
 *
 * @param verbose Flag to be more verbose, if something within the
 * information retrival went wrong.
 *
 * @return On success, the length of the status array received is
 * returned. This usually equals to the result of @ref
 * PSC_getNrOfNodes().  Otherwise, e.g. if the space provided within
 * buffer is to small or some other error occurred, -1 is returned.
 *
 * @deprecated Better use @ref
 * PSI_infoList(..,PSP_INFO_LIST_HOSTSTATUS,...) in order to get the
 * requested information.
 */
int INFO_request_hoststatus(void* buf, size_t size, int verbose);

/**
 * @brief Get a up to date nodelist.
 *
 * Get a up to date nodelist.  Each node is represented by a @ref
 * NodelistEntry_t structure within the buffer @a buf, indexed by the
 * hosts ParaStation ID.
 *
 * @warning The member @a maxJobs within the @ref NodelistEntry_t
 * structure will not contain the correct value stored on the
 * nodes. In order to get this information, use a call to @ref
 * PSI_infoOption() with the @a option parameter set to @a
 * PSP_OP_PROCLIMIT.
 *
 * @param buf Buffer to store the requested nodelist in.
 *
 * @param size Size of the buffer @a buf.
 *
 * @param verbose Flag to be more verbose, if something within the
 * information retrival went wrong.
 *
 * @return On success, the length of the nodelist received in byte is
 * returned. Otherwise, e.g. if the space provided within buffer is to
 * small or some other error occurred, -1 is returned.
 *
 * @deprecated Better use one or more calls to @ref
 * PSI_infoList(..,what,...) with @a what being one of @a
 * PSP_INFO_LIST_HOSTSTATUS, @a PSP_INFO_LIST_ALLJOBS, @a
 * PSP_INFO_LIST_NORMJOBS, @a PSP_INFO_LIST_VIRTCPUS, @a
 * PSP_INFO_LIST_HWSTATUS or @a PSP_INFO_LIST_LOAD in order to get to
 * corresponding information.
 *
 * 
 in order to
 * get the requested information.
 */
int INFO_request_nodelist(NodelistEntry_t *buf, size_t size, int verbose);

/**
 * @brief Get a up to date tasklist.
 *
 * Get a up to date tasklist of node @a node. Each task is represented
 * by a @ref INFO_taskinfo_t structure within the buffer @a taskinfo,
 * indexed by the task's running number.
 *
 * @param node The ParaStation ID of the node to request 
 *
 * @param taskinfo Buffer to store the requested tasklist in.
 *
 * @param size Size of the buffer @a taskinfo.
 *
 * @param verbose Flag to be more verbose, if something within the
 * information retrival went wrong.
 *
 * @return On success, the total number of jobs running on node @a
 * node is returned, even if this number is bigger than the
 * corresponding space provided within @a taskinfo. Otherwise, e.g. if
 * some error occurred, -1 is returned.
 *
 * @deprecated Better use one or more calls to @ref
 * PSI_infoString(..,what,...) with @a what being one of @a
 * PSP_INFO_LIST_ALLJOBS or @a PSP_INFO_LIST_ALLTASKS in order to get
 * to requested information.
 */
int INFO_request_tasklist(PSnodes_ID_t node,
			  INFO_taskinfo_t taskinfo[], size_t size,int verbose);

/**
 * @brief Get the number of supported hardware types.
 *
 * Get the number of hardware types supported by the ParaStation
 * system in the actual configuration. This corresponds to the number
 * of @c Hardware entries within the ParaStation configuration file.
 *
 * @param verbose Flag to be more verbose, if something within the
 * information retrival went wrong.
 *
 * @return On success, the number of supported hardware types is
 * returned. Otherwise -1 is returned.
 *
 * @deprecated Better use @ref PSI_infoInt(..,PSP_INFO_HWNUM,...) in
 * order to get to requested information.
 */
int INFO_request_hwnum(int verbose);

/**
 * @brief Get a hardware index.
 *
 * Get the actual index of the hardware named as @a type within the
 * ParaStation configuration file.
 *
 * @param type The name of the hardware the index is requested for.
 *
 * @param verbose Flag to be more verbose, if something within the
 * information retrival went wrong.
 *
 * @return On success, the actual hardware index is
 * returned. Otherwise -1 is returned.
 */
int INFO_request_hwindex(char *type, int verbose);

/**
 * @brief Get a hardware's name.
 *
 * Get the name of the hardware with index number @a index.
 *
 * @param index The index of the requested hardware name.
 *
 * @param verbose Flag to be more verbose, if something within the
 * information retrival went wrong.
 *
 * @return On success, a pointer to a static string that contains the
 * hardware name is returned. Successiv calls to this function might
 * change this string, i.e. don't count on the content of this string
 * after further calls to this function. If an error occurred during
 * information retrival, NULL is returned.
 */
char *INFO_request_hwname(int32_t index, int verbose);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __INFO_H */
