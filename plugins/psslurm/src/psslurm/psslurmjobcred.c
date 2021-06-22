/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <string.h>
#include <ctype.h>

#include "pluginmalloc.h"

#include "psslurmlog.h"

#include "psslurmjobcred.h"

bool *getCPUsetFromCoreBitmap(uint32_t total, const char *bitmap)
{
    bool *coreMap = ucalloc(total * sizeof(*coreMap));

    const char *bitstr = bitmap;
    if (!strncmp(bitstr, "0x", 2)) bitstr += 2;

    size_t len = strlen(bitstr);

    int count = 0;

    /* parse slurm bit string in LSB first order */
    while (len--) {
	int cur = (int)bitstr[len];

	if (!isxdigit(cur)) {
	    mlog("%s: invalid character in core map sting '%c'\n", __func__,
		    cur);
	    ufree(coreMap);
	    return NULL;
	}

	if (isdigit(cur)) {
	    cur -= '0';
	} else {
	    cur = toupper(cur);
	    cur -= 'A' - 10;
	}

	for (int32_t i = 1; i <= 8; i *= 2) {
	    if (cur & i) coreMap[count] = true;
	    count++;
	}
    }

    if (psslurmlogger->mask & PSSLURM_LOG_PART) {
	flog("cores '%s' coreMap '", bitstr);
	for (uint32_t i = 0; i < total; i++) mlog("%i", coreMap[i]);
	mlog("'\n");
    }

    return coreMap;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
