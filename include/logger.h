/*
 *               ParaStation3
 * logger.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: logger.h,v 1.10 2002/07/26 15:23:01 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for interaction with the ParaStation Logger.
 *
 * $Id: logger.h,v 1.10 2002/07/26 15:23:01 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __LOGGER_H__
#define __LOGGER_H__

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Documentation */

/*********************************************************************
 * int LOGGERopenPort()
 *
 * open the logger port.
 * RETURN the portno of the logger
 */
unsigned short LOGGERopenPort(void);

/**
 * void LOGGERexecLogger()
 *
 * spawns a logger.
 * @return No return value.
 *
 * @see LOGGERopenPort()
 */
void LOGGERexecLogger(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __LOGGER_H */
