/*
 *               ParaStation
 * adminparser.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: adminparser.h,v 1.1 2003/08/15 13:20:54 eicker Exp $
 *
 */
/**
 * \file
 * ParaStation admin command line parser functions
 *
 * $Id: adminparser.h,v 1.1 2003/08/15 13:20:54 eicker Exp $
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

/** @todo Add docu */
#define quitMagic 17

/**
 * @brief Parse admins input line.
 *
 * @todo Add docu
 */
int parseLine(char *line);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __ADMINPARSER_H */
