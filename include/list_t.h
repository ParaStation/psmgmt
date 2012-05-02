/*
 * ParaStation
 *
 * Copyright (C) 2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Simple doubly linked list.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 */
#ifndef __LIST_T_H
#define __LIST_T_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

struct list_head {
    struct list_head *next, *prev;
};

typedef struct list_head list_t;

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __LIST_T_H */
