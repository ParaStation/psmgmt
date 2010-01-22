/*
 *               ParaStation
 *
 * Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
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
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <sys/time.h>

#include "logging.h"

int32_t logger_getMask(logger_t* logger)
{
    return logger->mask;
}

void logger_setMask(logger_t* logger, int32_t mask)
{
    logger->mask = mask;
}

char* logger_getTag(logger_t* logger)
{
    return logger->tag;
}

void logger_setTag(logger_t* logger, char* tag)
{
    if (logger->tag) free(logger->tag);

    if (tag) {
	logger->tag = strdup(tag);
    } else {
	logger->tag = NULL;
    }
}

char logger_getTimeFlag(logger_t* logger)
{
    return logger->timeFlag;
}

void logger_setTimeFlag(logger_t* logger, char flag)
{
    logger->timeFlag = flag;
}

logger_t* logger_init(char* tag, FILE* logfile)
{
    logger_t* logger = malloc(sizeof(*logger));

    logger->logfile = logfile;
    logger_setMask(logger, 0);
    logger->tag = NULL;
    logger_setTag(logger, tag);
    logger->trail = NULL;
    logger->timeFlag = 0;

    return logger;
}

/**
 * @brief Create time-stamp
 *
 * Create a time-stamp for the logger @a logger. If the logger's
 * timeFlag is set, a real time-stamp is created. Otherwise the
 * time-stamp will be empty.
 *
 * The character-array returned is a static array within this
 * function. Thus calling the function multiple time might lead to
 * unexpected results.
 *
 * @param logger The logger the time-stamp is created for.
 *
 * @return Return a pointer to a static character array containing the
 * time-stamp created.
 */
static inline char *getTimeStr(logger_t *logger)
{
    static char timeStr[40];
    struct timeval time;

    if (!logger || !logger->timeFlag) return "";

    gettimeofday(&time, NULL);

    strftime(timeStr, sizeof(timeStr), "[%H:%M:%S", localtime(&time.tv_sec));

    snprintf(timeStr+strlen(timeStr), sizeof(timeStr)-strlen(timeStr),
	    ".%ld]", (long)time.tv_usec);

    return timeStr;
}

/**
 * @brief Actually print message
 *
 * Worker function for @ref logger_print(), @ref logger_vprint(), @ref
 * logger_warn() and @ref logger_exit() actually printing the message.
 *
 * The message defined by @a format and @a ap will be spiffed up with
 * @a logger's tag and put out to the destination also defined within
 * @a logger.
 *
 * The message is only actually put out if @a format contains a
 * trailing newline character. Otherwise it will be stored within @a
 * logger and put in front of further messages sent via this special
 * logger.
 *
 * This function does @b no keys/mask handling, i.e. this has to be
 * done within the wrapper functions.
 *
 *
 * @param logger The logger facility to use.
 *
 * @param format The format to be used in order to produce output. The
 * syntax of this parameter is according to the one defined for the
 * @ref printf() family of functions from the C standard. This string
 * will also define the further parameters to be expected from within
 * the va list @a ap.
 *
 * @param ap The va_list of the remainig parameters defined from @a
 * format.
 *
 * @return No return value.
 *
 * @see logger_print(), logger_vprint(), logger_warn(), logger_exit()
 */
static void do_print(logger_t* logger, const char* format, va_list ap)
{
    static char *prefix = NULL, *text = NULL, *line = NULL;
    static int prfxlen = 0, txtlen = 0, linelen = 0;
    va_list aq;
    char *tag;
    int len;

    if (!logger) return;
    tag = logger->tag;

    if (!logger->trail) {
	char *timeStr = getTimeStr(logger);
	len = snprintf(prefix, prfxlen, "%s%s%s", tag ? tag : "", timeStr,
		       (tag || logger->timeFlag) ? ": " : "");
	if (len >= prfxlen) {
	    prfxlen = len + 80; /* Some extra space */
	    prefix = (char*)realloc(prefix, prfxlen);
	    sprintf(prefix, "%s%s%s", tag ? tag : "", timeStr,
		    (tag || logger->timeFlag) ? ": " : "");
	}
    }

    va_copy(aq, ap);
    len = vsnprintf(text, txtlen, format, ap);
    if (len >= txtlen) {
	txtlen = len + 80; /* Some extra space */
	text = (char*)realloc(text, txtlen);
	vsprintf(text, format, aq);
    }
    va_end(aq);

    if (logger->trail) {
	len = snprintf(line, linelen, "%s%s", logger->trail, text);
	if (len >= linelen) {
	    linelen = len + 80; /* Some extra space */
	    line = (char*)realloc(line, linelen);
	    sprintf(line, "%s%s", logger->trail, text);
	}
    } else {
	len = snprintf(line, linelen, "%s%s", prefix, text);
	if (len >= linelen) {
	    linelen = len + 80; /* Some extra space */
	    line = (char*)realloc(line, linelen);
	    sprintf(line, "%s%s", prefix, text);
	}
    }

    if (logger->logfile) {
	fprintf(logger->logfile, "%s", logger->trail ? text : line);
	fflush(logger->logfile);
    }

    if (text[strlen(text)-1] == '\n') {
	if (!logger->logfile) syslog(LOG_ERR, "%s", line);
	if (logger->trail) {
	    free(logger->trail);
	    logger->trail = NULL;
	}
    } else {
	if (logger->trail) free(logger->trail);
	logger->trail = strdup(line);
    }
}

void logger_print(logger_t* logger, int32_t key, const char* format, ...)
{
    va_list ap;

    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    va_start(ap, format);
    do_print(logger, format, ap);
    va_end(ap);
}

void logger_vprint(logger_t* logger, int32_t key,
		   const char* format, va_list ap)
{
    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    do_print(logger, format, ap);
}

void logger_warn(logger_t* logger, int32_t key, int eno,
		 const char* format, ...)
{
    static char* fmt = NULL;
    static int fmtlen = 0;
    char* errstr = strerror(eno);
    va_list ap;
    int len;

    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    len = snprintf(fmt, fmtlen,
		   "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    if (len >= fmtlen) {
	fmtlen = len + 80; /* Some extra space */
	fmt = (char*)realloc(fmt, fmtlen);
	sprintf(fmt, "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    }

    va_start(ap, format);
    do_print(logger, fmt, ap);
    va_end(ap);
}

void logger_exit(logger_t* logger, int eno, const char* format, ...)
{
    static char* fmt = NULL;
    static int fmtlen = 0;
    char* errstr = strerror(eno);
    va_list ap;
    int len;

    if (!logger) return;

    len = snprintf(fmt, fmtlen,
		   "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    if (len >= fmtlen) {
	fmtlen = len + 80; /* Some extra space */
	fmt = (char*)realloc(fmt, fmtlen);
	sprintf(fmt, "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    }

    va_start(ap, format);
    do_print(logger, fmt, ap);
    va_end(ap);

    exit(-1);
}
