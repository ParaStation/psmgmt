/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * ParaStation admin command line parser functions
 */
#ifndef __ADMINPARSER_H
#define __ADMINPARSER_H

#include <stdbool.h>
#include "linenoise.h"

/**
 * @brief Prepare parser
 *
 * Initialize parser. This has to be called before @ref parseLine() is
 * utilized.
 *
 * @return No return value
 */
void parserPrepare(void);

/**
 * @brief Prepare parser for interactive use
 *
 * Enhance parser for interactive use. This has to be called before
 * @ref completeLine() is utilized, which typically happens indirectly
 * via calls to @ref linenoise().
 *
 * @return No return value
 */
void parserPrepInteractive(void);

/**
 * @brief Release parser
 *
 * Cleanup the parser. If @ref parseLine() is called afterwards, its
 * behavior is undefined,
 *
 * @return No return value
 */
void parserRelease(void);

/**
 * @brief Parse admins input line
 *
 * Parse a single line conforming to the syntax of psiadmin an execute
 * to corresponding commands.
 *
 * @param line The line to handle, i.e. to parse and execute.
 *
 * @return If the 'exit' or 'quit' command was reached, true is returned
 * or false otherwise
 */
bool parseLine(char *line);

/**
 * @brief Generate candidates for line completion
 *
 * Generate a list of candidates for linenoise's line completion given
 * the text in @a buf typed in so far. This function is intended to
 * act as linenoise's completion callback, thus it will be called from
 * linenoise whenever a completion has to be done.
 *
 * The actual implementation utilizes the same keylists as the parser
 * used to implement the specific of psiadmin's directives.
 *
 * @param buf The text to complete
 *
 * @param lc Handle to add linenoise's completions to
 *
 * @return No return value
 *
 * @see linenoiseAddCompletion()
 */
void completeLine(const char *buf, linenoiseCompletions *lc);

/* ******************* psiadmin's parameter-space **********************/

/** Flag to print hostnames instead of ParaStation IDs */
extern int paramHostname;

/** Flag to print hexadecimal values on some resource limits */
extern int paramHexFormat;

/** Delay (in ms) between consecutive starts of remote psids */
extern int paramStartDelay;

#endif /* __ADMINPARSER_H */
