/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "peloguescript.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "peloguelog.h"

/**
 * @brief Verify permissions of a pelogue script
 *
 * Verify the correct permissions of a pelogue script contained in
 * @a filename. If @a root is true, this function checks for root's
 * permission to read and execute this file and that no other users
 * are allowed to modify it. Otherwise it checks for allowance to read
 * and execute this file for root and other users.
 *
 * @param filename Pelogue file to verify
 *
 * @param root Flag to check only for root's permissions
 *
 * @return Returns 1 on success, i.e. if the permission are set as
 * stated above. If the file is non-existing or stat() fails, -1 is
 * returned. If the file does not match the checked permissions, -2 is
 * returned.
 */
int checkPELogueFileStats(char *filename, bool root)
{
    struct stat statbuf;
   if (stat(filename, &statbuf) == -1) return -1;

    if (root) {
	/* readable and executable by root and NOT writable by anyone
	 * besides root */
	if (statbuf.st_uid != 0) return -2;
	if (!S_ISREG(statbuf.st_mode)
	    || ((statbuf.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR))
	    || (statbuf.st_mode & (S_IWGRP | S_IWOTH))) {
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

bool checkDDir(char *dDir, bool root)
{
    DIR *ddir = opendir(dDir);
    if (!ddir) {
	flog("cannot open pelogue .d directory %s\n", dDir);
	return false;
    }

    char filePath[PATH_MAX];
    struct dirent *dp;
    while ((dp = readdir(ddir))) {
	// Skip "." and ".."
	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;

	snprintf(filePath, sizeof(filePath), "%s/%s", dDir, dp->d_name);
	if (checkPELogueFileStats(filePath, root) != 1) {
	    flog("file '%s' does not have correct permissions\n", filePath);
	    closedir(ddir);
	    return false;
	}
    }
    closedir(ddir);
    return true;
}
