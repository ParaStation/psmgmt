/*
 * ParaStation
 *
 * Copyright (C) 2009-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Simple doubly linked list type
 */
#ifndef __LIST_T_H
#define __LIST_T_H

struct list_head {
    struct list_head *next, *prev;
};

typedef struct list_head list_t;

#endif  /* __LIST_T_H */
