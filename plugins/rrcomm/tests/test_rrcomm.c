/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>

#include "rrcomm.h"

int main(void)
{
    RRC_init();

    printf("Hello world!\n");

    RRC_finalize();

    return 0;
}
