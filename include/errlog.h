/*
 *               ParaStation3
 * errlog.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: errlog.h,v 1.4 2002/06/14 15:27:00 eicker Exp $
 *
 */
/**
 * \file
 * ParaStation ErrLog facility used within MCast and RDP.
 *
 * $Id: errlog.h,v 1.4 2002/06/14 15:27:00 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __ERRLOG_H
#define __ERRLOG_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include <stdio.h>
#include <syslog.h>

/** Flag whether to use syslog. Set via initErrLog(). */
static int syslogErrLog = 1;

/**
 * Actual log-level for logging. Set/get thru
 * setErrLogLevel()/getErrLogLevel().
 */
static int levelErrLog = 0;

/**
 * Actual tag prepended each log-message. Set/get thru
 * setErrLogTag()/getErrLogTag() or initErrLog().
 */
static char *ErrLogTag = NULL;

/**
 * @brief Query the log-level.
 *
 * Get the actual log-level of the ErrLog module.
 *
 * @return The actual log-level is returned.
 *
 * @see setErrLogLevel()
 */
static int getErrLogLevel(void)
{
    return levelErrLog;
}

/**
 * @brief Set the log-level.
 *
 * Set the log-level of the ErrLog module. The possible values depend on
 * the usage in the actual modules.
 *
 * @param level The log-level to be set
 *
 * @return No return value.
 *
 * @see getErrLogLevel()
 */
static void setErrLogLevel(int level)
{
    if ((level>=0) || (level<16)) {
        levelErrLog = level;
    }
}

/**
 * @brief Query the log-tag.
 *
 * Get the actual log-tag of the ErrLog module.
 *
 * @return The actual log-tag is returned.
 *
 * @see setErrLogTag()
 */
static char * getErrLogTag(void)
{
    return ErrLogTag;
}

/**
 * @brief Set the log-tag.
 *
 * Set the log-tag of the ErrLog module. The log-tag is prepended to each
 * message put out via errlog() or errexit().
 *
 * @param tag The log-tag to be set.
 *
 * @return No return value.
 *
 * @see getErrLogTag()
 */
static void setErrLogTag(char *tag)
{
    if (ErrLogTag) free(ErrLogTag);

    if (tag) {
	ErrLogTag = strdup(tag);
    } else {
	ErrLogTag = NULL;
    }

    return;
}

/**
 * @brief Initialize ErrLog facility
 *
 * Initialize the ErrLog facility using the tag @a tag to log via syslog(),
 * if @a syslog is true, and via stderr otherwise.
 *
 * @param tag The tag to be used in all output via errlog()/errexit().
 * @param syslog Flag to mark syslog() to be used for any output.
 *
 * @return No return value.
 */
static void initErrLog(char *tag, int syslog)
{
    setErrLogTag(tag);
    syslogErrLog = syslog;
}

/**
 * @brief Print log-messages.
 *
 * Prints message @a s with some beautification, if @a level is
 * <= @ref levelErrLog. The output goes to syslog or stderr depending on
 * @ref syslogErrLog.
 *
 * @param s The actual message to log.
 * @param level The log-level of the message. Comparing to @ref levelErrLog
 * decides whether @a s is actually put out or not.
 *
 * @return No return value.
 */
static void errlog(char *s, int level)
{
    static char errtxt[320];

    if (level > levelErrLog) return;

    snprintf(errtxt, sizeof(errtxt), "%s: %s\n", ErrLogTag ? ErrLogTag:"", s);
    if (syslogErrLog) {
        syslog(LOG_ERR, errtxt);
    } else {
        fprintf(stderr, errtxt);
    }

    return;
}

/**
 * @brief Print log-messages and exit.
 *
 * Prints message @a s and string corresponding to errno with some
 * beautification. The output goes to syslog or stderr depending on
 * @ref syslogErrLog.
 *
 * @param s The actual message to log.
 * @param errorno The errno which occured. errexit() logs the corresponding
 * string given by strerror().
 *
 * @return No return value.
 *
 * @see errno(3), strerror(3)
 */
static void errexit(char *s, int errorno)
{
    static char errtxt[320];

    if (syslogErrLog) {
	char* errstr = strerror(errorno);
	snprintf(errtxt, sizeof(errtxt), "%s ERROR: %s: %s\n",
		 ErrLogTag ? ErrLogTag:"", s, errstr ? errstr : "UNKNOWN");
        syslog(LOG_ERR, errtxt);
    } else {
        perror(s);
    }
    exit(-1);
    return;
}

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __ERRLOG_H */
