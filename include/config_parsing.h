/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Parser for the config file of the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PARSE_H
#define __PARSE_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * Structure to store the daemons configuration read from the
 * configuration file. Further parts of the configuration file are
 * directly stored into the PSnodes-database and further databases.
 */
typedef struct {
    char* coreDir;       /**< psid's current working directory. In the
			    improbable case of a core-dump the core-file
			    will be deposited here. */
    int selectTime;      /**< Time spent within psid's central select().
			    Result of 'SelectTime'. Default is 2. */
    int deadInterval;    /**< Declare node dead after this # missing pings.
			    Result of 'DeadInterval'. Default is 10. */
    int RDPPort;         /**< The UDP port to use for RDP messages.
			    Result of 'RDPPort'. Default is 886. */
    int useMCast;        /**< Flag if MCast should be used for status controll.
			    Result of 'UseMCast'. Default is 1 (use MCast). */ 
    int MCastGroup;      /**< The MCast group to use.
			    Result of 'MCastGroup'. Default is 237. */
    int MCastPort;       /**< The UDP port to use for MCast messages.
			    Result of 'MCastPort'. Default is 1889. */
    int logMask;         /**< The logging mask (verbosity) to use.
			    Result of 'LogMask'. Default is 0. */
    int logDest;         /**< The destination of all information to put out.
			    Result of 'LogDestination'. Default is
			    LOG_DAEMON. */
    FILE* logfile;       /**< The file to use for logging. If NULL,
			    syslog(3) is used for output. This
			    destinations is defined on the
			    commandline. */
    int freeOnSuspend;   /**< Flag if a job's resources are freed on
			    suspend. */
    int handleOldBins;   /**< Flag if resources occupied by old binaries
			    (prior 4.1.0 should be recognized. */
    int nodesSort;       /**< The default sorting strategy; used if the user
			    does not declare a different one explicitely. */
    int acctPollInterval;/**< Interval of forwarder to poll for accounting
			    info. No polling for 0 (the default). */
} config_t;

/**
 * @brief Parse the configuration file.
 *
 * Parse the configuration file @a configfile and return a pointer to
 * the parsed configration information. During parsing, use the
 * syslog(3) facility if @a usesyslog is different from 0.
 *
 * @a loglevel steers the verbosity while parsing the
 * configuration. If set to 0, only fatal errors will reported. Higher
 * values will lead to more verbose output even if no error occurred.
 *
 * As a side effect, the PSnodes-database is created and filled with
 * information on all the participating nodes of the
 * cluster. I.e. resolving of IP address to node ID and vice versa is
 * possible after calling this function successfully.
 *
 * @param logfile File to use for logging. If NULL, all messages are
 * printed via syslog(3).
 *
 * @param loglevel The verbosity of generated output.
 *
 * @param configfile The name of the config file to parse.
 *
 * @return On success, a pointer to a @ref config_t structure filled
 * with the parsed information is returned. Otherwise NULL is
 * returned.
 *
 * @see syslog(3)
 */
config_t* parseConfig(FILE* logfile, int loglevel, char* configfile);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PARSE_H */
