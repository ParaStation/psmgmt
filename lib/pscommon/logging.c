/*
 *               ParaStation
 *
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

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

logger_t* logger_init(char* tag, FILE* logfile)
{
    logger_t* logger = malloc(sizeof(*logger));

    logger->logfile = logfile;
    logger_setMask(logger, 0);
    logger->tag = NULL;
    logger_setTag(logger, tag);
    logger->trail = NULL;

    return logger;
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
    static char *errfmt = NULL, *errtxt = NULL, *errline = NULL;
    static int fmtlen = 0, txtlen = 0, linelen = 0;
    va_list aq;
    char* tag;
    int len;

    if (!logger) return;
    tag = logger->tag;

    len = snprintf(errfmt, fmtlen, "%s%s%s",
		   (tag && !logger->trail) ? tag : "",
		   (tag && !logger->trail) ? ": " : "", format);
    if (len >= fmtlen) {
	fmtlen = len + 80; /* Some extra space */
	errfmt = (char*)realloc(errfmt, fmtlen);
	sprintf(errfmt, "%s%s%s", (tag && !logger->trail) ? tag : "",
		(tag && !logger->trail) ? ": " : "", format);
    }

    va_copy(aq, ap);
    len = vsnprintf(errtxt, txtlen, errfmt, ap);
    if (len >= txtlen) {
	txtlen = len + 80; /* Some extra space */
	errtxt = (char*)realloc(errtxt, txtlen);
	vsprintf(errtxt, errfmt, aq);
    }
    va_end(aq);

    if (logger->trail) {
	len = snprintf(errline, linelen, "%s%s", logger->trail, errtxt);
	if (len >= linelen) {
	    linelen = len + 80; /* Some extra space */
	    errline = (char*)realloc(errline, linelen);
	    sprintf(errline, "%s%s", logger->trail, errtxt);
	}
    }

    if (errtxt[strlen(errtxt)-1] == '\n') {
	if (logger->logfile) {
	    fprintf(logger->logfile, "%s", logger->trail ? errline : errtxt);
	} else {
	    syslog(LOG_ERR, logger->trail ? errline : errtxt);
	}
	if (logger->trail) {
	    free(logger->trail);
	    logger->trail = NULL;
	}
    } else {
	if (logger->trail) free(logger->trail);
	logger->trail = strdup(logger->trail ? errline : errtxt);
    }
}

void logger_print(logger_t* logger, long key, const char* format, ...)
{
    va_list ap;

    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    va_start(ap, format);
    do_print(logger, format, ap);
    va_end(ap);
}

void logger_vprint(logger_t* logger, long key, const char* format, va_list ap)
{
    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    do_print(logger, format, ap);
}

void logger_warn(logger_t* logger, long key, int eno, const char* format, ...)
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
