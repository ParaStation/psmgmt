/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMIX_UTIL
#define __PS_PMIX_UTIL

#include "pspmixtypes.h"

/**
 * Print server using mlog()
 *
 * Prints server and selected forwarder data if available
 *
 * @param server    server to print
 * @param sessions  print all sessions, jobs and reservations, too
 * @param caller    function name of the calling function
 * @param line      line number where this function is called
 */
void __pspmix_printServer(PspmixServer_t *server, bool printSessions,
			  const char *caller, const int line);

#define pspmix_printServer(server, sessions) \
	__pspmix_printServer(server, sessions, __func__, __LINE__)

/**
 * Unlist and free a job object
 *
 * @param job  job to delete
 */
void pspmix_deleteJob(PspmixJob_t *job);

/**
 * Unlist and free a session object
 *
 * @param session  session to delete
 * @param warn     warn if job list is not empty
 * @param caller   function name of the calling function
 * @param line     line number where this function is called
 *
 */
void __pspmix_deleteSession(PspmixSession_t *session, bool warn,
			  const char *caller, const int line);

#define pspmix_deleteSession(session, warn) \
	__pspmix_deleteSession(session, warn, __func__, __LINE__)

/**
 * Unlist and free a server object
 *
 * @param server  server to delete
 * @param warn    warn if session list is not empty
 * @param caller  function name of the calling function
 * @param line    line number where this function is called
 */
void __pspmix_deleteServer(PspmixServer_t *server, bool warn,
			 const char *caller, const int line);

#define pspmix_deleteServer(server, warn) \
	__pspmix_deleteServer(server, warn, __func__, __LINE__)

#endif
