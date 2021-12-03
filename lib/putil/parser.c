/*
 * ParaStation
 *
 * Copyright (C) 2002-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "parser.h"

#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "logging.h"
#include "pscommon.h"

/** logger used for error- and progress-messages */
static logger_t *logger = NULL;

/** The file to parse, if any */
static FILE *parsefile;

/** strtok_r()'s workspace */
static char *strtok_work;

/** Number of the current line to parse */
static int parseline;

/**
 * @brief Get next line
 *
 * Get the next line from the file to parse. This function already
 * handles the continuation of lines via '\', i.e. it might fetch more
 * than one line of the actual file as long as they belong together in
 * a logical way.
 *
 * The line is stored into a static buffer and a pointer to this
 * buffer is returned. Therefore the length of (logical) lines is
 * currently limited to 512 characters.
 *
 * @return Pointer to the line fetched, or NULL, if an error occurred.
 */
static char *nextline(void)
{
    static char line[512];
    char tag[32];
    int length=0;

 continuation:
    parseline++;

    snprintf(tag, sizeof(tag), "Parser in line %d", parseline);
    logger_setTag(logger, tag);

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
    if (!logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }

    parsefile = input;

    parseline = 0;
}

void parser_finalize(void)
{
    logger_finalize(logger);
    logger = NULL;

    parsefile = NULL;
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


static keylist_t * matchToken(char *token, keylist_t *keylist)
{
    unsigned int i;
    int mismatch = 0;
    keylist_t *candidate = NULL;
    size_t tokLen;

    if (!token) return NULL; /* no token */
    if (!keylist) return NULL; /* no keys to match */

    tokLen = strlen(token);
    if (!tokLen) return NULL; /* empty token */

    for (i=0; keylist[i].key; i++) {
	if (strncasecmp(token, keylist[i].key, tokLen)==0) {
	    if (strlen(keylist[i].key) == tokLen) {
		/* exact match */
		candidate = &keylist[i];
		mismatch = 0;
		break;
	    }
	    if (!candidate) {
		candidate = &keylist[i];
	    } else if (keylist[i].action != candidate->action) {
		mismatch = 1; /* found more than 1 matching key */
	    }
	}
    }

    if (candidate && !mismatch) return candidate;

    /* Default action */
    if (!keylist[i].key) return &keylist[i];

    return NULL;
}

int parser_parseToken(char *token, parser_t *parser)
{
    keylist_t *matchedKey = matchToken(token, parser->keylist);

    if (!matchedKey) return 0; /* something went horribly wrong */

    if (matchedKey->key) {
	/* not the default action */
	if (matchedKey->action) return matchedKey->action(matchedKey->key);
    } else {
	if (matchedKey->action) {
	    return matchedKey->action(token);
	}
    }

    return 0;
}

keylist_t * parser_nextKeylist(char *token, keylist_t *keylist, char **matched)
{
    keylist_t *matchedKey = matchToken(token, keylist);

    *matched = NULL;

    if (matchedKey) {
	*matched = matchedKey->key;
	return matchedKey->next;
    }

    return NULL;
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
    va_list ap;

    va_start(ap, format);
    logger_vprint(logger, key, format, ap);
    va_end(ap);
}

void parser_exit(int eno, char *format, ...)
{
    char *fmt = NULL, *errstr = strerror(eno);
    va_list ap;

    if (eno) {
	size_t len = snprintf(NULL, 0, "%s: %s\n", format,
			      errstr ? errstr : "UNKNOWN");
	fmt = malloc(len+1);
    }
    if (fmt) {
	sprintf(fmt, "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    } else {
	fmt = format;
    }

    va_start(ap, format);
    logger_vprint(logger, -1, fmt, ap);
    va_end(ap);

    if (fmt != format) free(fmt);

    exit(-1);
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
    return PSC_numFromString(token, val);
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

    return NULL;
}

in_addr_t parser_getHostname(char *token)
{
    char *hname;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int rc;
    struct in_addr *in_addr = NULL;
    in_addr_t addr;

    if (!token) {
	parser_comment(-1, "%s: token is NULL\n", __func__);
	return 0;
    }

    hname = token;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(hname, NULL, &hints, &result);
    if (rc != 0) {
	parser_comment(-1, "Unknown host '%s': %s\n", hname, gai_strerror(rc));
	return -1;
    }

    /*
     * getaddrinfo() returns a list of address structures.
     * Try each address until we successfully resolve to ParaStation ID.
     */

    for (rp = result; rp && !in_addr; rp = rp->ai_next) {
	char addrStr[INET_ADDRSTRLEN] = {'\0'};
	switch (rp->ai_family) {
	case AF_INET:
	    in_addr = &((struct sockaddr_in *)rp->ai_addr)->sin_addr;
	    inet_ntop(rp->ai_family, in_addr,addrStr, sizeof(addrStr));
	    parser_comment(PARSER_LOG_RES,
			   "Found host '%s' to have address %s\n",
			   hname, addrStr);
	    break;
	case AF_INET6:
	    /* ignore -- don't handle IPv6 yet */
	    break;
	}
    }

    if (!in_addr) {
	parser_comment(-1, "%s: No entry for '%s'\n", __func__, hname);
	return 0;
    }

    addr = in_addr->s_addr;

    freeaddrinfo(result);           /* No longer needed */

    return addr;
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

/*------------------------- Hashing ----------------------------*/

/*
 * MurmurHash3 was written by Austin Appleby. This code is stolen from
 * https://github.com/aappleby/smhasher under MIT license with minor
 * adaptations
 */
static inline uint32_t rotl32(uint32_t x, int8_t r)
{
    return (x << r) | (x >> (32 - r));
}

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

static inline uint32_t fmix32( uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

static uint32_t MurmurHash3_x86_32(const void *key, size_t len, uint32_t seed)
{
    const uint8_t * data = (const uint8_t*)key;
    const int nblocks = len / 4;

    uint32_t h1 = seed;

    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    //----------
    // body

    const uint32_t *blocks = (const uint32_t *)(data + nblocks*4);

    for (int i = -nblocks; i; i++) {
	uint32_t k1 = blocks[i];

	k1 *= c1;
	k1 = rotl32(k1,15);
	k1 *= c2;

	h1 ^= k1;
	h1 = rotl32(h1,13);
	h1 = h1*5+0xe6546b64;
    }

    //----------
    // tail

    const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

    uint32_t k1 = 0;

    switch(len & 3) {
    case 3:
	k1 ^= tail[2] << 16;
	__attribute__((fallthrough));
    case 2:
	k1 ^= tail[1] << 8;
	__attribute__((fallthrough));
    case 1:
	k1 ^= tail[0];
	k1 *= c1; k1 = rotl32(k1,15); k1 *= c2; h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= len;

    return fmix32(h1);
}

void parser_updateHash(uint32_t *hashVal, char *line)
{
    if (!hashVal || !line) return;

    *hashVal += MurmurHash3_x86_32(line, strlen(line), 0);
}
