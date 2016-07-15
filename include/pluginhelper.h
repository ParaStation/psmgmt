/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PLUGIN_LIB_HELPER
#define __PS_PLUGIN_LIB_HELPER

#include <time.h>

#include "psnodes.h"

/**
 * @brief Remove a directory recursive.
 *
 * @param directory The directory to remove.
 *
 * @param root Also delete the root directory.
 *
 * @return Returns 0 on error and 1 on success.
 */
int removeDir(char *directory, int root);

/**
 * @brief Get the PS Node ID by hostname.
 *
 * @param host The hostname to get the nodeID for.
 *
 * @return Returns the requested nodeID or -1 on error.
 */
PSnodes_ID_t getNodeIDbyName(char *host);

/**
 * @brief Get the hostname from a PS nodeID.
 *
 * @param id The nodeID to lookup the hostname for.
 *
 * @return Returns the requested hostname or NULL on error.
 */
const char *getHostnameByNodeId(PSnodes_ID_t id);

/**
 * @brief Block a signal.
 *
 * @param signal The signal to block.
 *
 * @param block Flag to block the signal if set to 1 or unblock it if set to 0.
 *
 * @return No return value.
 */
void blockSignal(int signal, int block);

/**
 * @brief Eliminate leading whitespaces
 *
 * Remove all leading whitespace (i.e. just <space> characters) from
 * the character array @a string. This function actually does not
 * modify @a string but returns a pointer to the first non-space
 * character inside the array.
 *
 * @param string Character array to be trimmed
 *
 * @return Pointer to the first non-space character within @a string
 */
char *ltrim(char *string);

/**
 * @brief Eliminate trailing whitespaces
 *
 * Remove all trailing whitespace (i.e. just <space> and \n
 * characters) from the character array @a string. For this, all such
 * characters are replaced by \0 characters in the original character
 * array.
 *
 * @param string Character array to be trimmed
 *
 * @return @a string is returned
 */
char *rtrim(char *string);

/**
 * @brief Eliminate leading and trailing whitespace
 *
 * Apply @ref ltrim() and @ref rtrim() to @a string.
 *
 * @param string Character array to be trimmed
 *
 * @return According to @ref ltrim()'s return value.
 */
char *trim(char *string);

/**
 * @brief Eliminate leading and trailing quotes
 *
 * Remove single leading and trailing double-quote (<">)
 * characters. For this, a trailing double-quote is replace by \0
 * while a leading double-quote is skipped by returned a pointer
 * pointing behind it.
 *
 * @param string Character array to be cleaned from double-quotes
 *
 * @return If @a string's first character is a double-quote, @a string
 * + 1 is returned. Or @a string otherwise.
 */
char *trim_quotes(char *string);

/**
 * @brief Get character string describing the local time
 *
 * Get a character string holding the local time according to @a time
 * formatted as "%Y-%m-%d %H:%M:%S". For this a static character array
 * is used. Thus, futures calls to this function will modify the
 * resulting character string.
 *
 * @param time The time to be printed
 *
 * @return Pointer to a character array holding the time description
 */
char *printTime(time_t time);

/**
 * @brief Extract seconds from time
 *
 * Extract the number of seconds from the character string @a wtime
 * describing a time period. @a wtime is expected in the format
 * [[hh:]mm:]ss.
 *
 * @param wtime String describing the time
 *
 * @return On success the number of seconds described by @a wtime is
 * returned. Otherwise 0 is returned.
 *
 */
unsigned long stringTimeToSec(char *wtime);

/**
 * @brief Print binary data in hex format
 *
 * @param data Pointer to data to print
 *
 * @param len The lenght of the data to print
 *
 * @param tag The tag will be printed in front of the data
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __printBinaryData(char *data, size_t len, char *tag,
			const char *func, const int line);

#define printBinaryData(data, len, tag) \
	__printBinaryData(data, len, tag, __func__, __LINE__)

#endif  /* __PS_PLUGIN_LIB_HELPER */
