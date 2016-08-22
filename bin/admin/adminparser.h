/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * \file
 * ParaStation admin command line parser functions
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __ADMINPARSER_H
#define __ADMINPARSER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Prepare parser
 *
 * Initialize parser. This has to be called before @ref parseLine().
 *
 * @return No return value.
 */
void parserPrepare(void);

/**
 * @brief Release parser
 *
 * Cleanup the parser. The behavior is undefined, if @ref parseLine()
 * is called afterwards.
 *
 * @return No return value.
 */
void parserRelease(void);

/**
 * @brief Parse admins input line.
 *
 * Parse a single line conforming to the syntax of psiadmin an execute
 * to corresponding commands.
 *
 * @param line The line to handle, i.e. to parse and execute.
 *
 * @return If the 'exit' or 'quit' command was reached, true is returned
 * or false otherwise.
 */
bool parseLine(char *line);

/**
 * @brief Generate candidate for line completion
 *
 * Generate a list list of candidates for readline's line completion
 * given the word @a text typed in so far. This function is intended
 * to act as readline's @a rl_attempted_completion_function, thus it
 * will be called from readline whenever a completion has to be done.
 *
 * The actual implementation utilizes the same keylists as the parser
 * used to implement the special syntax of psiadmin's directives.
 *
 * @param text The text to complete
 *
 * @param start The position of the first character of @a text within
 * readline's @a rl_line_buffer.
 *
 * @param end The position of the last character of @a text within
 * readline's @a rl_line_buffer.
 *
 * @return If possible completions are found, a array of these strings
 * is returned. The actual array returned is created using readline's
 * @ref rl_completion_matches() function.
 *
 * @see rl_line_buffer, rl_completion_matches()
 */
char **completeLine(const char *text, int start, int end);

/* ******************* psiadmin's parameter-space **********************/

/** Flag to print hostnames instead of ParaStation IDs */
extern int paramHostname;

/** Flag to print hexadecimal values on some resource limits */
extern int paramHexFormat;


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __ADMINPARSER_H */
