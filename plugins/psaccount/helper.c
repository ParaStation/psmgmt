/*
 * ParaStation
 *
 * Copyright (C) 2010-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psaccountlog.h"
#include "helper.h"

void *umalloc(size_t size, const char *func)
{
    void *ptr;

    if (!(ptr = malloc(size))) {
        fprintf(stderr, "%s: memory allocation failed\n", func);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *urealloc(void *old ,size_t size, const char *func)
{
    void *ptr;

    if (!(ptr = realloc(old, size))) {
        fprintf(stderr, "%s: realloc failed.\n", func);
        exit(EXIT_FAILURE);
    }
    return ptr;
}
