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
 * @brief Verify permissions of a pelogue script snippet
 *
 * Verify the correct permissions of the pelogue script snippet @a
 * filename.
 *
 * script snippets must not be writable by other users than root and
 * must be readable and executable by the owner.
 *
 * @param filename Name of the pelogue script snippet to verify
 *
 * @return Return true if @a filename has propoer stats or false otherwise
 */
static bool checkFileStats(char *filename)
{
    if (!filename) return false;

    struct stat statbuf;
    if (stat(filename, &statbuf) == -1 || !S_ISREG(statbuf.st_mode)) {
	flog("pelogue snippet %s not valid\n", filename);
	return false;
    }

    if (statbuf.st_mode & (S_IWGRP | S_IWOTH)
	|| (statbuf.st_uid != 0 && (statbuf.st_mode & S_IWUSR))) {
	flog("pelogue snippet %s might be vulnerable\n", filename);
	return false;
    }

    if ((statbuf.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR)) {
	flog("pelogue snippet %s with wrong permissions\n", filename);
	return false;
    }

    return true;
}

bool checkDDir(char *dDir)
{
    if (!dDir) return false;

    struct stat statbuf;
    if (stat(dDir, &statbuf) == -1 || !S_ISDIR(statbuf.st_mode)) {
	flog("dDir %s unavailable\n", dDir);
	return false;
    }
    if (statbuf.st_mode & (S_IWGRP | S_IWOTH)
	|| (statbuf.st_uid != 0 && (statbuf.st_mode & S_IWUSR))) {
	flog("dDir %s might be vulnerable\n", dDir);
	return false;
    }

    DIR *ddir = opendir(dDir);
    if (!ddir) {
	flog("cannot open dDir %s\n", dDir);
	return false;
    }

    struct dirent *dp;
    while ((dp = readdir(ddir))) {
	// skip entries that are not regular files
	if (dp->d_type != DT_REG && dp->d_type != DT_LNK) continue;
	// skip hidden files
	if (dp->d_name[0] == '.') continue;
	// skip files not ending with ".sh"
	size_t nameLen = strlen(dp->d_name);
	if (strcmp(dp->d_name + nameLen - 3, ".sh")) continue;

	char filePath[PATH_MAX];
	snprintf(filePath, sizeof(filePath), "%s/%s", dDir, dp->d_name);
	if (!checkFileStats(filePath)) {
	    closedir(ddir);
	    return false;
	}
    }
    closedir(ddir);
    return true;
}
