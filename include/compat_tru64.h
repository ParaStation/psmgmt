/*
 *               ParaStation
 *
 * Copyright (C) 1999-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Tru64 compatibility stuff
 *
 * $Id$
 *
 * \author
 * Jens Hauke <hauke@par-tec.com>
 *
 */
#ifndef _COMPAT_TRU64_H_
#define _COMPAT_TRU64_H_


#define __attribute__( name )

#ifndef __KERNEL__
#define _DEC_XPG

/* #define _BSD */
/* Only defined if _BSD is defined */
extern int      setenv   (const char *, const char *, int);
extern void     unsetenv (const char *);

#endif

#define NO_MACRODOTDOT

#endif /* _COMPAT_TRU64_H_ */
