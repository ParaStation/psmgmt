/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "peloguescript.h"

#include <sys/stat.h>

int checkPELogueFileStats(char *filename, bool root)
{
    struct stat statbuf;

    if (stat(filename, &statbuf) == -1) return -1;

    if (root) {
	/* readable and executable by root and NOT writable by anyone
	 * besides root */
	if (statbuf.st_uid != 0) return -2;
	if (!S_ISREG(statbuf.st_mode) ||
	    ((statbuf.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR)) ||
	    (statbuf.st_mode & (S_IWGRP | S_IWOTH))) {
	    return -2;
	}
    } else {
	/* readable and executable by root and other  */
	if ((statbuf.st_mode & (S_IROTH | S_IXOTH)) != (S_IROTH | S_IXOTH)) {
	    return -2;
	}
    }
    return 1;
}
