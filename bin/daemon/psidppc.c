/*
 *               ParaStation
 *
 * Copyright (C) 2008-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "psidutil.h"
#include "psidhw.h"


int PSID_PPC(void)
{
#if defined __PPC__
    return 1;
#else
    return 0;
#endif
}

#define CPUTREE_PATH "/proc/device-tree/cpus"

long PSID_getPhysCPUs_PPC(void)
{
#if defined __PPC__
    long virtCPUs = PSID_getVirtCPUs(), physCPUs = 0, myVirt = 0;
    DIR *dirs;
    FILE *typeFile;
    struct dirent *dirent;
    struct stat statent;
    char line[1024], name[PATH_MAX];

    dirs = opendir(CPUTREE_PATH);
    if (!dirs) {
	PSID_warn(-1, errno, "%s: opendir() failed", __func__);
	return virtCPUs;
    }

    while ((dirent = readdir(dirs))) {
	if (dirent->d_type != DT_DIR) continue;

	if (!strcmp(dirent->d_name, ".")
	    || !strcmp(dirent->d_name, "..")) continue;

	strncpy(name, CPUTREE_PATH, sizeof(name));
	strncat(name, "/", sizeof(name)-strlen(name));
	strncat(name, dirent->d_name, sizeof(name)-strlen(name));
	strncat(name, "/device_type", sizeof(name)-strlen(name));

	typeFile = fopen(name, "r");
	if (!typeFile) continue;

	fgets(line, sizeof(line), typeFile);

	if (strcmp(line, "cpu")) continue;
	fclose(typeFile);

	physCPUs++;

	strncpy(name, CPUTREE_PATH, sizeof(name));
	strncat(name, "/", sizeof(name)-strlen(name));
	strncat(name, dirent->d_name, sizeof(name)-strlen(name));
	strncat(name, "/ibm,ppc-interrupt-server#s",sizeof(name)-strlen(name));

	if (stat(name, &statent)) {
	    if (errno != ENOENT)
		PSID_warn(-1, errno, "%s: opendir() failed", __func__);
	    myVirt++;
	    continue;
	}

	myVirt += statent.st_size/sizeof(int);
    }
    closedir(dirs);

    if (myVirt != virtCPUs) {
	PSID_log(-1, "%s: %ld virt. CPUs found, but sysconf() reports %ld.\n",
		 __func__, myVirt, virtCPUs);
	return virtCPUs;
    }

    return physCPUs;
#else
    return PSID_getVirtCPUs();
#endif
}
