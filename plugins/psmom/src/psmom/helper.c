/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "psmomlog.h"
#include "psmom.h"
#include "pluginmalloc.h"

#include "helper.h"

int strToInt(char *string)
{
    int num;

    if ((sscanf(string, "%d", &num)) != 1) {
	return 0;
    }
    return num;
}

unsigned long sizeToBytes(char *string)
{
    unsigned long size;
    char suf[11];
    int s_word = sizeof(int);

    struct convTable conf_table[] =
    {
	{ "b",	1 },
	{ "kb",	1024 },
	{ "mb",	1024 * 1024 },
	{ "gb",	1024 * 1024 * 1024 },
	{ "w",	s_word },
	{ "kw",  s_word * 1024 },
	{ "mw",  s_word * 1024 * 1024 },
	{ "gw",  s_word * 1024 * 1024 * 1024 },
	{ "", 0 },
    };

    struct convTable *ptr = conf_table;


    if ((sscanf(string, "%lu%10s", &size, suf)) > 2) {
	return 0;
    }

    while (ptr->mult !=  0) {
	if (!(strcmp(ptr->format, suf))) {
	    return ptr->mult * size;
	}
	ptr++;
    }

    return 0;
}

unsigned long stringTimeToSec(char *wtime)
{
    int count = 0;
    int arg1 = 0, arg2 = 0, arg3 = 0;

    if (!wtime) return 0;

    if ((count = sscanf(wtime, "%d:%d:%d", &arg1, &arg2, &arg3)) > 3) {
	//mlog("%s: got c:%d, 1:%d 2:%d 3:%d\n", __func__, count, arg1, arg2, arg3);
	return 0;
    }

    switch (count) {
	case 0:
	    return 0;
	case 1:
	    return arg1;
	case 2:
	    return (arg1 * 60) + arg2;
	case 3:
	    return (arg1 * 3600) + (arg2 * 60) + arg3 ;
    }
    return 0;
}

int removeDir(char *directory, int root)
{
    struct dirent *d;
    DIR *dir;
    char buf[400];
    struct stat sbuf;

    if (!(dir = opendir(directory))) {
	return 0;
    }

    while ((d = readdir(dir))) {
	if ((!strcmp(d->d_name, ".") || !(strcmp(d->d_name, "..")))) continue;
	snprintf(buf, sizeof(buf), "%s/%s", directory, d->d_name);
	stat(buf, &sbuf);

	if (S_ISDIR(sbuf.st_mode)) {
	    /* remove all dirs recursive */
	    removeDir(buf, 1);
	} else {
	    remove(buf);
	}
    }

    /* delete also the root directory */
    if (root) {
	remove(directory);
    }

    closedir(dir);
    return 1;
}
