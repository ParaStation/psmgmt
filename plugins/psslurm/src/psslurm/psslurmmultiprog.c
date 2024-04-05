/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmmultiprog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "pslog.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "psslurmlog.h"
#include "psslurmio.h"

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
    while (ptr[0] != '\n' && ptr[0] != '\0') {
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
    const char delimiters[] = ",";
    char *saveptr, *range;
    unsigned int i, rank, min, max, count = 0;

    range = strtok_r(rankList, delimiters, &saveptr);

    while (range) {
	if (!strchr(range, '-')) {
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
    char *saveptr;
    const char delimiters[] ="\n";

    char *line = strtok_r(conf, delimiters, &saveptr);
    while (line) {
	/* skip comments and empty lines */
	if (line[0] == '#' || line[0] == '\0') {
	    line = strtok_r(NULL, delimiters, &saveptr);
	    continue;
	}

	line = trim(line);

	/* rank range (1,7,2-3) */
	char *rank = line;

	/* executable */
	char *sepSpace = strchr(line, ' ');
	char *sepTab = strchr(line, '\t');

	if (!sepSpace && !sepTab) {
	    mlog("%s: invalid executable for '%s'\n", __func__, line);
	    exit(1);
	}

	char *tmp, *args;
	if (!sepSpace) {
	    tmp = sepTab;
	} else if (!sepTab) {
	    tmp = sepSpace;
	} else {
	    tmp = (sepSpace < sepTab) ? sepSpace : sepTab;
	}

	char *executable = tmp+1;
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

/* ******************************** *
 * Argument Splitting State Machine *
 * ******************************** */

static int const chunksize = 5;

typedef enum {
    betweenword,
    inword,
    in_single,
    in_double,
    finished,
} state_t;

static char ** push(char **argv, size_t *argc, char *pos)
{
    if (*argc % chunksize == 0) {
	argv = urealloc(argv, sizeof(char*) * (*argc + chunksize));
    }

    /* privious string is now complete, so duplicate it */
    if (*argc != 0) {
	argv[(*argc)-1] = ustrdup(argv[(*argc)-1]);
    }

    argv[(*argc)++] = pos;

    return argv;
}

static char ** splitArguments(char *args)
{
    if (!args) {
	flog("invalid argument pointer\n");
	return NULL;
    }
    /* out will hold all argv strings */
    char *out = umalloc(strlen(args) + 1);

    size_t argc = 0;
    char ** argv = NULL;

    char *pos = args, *outpos = out;
    state_t state = betweenword;
    char *warning = NULL;

    while (state != finished) {
	char c = *pos++;

#if 0
	if (argc == 0) {
	    memset(out, 0, strlen(args) + 1);
	} else {
	    mlog("%s: current argument so far: %s\n", __func__, argv[argc-1]);
	}
#endif

	switch (state) {

	case inword:
	    switch (c) {
	    case '\'':
		state = in_single;
		break;
	    case '"':
		state = in_double;
		break;
	    case '\\':
		c = *pos++;
		if (c == '\0') {
		    warning = "single slash at end";
		    state = finished;
		    *outpos++ = '\\';
		}
		*outpos++ = c;
		break;
	    case ' ':
	    case '\t':
	    case '\v':
	    case '\n':
	    case '\r':
		state = betweenword;
		/* finish current argv */
		*outpos++ = '\0';
		break;
	    case '\0':
		state = finished;
		/* fallthrough */
	    default:
		*outpos++ = c;
		break;
	    }
	    break;

	case betweenword:
	    switch (c) {
	    case ' ':
	    case '\t':
	    case '\v':
	    case '\n':
	    case '\r':
		break;
	    case '\0':
		state = finished;
		*outpos++ = '\0';
		break;
	    default:
		state = inword;
		--pos; /* "unread" */
		/* start new argv */
		argv = push(argv, &argc, outpos);
		break;
	    }
	    break;

	case in_single:
	    switch (c) {
	    case '\'':
		state = inword;
		break;
	    case '\0':
		warning = "unfinished single quote";
		state = finished;
		/* fallthrough */
	    default:
		*outpos++ = c;
		break;
	    }
	    break;

	case in_double:
	    switch (c) {
	    case '\\':
		c = *pos++;
		if (c == '\0') {
		    warning = "unfinished double quote, single slash at end";
		    state = finished;
		    *outpos++ = '\\';
		} else if (c != '\\' && c != '"' && c != '\n')  {
		    *outpos++ = '\\';
		}
		*outpos++ = c;
		break;
	    case '"':
		state = inword;
		break;
	    case '\0':
		warning = "unfinished double quote";
		state = finished;
		/* fallthrough */
	    default:
		*outpos++ = c;
	    }
	    break;

	case finished:
	    assert(0);
	}
    }

    if (warning) {
	mlog("%s: Failed to split arguments: %s\n", __func__, warning);
	/* slurm is tolerant
	free(argv);
	return NULL;
	*/
    }
    argv = push(argv, &argc, NULL);
    ufree(out);

    return argv;
}

static int addExecutableToArgv(strv_t argV, const char *lastExe,
			       uint32_t exeCount, char *lastArgs)
{
    char np[128];

    strvAdd(argV, "-np");
    snprintf(np, sizeof(np), "%u", exeCount);
    strvAdd(argV, np);
    strvAdd(argV, lastExe);

#if 0
    mlog("%s: Adding executable '%s'\n", __func__, lastExe);
#endif

    char **tmpArgs = splitArguments(lastArgs);
    if (!tmpArgs) {
	return -1;
    }

    for (int i = 0; tmpArgs[i]; i++) {
#if 0
	flog("adding argument '%s'\n", tmpArgs[i]);
#endif
	strvLink(argV, tmpArgs[i]);
#if 0
	flog("argv generated so far: ");
	for (char **str = strvGetArray(argV); *str; str++) mlog("%s ", *str);
	mlog("\n");
#endif
    }

    ufree(tmpArgs);

    return 0;
}

void setupArgsFromMultiProg(Step_t *step, Forwarder_Data_t *fwdata, strv_t argV)
{
    Multi_Prog_t *mp = umalloc(step->np * sizeof(Multi_Prog_t));
    for (uint32_t i = 0; i < step->np; i++) mp[i].exe = mp[i].args = NULL;

    /* parse the multi prog conf */
    parseMultiProgConf(step->argv[1], mp, step->np);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_PROCESS) {
	flog("Got following multiprog data:\n");
	for (uint32_t i = 0; i < step->np; i++) {
	    flog("  %i:  %s   %s\n", i, mp[i].exe, mp[i].args);
	}
    }

    /* generate arguments for every executable */
    uint32_t exeCount = 0, uniqExeCount = 0;
    char *lastExe = mp[0].exe;
    char *lastArgs = mp[0].args;

    size_t startArgc = strvSize(argV);
    for (uint32_t i = 0; i < step->np; i++) {
	if (!mp[i].exe) goto setup_error;
	if (!strcmp(mp[i].exe, lastExe) && !strcmp(mp[i].args, lastArgs)) {
	    /* count how often this exe/args combination should run (==np) */
	    exeCount++;
	    continue;
	}

	if (strvSize(argV) != startArgc) {
	    /* this is not the first executable, separate using colon */
	    strvAdd(argV, ":");
	}

	if (addExecutableToArgv(argV, lastExe, exeCount, lastArgs) != 0) {
	    goto setup_error;
	}
#if 0
	flog("generated argv %i with exeCount '%s' lastExe '%s' lastArgs '%s'\n",
	     i, np, lastExe, lastArgs);
#endif

	lastExe = mp[i].exe;
	lastArgs = mp[i].args;
	exeCount = 1;
	uniqExeCount++;
    }

    /* now add the last exe/args combo */

    if (uniqExeCount) {
	/* we found more than one exe/args combination */
	strvAdd(argV, ":");
    }

    if (addExecutableToArgv(argV, lastExe, exeCount, lastArgs) != 0) {
	goto setup_error;
    }

    ufree(mp);

#if 0
    flog("generated last argv with exeCount '%s' lastExe '%s' lastArgs '%s'\n",
	 np, lastExe, lastArgs);
#endif

#if 0
    flog("complete argv: ");
    for (char **str = strvGetArray(argV); *str; str++) mlog("%s ", *str);
    mlog("\n");
#endif

    return;

    char *msg;
setup_error:
    msg = "Error setting up arguments from multiprog file.\n";
    IO_printStepMsg(fwdata, msg, strlen(msg), 0, STDERR);
    exit(1);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
