/*
 *               ParaStation
 * pspartition.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pspartition.h,v 1.3 2004/01/09 15:10:58 eicker Exp $
 *
 */
/**
 * @file
 * Basic enumerations for partition creation and reservation.
 *
 * $Id: pspartition.h,v 1.3 2004/01/09 15:10:58 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSPARTITION_H
#define __PSPARTITION_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Various sort modes for partition creation. */
typedef enum {
    PART_SORT_NONE,           /**< No sorting at all. */
    PART_SORT_PROC,           /**< Sort by number of ParaStation processes. */
    PART_SORT_LOAD_1,         /**< Sort by 1 minute load average. */
    PART_SORT_LOAD_5,         /**< Sort by 5 minute load average. */
    PART_SORT_LOAD_15,        /**< Sort by 15 minute load average. */
    PART_SORT_PROCLOAD,       /**< Sort by load_1 + proc. */
    PART_SORT_UNKNOWN = 0x0ff /**< Dummy for unknown sort mode. */
} PSpart_sort_t;

/** Options possibly bound to a partition. */
typedef enum {
    PART_OPT_NODEFIRST = 0x0001, /**< Place consecutive processes on different
				    nodes, if possible. Usually consecutive
				    processes are placed on the same node. */
    PART_OPT_EXCLUSIVE = 0x0002, /**< Only get exclusive nodes. I.e. no further
				    processes are allowed on that node. */
    PART_OPT_OVERBOOK = 0x0004,  /**< Allow more than one process per
				    node. This induces @ref PART_OPT_EXCLUSIVE
				    implicitely. */
    PART_OPT_WAIT = 0x0008,      /**< If not enough nodes are available, wait
				    for them (batch mode). */
} PSpart_option_t;

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSPARTITION_H */
