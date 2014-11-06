/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include "psslurmlog.h"

#include "pluginmalloc.h"
#include "pluginhelper.h"

#include "psslurmmultiprog.h"

typedef struct {
    char *exe;
    char *args;
} Multi_Prog_t;

static char *replaceArgSymbols(char *args, unsigned rank, unsigned offset)
{
    char *tmp, *buf = NULL, *ptr, *last;
    size_t len, bufSize = 0;
    char symbol[15];
    int haveOpenQuote = 0;

    if (!args) return ustrdup("");

    ptr = args;
    last = args;
    while (ptr && ptr[0] != '\n' && ptr[0] != '\0') {
	/* search for single quotes */
	if (ptr[0] == '\'') {
	    if (haveOpenQuote) {
		haveOpenQuote = 0;
	    } else {
		haveOpenQuote = 1;
	    }
	}

	/* parse symbols */
	if (ptr[0] == '%' &&
	    (ptr[1] == 't' || ptr[1] == 'o')) {

	    if (haveOpenQuote) {
		/* found quoted string, no parsing of symbols */
		if ((tmp = strchr(ptr+2, '\''))) {
		    len = tmp - last;
		    strn2Buf(last, len, &buf, &bufSize);
		    last = tmp;
		    ptr = tmp+1;
		    haveOpenQuote = 0;
		    continue;
		}
	    }

	    len = ptr - last;
	    strn2Buf(last, len, &buf, &bufSize);
	    if (ptr[1] == 't') {
		snprintf(symbol, sizeof(symbol), "%u", rank);
	    } else {
		snprintf(symbol, sizeof(symbol), "%u", offset);
	    }
	    str2Buf(symbol, &buf, &bufSize);
	    last = ptr+2;
	    ptr++;
	}

	ptr++;
    }

    if ((len = ptr - last) >0) {
	strn2Buf(last, len, &buf, &bufSize);
    }

    return buf;
}
static void unrollRanks(Multi_Prog_t *mp, uint32_t np, char *rankList,
			    char *executable, char *args)
{
    const char delimiters[] =",";
    char *saveptr, *range;
    unsigned int i, rank, min, max, count = 0;

    range = strtok_r(rankList, delimiters, &saveptr);

    while (range) {
	if (!(strchr(range, '-'))) {
	    if (range[0] == '*') {
		for (i=0; i<np; i++) {
		    if (!mp[i].exe) {
			mp[i].exe = ustrdup(executable);
			mp[i].args = replaceArgSymbols(args, i, count++);
		    }
		}
	    } else {
		if ((sscanf(range, "%u", &rank)) != 1 || rank >= np) {
		    mlog("%s: invalid rank '%s'\n", __func__, range);
		    exit(1);
		}
		mp[rank].exe = ustrdup(executable);
		mp[rank].args = replaceArgSymbols(args, rank, count++);
	    }
	} else {
	    if ((sscanf(range, "%u-%u", &min, &max)) != 2) {
		mlog("%s: invalid range '%s'\n", __func__, range);
		exit(1);
	    }
	    if (min>max) {
		mlog("%s: invalid range '%s'\n", __func__, range);
		exit(1);
	    }
	    for (i=min; i<=max; i++) {
		mp[i].exe = ustrdup(executable);
		mp[i].args = replaceArgSymbols(args, i, count++);
	    }
	}
	range = strtok_r(NULL, delimiters, &saveptr);
    }
}

static void parseMultiProgConf(char *conf, Multi_Prog_t *mp, uint32_t np)
{
    char *line, *saveptr, *tmp, *rank, *executable, *args, *sepSpace, *sepTab;
    const char delimiters[] ="\n";

    line = strtok_r(conf, delimiters, &saveptr);
    while (line) {
	/* skip comments and empty lines */
	if (line[0] == '#' || line[0] == '\0') {
	    line = strtok_r(NULL, delimiters, &saveptr);
	    continue;
	}

	line = trim(line);

	/* rank range (1,7,2-3) */
	rank = line;

	/* executable */
	sepSpace = strchr(line, ' ');
	sepTab = strchr(line, '\t');

	if (!sepSpace && !sepTab) {
	    mlog("%s: invalid executable for '%s'\n", __func__, line);
	    exit(1);
	}
	if (!sepSpace) {
	    tmp = sepTab;
	} else if (!sepTab) {
	    tmp = sepSpace;
	} else {
	    tmp = (sepSpace < sepTab) ? sepSpace : sepTab;
	}

	executable = tmp+1;
	tmp[0] = '\0';
	executable = ltrim(executable);

	/* arguments (task: %t) */
	if (!(tmp = strchr(executable, ' '))) {
	    args = NULL;
	} else {
	    args = tmp+1;
	    tmp[0] = '\0';
	    args = ltrim(args);
	}

	/*
	mlog("%s: rank '%s' exe '%s' args '%s'\n", __func__, rank,
		executable, args);
	*/
	unrollRanks(mp, np, rank, executable, args);

	line = strtok_r(NULL, delimiters, &saveptr);
    }
}

void setupArgsFromMultiProg(Step_t *step, char **argv, int *argc)
{
    Multi_Prog_t *mp;
    uint32_t i, exeCount = 0, uniqExeCount = 0;
    int startArgc = *argc;
    char *lastExe = NULL, *lastArgs = NULL;
    char np[128];

    /* TODO: must grow argv dynamically */
    mp = umalloc(step->np * sizeof(Multi_Prog_t));
    for (i=0; i<step->np; i++) mp[i].exe = mp[i].args = NULL;

    /* parse the multi prog conf */
    parseMultiProgConf(step->argv[1], mp, step->np);

    /* generate arguments for every executable */
    lastExe = mp[0].exe;
    lastArgs = mp[0].args;

    for (i=0; i<step->np; i++) {
	if (!strcmp(mp[i].exe, lastExe) && !strcmp(mp[i].args, lastArgs)) {
	    exeCount++;
	} else {
	    if (*argc != startArgc) {
		argv[(*argc)++] = ustrdup(":");
	    }
	    argv[(*argc)++] = ustrdup("-np");
	    snprintf(np, sizeof(np), "%u", exeCount);
	    argv[(*argc)++] = ustrdup(np);
	    argv[(*argc)++] = ustrdup(lastExe);
	    if (strlen(lastArgs) >0) {
		argv[(*argc)++] = ustrdup(lastArgs);
	    }
	    /*
	    mlog("%s: generate argv '%i' save argv exeCount '%s' "
		    "lastExe '%s' lastArgs '%s'\n", __func__, i, np,
		    lastExe, lastArgs);
	    */

	    lastExe = mp[i].exe;
	    lastArgs = mp[i].args;
	    exeCount = 1;
	    uniqExeCount++;
	}
    }

    if (uniqExeCount) argv[(*argc)++] = ustrdup(":");
    argv[(*argc)++] = ustrdup("-np");
    snprintf(np, sizeof(np), "%u", exeCount);
    argv[(*argc)++] = ustrdup(np);
    argv[(*argc)++] = ustrdup(lastExe);
    if (strlen(lastArgs) >0) {
	argv[(*argc)++] = ustrdup(lastArgs);
    }
    /*
    mlog("%s: generate argv '' save argv exeCount '%s' "
	    "lastExe '%s' lastArgs '%s'\n", __func__, np,
	    lastExe, lastArgs);
    */
}
