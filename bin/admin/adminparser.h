/*
 *               ParaStation
 * adminparser.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: adminparser.h,v 1.2 2003/09/12 14:24:49 eicker Exp $
 *
 */
/**
 * \file
 * ParaStation admin command line parser functions
 *
 * $Id: adminparser.h,v 1.2 2003/09/12 14:24:49 eicker Exp $
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
