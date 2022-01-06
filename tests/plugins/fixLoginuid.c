/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "plugin.h"
#include "psidutil.h"

int requiredAPI = 107;

char name[] = "fixLoginuid";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

int initialize(FILE *logfile)
{
    FILE *fd;
    char fileName[128];
    struct stat sbuf;

    snprintf(fileName, sizeof(fileName), "/proc/%i/loginuid", getpid());
    if (stat(fileName, &sbuf) == -1) {
	PSID_warn(-1, errno, "%s: stat(%s)", name, fileName);
	return 1;
    }

    fd = fopen(fileName,"w");
    if (!fd) {
	PSID_warn(-1, errno, "%s: fopen(%s)", name, fileName);
	return 1;
    }
    fprintf(fd, "%d", getuid());
    fclose(fd);

    return 0;
}

char * help(void)
{
    return strdup("\tFix loginuid settings of main psid.\n");
}
