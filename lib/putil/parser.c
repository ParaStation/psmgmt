/*
 *               ParaStation3
 * parser.c
 *
 * General parser utility for ParaStation daemon and admin
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: parser.c,v 1.3 2002/07/03 20:19:49 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: parser.c,v 1.3 2002/07/03 20:19:49 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "errlog.h"

#include "parser.h"

static FILE *parsefile;

static int parseline;

static char errtxt[256];

static char *nextline(void)
{
    static char line[256];

    parseline++;

    if (!fgets(line, sizeof(line), parsefile)) {
	parser_comment("Got EOF", 5);
	line[0] = '\0';
	return line;
    }

    if (parser_getDebugLevel() >= 12) {
	int errlen;

	snprintf(errtxt, sizeof(errtxt), "Parsing '%s'", line);
	if (errtxt[(errlen=strlen(errtxt)-2)] == '\n'
	    && strlen(errtxt)+1 < sizeof(errtxt)) {
	    errtxt[errlen++] = '\\';
	    errtxt[errlen++] = 'n';
	    errtxt[errlen++] = '\'';
	    errtxt[errlen++] = '\0';
	}
	parser_comment(errtxt, 14);
    }

    if (strlen(line) == (sizeof(line) - 1)) {
	parser_comment("Line too long: '%s'\n", 0);
	return NULL;
    }

    return line;
}

void parser_init(int usesyslog, FILE *input)
{
    initErrLog("Parser", usesyslog);

    parsefile = input;

    parseline = 0;
}

int parser_getDebugLevel(void)
{
    return getErrLogLevel();
}

void parser_setDebugLevel(int level)
{
    setErrLogLevel(level);
}

int parser_parseString(char *token, parser_t *parser)
{
    int ret;

    do {
	if (!token) break; /* end of string */

	ret = parser_parseToken(token, parser);
	if (ret) return ret;

	token = strtok(NULL, parser->delim); /* next token */
    } while(1);

    return 0;
}

int parser_parseToken(char *token, parser_t *parser)
{
    unsigned int i;
    int ret;

    if (!token) return 0; /* end of string */
    // fprintf(stderr, "Token: %s\n", token);

    for (i=0; parser->keylist[i].key; i++) {
	if (strcasecmp(token, parser->keylist[i].key)==0) {
	    if (parser->keylist[i].action) {
		return parser->keylist[i].action(token);
	    }
	    break;
	}
    }

    /* Default action */
    if (!parser->keylist[i].key) {
	if (parser->keylist[i].action) {
	    return parser->keylist[i].action(token);
	}
    }

    return 0;
}

char *parser_registerString(char *string, parser_t *parser)
{
    return strtok(string, parser->delim);
}

int parser_parseFile(parser_t *parser)
{
    char *line, *token;
    int ret;

    do {
	line = nextline();

	if (!line) {
	    return -1;
	}

	if (!strlen(line)) break; /* EOF reached */

	/* Put line into strtok() */
	token = parser_registerString(line, parser);

	/* Do the parsing */
	ret = parser_parseString(token, parser);
	if (ret) return ret;

    } while(1);

    return 0;
}

int parser_error(char *token)
{
    snprintf(errtxt, sizeof(errtxt),
	     "in line %d: Syntax error at '%s'", parseline, token);
    errlog(errtxt, 0);

    return -1;
}

void parser_comment(char *comment, int level)
{
    char errtxt[320];
    snprintf(errtxt, sizeof(errtxt), "in line %d: %s", parseline, comment);

    errlog(errtxt, level);
}

char *parser_getString(void)
{
    return strtok(NULL, " \t\n");
}

char *parser_getLine(void)
{
    return strtok(NULL, "\n");
}

int parser_getComment(char *token)
{
    char *line = parser_getLine();

    if (line) {
	snprintf(errtxt, sizeof(errtxt), "Got comment '%s'", line);
    } else {
	snprintf(errtxt, sizeof(errtxt), "Got empty comment");
    }

    parser_comment(errtxt, 12);


    return 0;
}

long int parser_getNumber(char *token)
{
    char *end;
    long int num;

    num = strtol(token, &end, 0);
    if (*end != '\0') {
	return -1;
    }

    return num;
}

char *parser_getFilename(char *token, char *prefix, char *extradir)
{
    char *absname = NULL;
    struct stat fstat;

    if (token[0]=='/') {
	absname = strdup(token);
    } else {
	absname = malloc(strlen(prefix) + (extradir ? strlen(extradir) : 0)
			 + strlen(token) + 3);

	if (extradir) {
	    strcpy(absname, prefix);
	    strcat(absname, "/");
	    strcat(absname, extradir);
	    strcat(absname, "/");
	    strcat(absname, token);

	    if (stat(absname, &fstat)==0 && S_ISREG(fstat.st_mode)) {
		return absname;
	    }

	    snprintf(errtxt, sizeof(errtxt),
		     "parser_getFilename(): file '%s' not found", absname);
	    parser_comment(errtxt, 10);
	}

	strcpy(absname, prefix);
	strcat(absname, "/");
	strcat(absname, token);
    }

    if (stat(absname, &fstat)==0 && S_ISREG(fstat.st_mode)) {
	return absname;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "parser_getFilename(): file '%s' not found", absname);
    parser_comment(errtxt, 10);

    free(absname);
    absname = NULL;

    return NULL;
}

unsigned int parser_getHostname(char *token)
{
    char *hname;
    struct in_addr in_addr;

    struct hostent *hostinfo;

    hname = token;

    hostinfo = gethostbyname(hname);

    if (!hostinfo) {
	snprintf(errtxt, sizeof(errtxt), "%s: %s", hname, hstrerror(h_errno));
	parser_comment(errtxt, 0);

	return 0;
    }

    if (hostinfo->h_length != sizeof(in_addr.s_addr)) {
	snprintf(errtxt, sizeof(errtxt), "%s: Wrong size of address", hname);
	parser_comment(errtxt, 0);

	h_errno = NO_ADDRESS;

	return 0;
    }

    memcpy(&in_addr.s_addr, hostinfo->h_addr_list[0], sizeof(in_addr.s_addr));

    snprintf(errtxt, sizeof(errtxt), "Found host '%s' to have address %s",
	     hname, inet_ntoa(in_addr));
    parser_comment(errtxt, 10);
    
    return in_addr.s_addr;
}

int parser_getNumValue(char *token, int *value, char *valname)
{
    int num;

    num = parser_getNumber(token);

    if (num < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "'%s' is not a valid number for '%s'", token, valname);
	parser_comment(errtxt, 0);

	return -1;
    }

    snprintf(errtxt, sizeof(errtxt), "Got '%d' for '%s'", num, valname);
    parser_comment(errtxt, 8);

    *value = num;

    return 0;
}

int parser_getBool(char *token, int *value, char *valname)
{
    if (strcasecmp(token, "true")==0) {
	*value = 1;
    } else if (strcasecmp(token, "false")==0) {
	*value = 0;
    } else if (strcasecmp(token, "yes")==0) {
	*value = 1;
    } else if (strcasecmp(token, "no")==0) {
	*value = 0;
    } else {
	int num;

	num = parser_getNumber(token);

	if (num < 0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "'%s' is not a valid boolean value for '%s'",
		     token, valname);
	    parser_comment(errtxt, 0);

	    return -1;
	}

	*value = num;
    }

    snprintf(errtxt, sizeof(errtxt), "Got '%s' for boolean value '%s'",
	     *value ? "TRUE" : "FALSE", valname);
    parser_comment(errtxt, 8);

    return 0;
}

int parser_parseOn(char *token, parser_t *parser)
{
    int ret;

    ret = parser_parseString(token, parser);

    if (!ret) {
	ret = parser_parseFile(parser);
    }

    return ret;
}
