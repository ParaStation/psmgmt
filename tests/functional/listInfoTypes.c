/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>

#include "psprotocol.h"

int main(void)
{
    PSP_Info_t info;
    for(info = 0; info < 64; info++) {
	printf("%d\t%#.3x\t%s\n", info, info, PSP_printInfo(info));
    }

    return 0;
}
