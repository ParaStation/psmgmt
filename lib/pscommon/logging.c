/*
 * ParaStation
 *
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "logging.h"

#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <sys/time.h>
#include <time.h>

#define LOG_MAGIC 0x0577215664901532

static inline bool logger_isValid(logger_t *logger)
{
    return logger && logger->magic == LOG_MAGIC;
}

int32_t logger_getMask(logger_t* logger)
{
    return logger_isValid(logger) ? logger->mask : 0;
}

void logger_setMask(logger_t* logger, int32_t mask)
{
    if (logger_isValid(logger)) logger->mask = mask;
}

char* logger_getTag(logger_t* logger)
{
    return logger_isValid(logger) ? logger->tag : NULL;
}

void logger_setTag(logger_t* logger, const char* tag)
{
    if (!logger_isValid(logger)) return;

    free(logger->tag);
    logger->tag = tag ? strdup(tag) : NULL;
}

bool logger_getTimeFlag(logger_t* logger)
{
    return logger_isValid(logger) ? logger->timeFlag : false;
}

void logger_setTimeFlag(logger_t* logger, bool flag)
{
    if (logger_isValid(logger)) logger->timeFlag = flag;
}

bool logger_getWaitNLFlag(logger_t* logger)
{
    return logger_isValid(logger) ? logger->waitNLFlag : false;
}

void logger_setWaitNLFlag(logger_t* logger, bool flag)
{
    if (logger_isValid(logger)) logger->waitNLFlag = flag;
}

logger_t* logger_init(const char* tag, FILE* logfile)
{
    logger_t* logger = (logger_t*)malloc(sizeof(*logger));

    if (logger) {
	logger->magic = LOG_MAGIC;
	logger->logfile = logfile;
	logger_setMask(logger, 0);
	logger->tag = NULL;
	logger_setTag(logger, tag);
	/* pre-allocate trail to prevent psid from bloating */
	logger->trailSize = 256;
	logger->trail = (char*)malloc(logger->trailSize);
	logger->trailUsed = 0;
	logger->timeFlag = false;
	logger->waitNLFlag = true;

	/* pre-allocate fmt, prfx and txt to prevent psid from bloating */
	logger->fmtSize = 256;
	logger->fmt = (char*)malloc(logger->fmtSize);
	logger->prfxSize = 256;
	logger->prfx = (char*)malloc(logger->prfxSize);
	logger->txtSize = 256;
	logger->txt = (char*)malloc(logger->txtSize);

	if (!logger->trail || !logger->fmt || !logger->prfx || !logger->txt) {
	    logger_finalize(logger);
	    logger = NULL;
	}
    }

    return logger;
}

void logger_finalize(logger_t* logger)
{
    if (!logger_isValid(logger)) return;

    if (logger->trailUsed) logger_print(logger, -1, "\n");

    free(logger->tag);
    free(logger->trail);
    free(logger->fmt);
    free(logger->prfx);
    free(logger->txt);
    logger->magic = 0;
    free(logger);
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

    if (!logger_isValid(logger) || !logger_getTimeFlag(logger)) return "";

    gettimeofday(&time, NULL);

    strftime(timeStr, sizeof(timeStr), "[%H:%M:%S", localtime(&time.tv_sec));

    snprintf(timeStr+strlen(timeStr), sizeof(timeStr)-strlen(timeStr),
	     ".%ld]", (long)time.tv_usec);

    return timeStr;
}

/**
 * @brief Panic output and exit
 *
 * Print some panic output to the logger @a l. The structure of the
 * output is described by the format @a f. The format is expected to
 * take two arguments of type pointer to character-string. Afterwards
 * @ref exit() is called in order to terminate the program.
 *
 * This function shall be called in fatal situations, e.g. if no
 * memory is allocateble any more.
 *
 * @param l The logger to use for output
 *
 * @param f Format string describing the output
 *
 * @param c1 First character string to fill the format
 *
 * @param c2 Second character string to fill the format
 *
 * @return No return value
 */
static void do_panic(logger_t* logger, const char *fmt,
		     const char *c1, const char *c2)
{
    if (logger_isValid(logger) && logger->logfile) {
	fprintf(logger->logfile, fmt, c1, c2);
    } else {
	syslog(LOG_ERR, fmt, c1, c2);
    }

    exit(1);
}

/**
 * @brief Actually print message
 *
 * Worker function for @ref logger_print(), @ref logger_vprint(), @ref
 * logger_warn() and @ref logger_exit() actually printing the message.
 *
 * The message defined by @a format and @a ap will be spiffed up with
 * @a logger's tag and some timestamp and put out to the destination
 * also defined within @a logger.
 *
 * The message is only actually put out if @a format contains a
 * newline character. Any trails left after the last newline character
 * will be stored within @a logger and put in front of further
 * messages sent via this special logger.
 *
 * This function does @b no keys/mask handling, i.e. this has to be
 * done within the wrapper functions.
 *
 *
 * @param l The logger facility to use.
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
static void do_print(logger_t* l, const char* fmt, va_list ap)
{
    if (!logger_isValid(l)) return;

    size_t len = 0;
    char *tag = l->tag;

    /* Prepare prefix string */
    char *timeStr = getTimeStr(l);
    int res = snprintf(l->prfx, l->prfxSize, "%s%s%s", tag ? tag : "", timeStr,
		       (tag || l->timeFlag) ? ": " : "");
    if (res >= 0) len = res;
    if (len >= l->prfxSize) {
	l->prfxSize = len + 80; /* Some extra space */
	l->prfx = (char*)realloc(l->prfx, l->prfxSize);
	if (!l->prfx) {
	    do_panic(l, "%s: no mem for prefix: '%s'\n", __func__, fmt);
	}
	sprintf(l->prfx, "%s%s%s", tag ? tag : "", timeStr,
		(tag || l->timeFlag) ? ": " : "");
    }

    /* Create actual output */
    va_list aq;
    va_copy(aq, ap);
    res = vsnprintf(l->txt, l->txtSize, fmt, ap);
    if (res > 0) len = (size_t)res;
    if (len >= l->txtSize) {
	l->txtSize = len + 80; /* Some extra space */
	l->txt = (char*)realloc(l->txt, l->txtSize);
	if (!l->txt) do_panic(l, "%s: no mem for text: '%s'\n", __func__, fmt);
	vsprintf(l->txt, fmt, aq);
    }
    va_end(aq);

    char *c = l->txt;

    while (c && *c) {
	char *r = strchr(c, '\n');

	if (r && l->waitNLFlag) {
	    *r = '\0';
	    r++;
	}

	if (!l->waitNLFlag) {
	    char *s = l->trailUsed ? l->trail : l->prfx;
	    if (l->logfile) {
		fprintf(l->logfile, "%s%s", s, c);
	    } else {
		syslog(LOG_ERR, "%s%s", s, c);
	    }
	    l->trailUsed = 0;
	    break;
	} else if (r) {
	    /* got newline, lets do the output */
	    char *s = l->trailUsed ? l->trail : l->prfx;
	    if (l->logfile) {
		fprintf(l->logfile, "%s%s\n", s, c);
	    } else {
		syslog(LOG_ERR, "%s%s", s, c);
	    }
	    l->trailUsed = 0;
	} else {
	    /* no newline, append to trail */
	    len = (!l->trailUsed && l->prfx) ? strlen(l->prfx) : 0;
	    len += strlen(c);
	    if (l->trailUsed + len >= l->trailSize) {
		l->trailSize = l->trailUsed + len + 80; /* Some extra space */
		l->trail = (char*)realloc(l->trail, l->trailSize);
		if (!l->trail) {
		    do_panic(l, "%s: no mem for trail%s\n", __func__, "");
		}
	    }
	    if (!l->trailUsed && l->prfx) {
		/* some prefix to be put into trail */
		res = sprintf(l->trail, "%s", l->prfx);
		if (res >= 0) {
		    l->trailUsed = (size_t)res;
		}
	    }
	    res = sprintf(l->trail + l->trailUsed, "%s", c);
	    if (res >= 0) {
		l->trailUsed += (size_t)res;
	    }
	}

	c = r;
    }

    if (l->logfile) fflush(l->logfile);
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

void logger_funcprint(logger_t* logger, const char *func, int32_t key,
		      const char *format, ...)
{
    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    if (func) {
	int res = snprintf(logger->fmt, logger->fmtSize,
			   "%s: %s", func, format);
	size_t len = (res >= 0) ? res : 0;
	if (len >= logger->fmtSize) {
	    logger->fmtSize = len + 80; /* Some extra space */
	    logger->fmt = (char*)realloc(logger->fmt, logger->fmtSize);
	    if (!logger->fmt) {
		do_panic(logger, "%s: no mem for '%s'\n", __func__, format);
	    }
	    sprintf(logger->fmt, "%s: %s", func, format);
	}
    }

    va_list ap;
    va_start(ap, format);
    do_print(logger, func ? logger->fmt : format, ap);
    va_end(ap);
}

void logger_warn(logger_t* logger, int32_t key, int eno,
		 const char* format, ...)
{
    char* errstr = strerror(eno);
    va_list ap;
    size_t len = 0;
    int res;

    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    res = snprintf(logger->fmt, logger->fmtSize,
		   "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    if (res >= 0) {
	len = (size_t)res;
    }
    if (len >= logger->fmtSize) {
	logger->fmtSize = len + 80; /* Some extra space */
	logger->fmt = (char*)realloc(logger->fmt, logger->fmtSize);
	if (!logger->fmt) {
	    do_panic(logger, "%s: no mem for '%s'\n", __func__, format);
	}
	sprintf(logger->fmt, "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    }

    va_start(ap, format);
    do_print(logger, logger->fmt, ap);
    va_end(ap);
}

void logger_write(logger_t* logger, int32_t key, const char *buf, size_t count)
{
    if (!logger || ((key != -1) && !(logger->mask & key))) return;

    if (logger->logfile) {
	size_t n;
	ssize_t i;
	for (n = 0, i = 1; n < count && i > 0; ) {
	    i = write(fileno(logger->logfile), &buf[n], count-n);
	    if (i <= 0) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
		    break;
		default:
		    do_panic(logger, "%s: %s", __func__, strerror(errno));
		}
	    } else {
		n += (size_t)i;
	    }
	}
    }
}

void logger_exit(logger_t* logger, int eno, const char* format, ...)
{
    char* errstr = strerror(eno);
    va_list ap;
    size_t len = 0;
    int res;

    if (!logger) exit(-1);

    res = snprintf(logger->fmt, logger->fmtSize,
		   "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    if (res >= 0) {
	len = (size_t)res;
    }
    if (len >= logger->fmtSize) {
	logger->fmtSize = len + 80; /* Some extra space */
	logger->fmt = (char*)realloc(logger->fmt, logger->fmtSize);
	if (!logger->fmt) {
	    do_panic(logger, "%s: no mem for '%s'\n", __func__, format);
	}
	sprintf(logger->fmt, "%s: %s\n", format, errstr ? errstr : "UNKNOWN");
    }

    va_start(ap, format);
    do_print(logger, logger->fmt, ap);
    va_end(ap);

    logger_finalize(logger);

    exit(-1);
}
