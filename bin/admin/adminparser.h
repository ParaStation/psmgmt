/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
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

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Parse admins input line.
 *
 * Parse a single line conforming to the syntax of psiadmin an execute
 * to corresponding commands.
 *
 * @param line The line to handle, i.e. to parse and execute.
 *
 * @return If the 'exit' or 'quit' command was reached, 1 is returned
 * or 0 otherwise.
 */
int parseLine(char *line);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __ADMINPARSER_H */
