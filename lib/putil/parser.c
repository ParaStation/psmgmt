/*
 *               ParaStation
 *
 * Copyright (C) 2002-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
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

#include "logging.h"

#include "parser.h"

static logger_t *logger;

static FILE *parsefile;

static char *strtok_work;

static int parseline;

static char *nextline(void)
{
    static char line[512];
    int length=0;

 continuation:
    parseline++;

    if (!fgets(line+length, sizeof(line)-length, parsefile)) {
	parser_comment(PARSER_LOG_FILE, "Got EOF\n");
	line[0] = '\0';
	return line;
    }
    length = strlen(line);
    if (length > 1 && line[length-2] == '\\' && line[length-1] == '\n') {
	length -= 2;
	goto continuation;
    }

    if (parser_getDebugMask() & PARSER_LOG_ECHO) {
	if (line[strlen(line)-1] == '\n') {
	    parser_comment(PARSER_LOG_ECHO, "parsing '%.*s\\n'\n",
			   (int)strlen(line)-1, line);
	} else {
	    parser_comment(PARSER_LOG_ECHO, "parsing '%s'\n", line);
	}
    }

    if (strlen(line) == (sizeof(line) - 1)) {
	parser_comment(-1, "Line too long\n");
	return NULL;
    }

    parser_removeComment(line);

    return line;
}

void parser_init(FILE* logfile, FILE *input)
{
    logger = logger_init("Parser", logfile);

    parsefile = input;

    parseline = 0;
}

int32_t parser_getDebugMask(void)
{
    return logger_getMask(logger);
}

void parser_setDebugMask(int32_t mask)
{
    logger_setMask(logger, mask);
}

void parser_setFile(FILE *input)
{
    parsefile = input;

    parseline = 0;
}

void parser_removeComment(char *line)
{
    char *hash, *start = line;
    int quote=0, dquote=0;

    while ((hash = strchr(start, '#'))) {
	/* This is a candidate. It might be quoted! */
	char *l;
	for (l=start; l<hash; l++) {
	    switch (*l) {
	    case '\'':
		if (!dquote) quote = !quote;
		break;
	    case '"':
		if (!quote) dquote = !dquote;
		break;
	    }
	}
	if (quote || dquote) {
	    /* Find next candidate */
	    start = hash+1;
	    continue;
	} else {
	    break;
	}
    }

    if (hash) {
	if (parser_getDebugMask() & PARSER_LOG_CMNT) {
	    char *txt = hash+1;

	    /* Remove leading whitespace */
	    while (*txt==' ' || *txt=='\t') txt++;

	    if (*txt == '\n') {
		parser_comment(PARSER_LOG_CMNT, "Remove empty comment\n");
	    } else {
		if (txt[strlen(txt)-1] == '\n') txt[strlen(txt)-1] = '\0';
		parser_comment(PARSER_LOG_CMNT, "Remove comment: '%s'\n", txt);
	    }
	}

	hash[0] = '\n';
	hash[1] = '\0';
    }
}

int parser_parseToken(char *token, parser_t *parser)
{
    unsigned int i;
    int mismatch = 0;
    keylist_t *candidate = NULL;
    size_t tokLen;

    if (!token) return 0; /* end of string */
    /* fprintf(stderr, "Token: %s\n", token); */

    tokLen = strlen(token);
    if (!tokLen) return 0; /* empty token */

    for (i=0; parser->keylist[i].key; i++) {
	if (strncasecmp(token, parser->keylist[i].key, tokLen)==0) {
	    if (!candidate) {
		candidate = &parser->keylist[i];
	    } else if (parser->keylist[i].action != candidate->action) {
		mismatch = 1; /* found more than 1 matching key */
	    }
	}
    }

    if (candidate && !mismatch) {
	if (candidate->action) {
	    return candidate->action(candidate->key);
	}
	return 0;
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
    return strtok_r(string, parser->delim, &strtok_work);
}

int parser_parseString(char *token, parser_t *parser)
{
    int ret;

    while (token) {
	ret = parser_parseToken(token, parser);
	if (ret) return ret;

	token = strtok_r(NULL, parser->delim, &strtok_work); /* next token */
    }

    return 0;
}

int parser_parseFile(parser_t *parser)
{
    char *line = nextline(), *token;
    int ret;

    while (line) {
	if (!strlen(line)) return 0; /* EOF reached */

	/* Put line into strtok_r() */
	token = parser_registerString(line, parser);

	/* Do the parsing */
	ret = parser_parseString(token, parser);
	if (ret) return ret;

	line = nextline();
    }

    return -1;
}

int parser_error(char *token)
{
    parser_comment(-1, "Syntax error at '%s'\n", token);
    return -1;
}

void parser_comment(parser_log_key_t key, char *format, ...)
{
    static char *fmt = NULL;
    static int fmtlen = 0;
    va_list ap;
    int len;

    len = snprintf(fmt, fmtlen, "in line %d: %s", parseline, format);
    if (len >= fmtlen) {
	fmtlen = len + 80; /* Some extra space */
	fmt = (char *)realloc(fmt, fmtlen);
	sprintf(fmt, "in line %d: %s\n", parseline, format);
    }

    va_start(ap, format);
    logger_vprint(logger, key, fmt, ap);
    va_end(ap);
}

void parser_commentCont(parser_log_key_t key, char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    logger_vprint(logger, key, format, ap);
    va_end(ap);
}

char *parser_getString(void)
{
    return strtok_r(NULL, " \t\n", &strtok_work);
}

char *parser_getQuotedString(void)
{
    char delim[]=" \t\n";
    while (*strtok_work==' ' || *strtok_work=='\t' || *strtok_work=='\n') {
	strtok_work++;
    }
    if (*strtok_work=='\"' || *strtok_work=='\'') {
	delim[0]=*strtok_work; delim[1]='\0';
	strtok_work++;
    }
    /* Test for empty quoted string */
    if (!isspace(*delim) && *strtok_work==*delim) {
	strtok_work++;
	return "";
    }
    return strtok_r(NULL, delim, &strtok_work);
}

char *parser_getLine(void)
{
    return strtok_r(NULL, "\n", &strtok_work);
}

int parser_getComment(char *token)
{
    char *line = parser_getLine();

    if (line) {
	parser_comment(PARSER_LOG_CMNT, "Got comment '%s'\n", line);
    } else {
	parser_comment(PARSER_LOG_CMNT, "Got empty comment\n");
    }

    return 0;
}

int parser_getNumber(char *token, long *val)
{
    char *end;
    long num;

    if (!token) return -1;

    num = strtol(token, &end, 0);
    if (*end != '\0') {
	return -1;
    }
    *val = num;

    return 0;
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

	    parser_comment(PARSER_LOG_VERB, "%s: file '%s' not found\n",
			   __func__, absname);
	}

	strcpy(absname, prefix);
	strcat(absname, "/");
	strcat(absname, token);
    }

    if (stat(absname, &fstat)==0 && S_ISREG(fstat.st_mode)) {
	return absname;
    }

    parser_comment(PARSER_LOG_VERB,
		   "%s: file '%s' not found\n", __func__, absname);

    free(absname);
    absname = NULL;

    return NULL;
}

unsigned int parser_getHostname(char *token)
{
    char *hname;
    struct in_addr in_addr;

    struct hostent *hostinfo;

    if (!token) {
	parser_comment(-1, "%s: token is NULL\n", __func__);
	return 0;
    }
	
    hname = token;

    hostinfo = gethostbyname(hname);

    if (!hostinfo) {
	parser_comment(-1, "%s: %s\n", hname, hstrerror(h_errno));
	return 0;
    }

    if (hostinfo->h_length != sizeof(in_addr.s_addr)) {
	parser_comment(-1, "%s: Wrong size of address\n", hname);

	h_errno = NO_ADDRESS;

	return 0;
    }

    memcpy(&in_addr.s_addr, hostinfo->h_addr_list[0], sizeof(in_addr.s_addr));

    parser_comment(PARSER_LOG_RES, "Found host '%s' to have address %s\n",
		   hname, inet_ntoa(in_addr));
    
    return in_addr.s_addr;
}

int parser_getNumValue(char *token, int *value, char *valname)
{
    long num;

    if (parser_getNumber(token, &num)) {
	parser_comment(-1, "'%s' is not a valid number for '%s'\n",
		       token, valname);

	return -1;
    }

    parser_comment(PARSER_LOG_RES, "got '%ld' for '%s'\n", num, valname);

    *value = num;

    return 0;
}

int parser_getBool(char *token, int *value, char *valname)
{
    if (!token) {
	parser_comment(-1, "No boolean value given%s%s%s\n",
		       valname ? " for '" : "",
		       valname ? valname : "", valname ? "'" : "");
	return -1;
    }

    if (strcasecmp(token, "true")==0) {
	*value = 1;
    } else if (strcasecmp(token, "false")==0) {
	*value = 0;
    } else if (strcasecmp(token, "yes")==0) {
	*value = 1;
    } else if (strcasecmp(token, "no")==0) {
	*value = 0;
    } else {
	long num;

	if (parser_getNumber(token, &num)) {
	    parser_comment(-1, "'%s' is not a valid boolean value%s%s%s\n",
			   token, valname ? " for '" : "",
			   valname ? valname : "", valname ? "'" : "");

	    return -1;
	}

	*value = !!num;
    }

    parser_comment(PARSER_LOG_RES, "got '%s' for boolean value '%s'\n",
		   *value ? "TRUE" : "FALSE", valname);

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
