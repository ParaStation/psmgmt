/*
 *               ParaStation3
 * parsing.c
 *
 * General parser utility for ParaStation daemon and admin
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: parser.c,v 1.1 2002/04/30 17:49:02 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: parser.c,v 1.1 2002/04/30 17:49:02 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "errlog.h"

#include "parser.h"

static FILE *parsefile;

static int parseline;

static char errtxt[256];

static char *nextline(void)
{
    static char line[256];

    do {
	parseline++;

	if (!fgets(line, sizeof(line), parsefile)) {
	    errlog("Got EOF", 5);
	    line[0] = '\0';
	    return line;
	}

	if (parse_getDebugLevel() > 10) {
	    snprintf(errtxt, sizeof(errtxt), "Got '%s'", line);
	    if (errtxt[strlen(errtxt)-2] == '\n'
		&& strlen(errtxt)+1 < sizeof(errtxt)) {
		errtxt[strlen(errtxt)-2] = '\\';
		errtxt[strlen(errtxt)-1] = 'n';
		errtxt[strlen(errtxt)] = '\'';
		errtxt[strlen(errtxt)+1] = '\0';
	    }
	    errlog(errtxt, 10);
	}

	if (strlen(line) == (sizeof(line) - 1)) {
	    errlog("Line too long: '%s'\n", 0);
	    return NULL;
	}

	if (line[0] == '#') continue;

	break;
    } while (1);

    return line;
}

void parse_init(int usesyslog, FILE *input)
{
    initErrLog("Parser", usesyslog);

    parsefile = input;

    parseline = 0;
}

int parse_getDebugLevel(void)
{
    return getErrLogLevel();
}

void parse_setDebugLevel(int level)
{
    setErrLogLevel(level);
}

int parse_string(char *string, parse_t *parser)
{
    char *token;
    unsigned int i;
    int ret;

    token = strtok(string, parser->delim);

    do {
	if (!token) break; /* end of string */

	/* Convert token to lowercase */
	for (i=0; i<strlen(token); i++) {
	    token[i] = tolower(token[i]);
	}

	for (i=0; parser->keylist[i].key; i++) {
	    if (strcmp(token, parser->keylist[i].key)==0) {
		if (parser->keylist[i].action) {
		    ret = parser->keylist[i].action(token);
		    if (ret) return ret;
		}
		break;
	    }
	}

	/* Default action */
	if (!parser->keylist[i].key) {
	    if (parser->keylist[i].action) {
		ret = parser->keylist[i].action(token);
		if (ret) return ret;
	    }
	}

	token = strtok(NULL, parser->delim); /* next token */
    } while(1);

    return 0;
}

int parse_file(parse_t *parser)
{
    char *line;
    int ret;

    do {
	line = nextline();

	if (!line) {
	    return -1;
	}

	if (!strlen(line)) break; /* EOF reached */

	ret = parse_string(line, parser);
	if (ret) return ret;

    } while(1);

    return 0;
}

int parse_error(char *token)
{
    snprintf(errtxt, sizeof(errtxt),
	     "Error in line %d at '%s'", parseline, token);
    errlog(errtxt, 0);

    return -1;
}

char *parse_getString(void)
{
    return strtok(NULL, " \t\n");
}

char *parse_getLine(void)
{
    return strtok(NULL, "\n");
}

long int parse_getNumber(char *token)
{
    char *end;
    long int num;

    num = strtol(token, &end, 0);
    if (*end != '\0') {
	snprintf(errtxt, sizeof(errtxt),
		 "%ld (parsed from '%s') is not a valid number\n", num, token);
	errlog(errtxt, 0);

	return -1;
    }

    return num;
}

char *parse_getFilename(char *prefix, char *extradir)
{
    char *fname;
    static char *absname = NULL;
    struct stat fstat;

    fname = parse_getString();

    if (absname) free(absname);

    if (fname[0]=='/') {
	absname = strdup(fname);
    } else {
	absname = malloc(strlen(prefix) + extradir ? strlen(extradir):0
			 + strlen(fname) + 3);
	if (extradir) {
	    strcpy(absname, prefix);
	    strcat(absname, "/");
	    strcat(absname, extradir);
	    strcat(absname, "/");
	    strcat(absname, fname);

	    if (stat(absname, &fstat)==0 && S_ISREG(fstat.st_mode)) {
		return absname;
	    }
	}
	strcpy(absname, prefix);
	strcat(absname, "/");
	strcat(absname, fname);
    }

    if (stat(absname, &fstat)==0 && S_ISREG(fstat.st_mode)) {
	return absname;
    }

    free(absname);
    absname = NULL;

    return NULL;
}

char *parse_getHostname(void)
{
    char *hname;

    hname = parse_getString();
    /* ToDo: Test if hname is a valid hostname (DNS lookup??) */
    return NULL;
}

int parse_getNumValue(char *token, int *value, char *valname)
{
    int num;

    num = parse_getNumber(token);

    if (num <= 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "'%s' is not a valid '%s'\n", token, valname);
	errlog(errtxt, 0);

	return -1;
    }

    *value = num;

    return 0;
}

int parse_continue(char *line, parse_t *parser)
{
    int ret;

    ret = parse_string(line, parser);

    if (ret) return ret;

    return parse_file(parser);
}
