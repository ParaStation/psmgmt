/*
 *               ParaStation
 * psidoption.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidoption.h,v 1.2 2003/12/10 16:40:57 eicker Exp $
 *
 */
/**
 * @file
 * Handle option requests to the ParaStation daemon.
 *
 * $Id: psidoption.h,v 1.2 2003/12/10 16:40:57 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDOPTIONS_H
#define __PSIDOPTIONS_H

#include "psprotocol.h"
#include "psnodes.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Send some options.
 *
 * Send some options upon startup of a daemon-daemon connection to @a
 * destnode's daemon.
 *
 * @param destnode The node the options should be send to.
 *
 * @return No return value.
 */
void send_OPTIONS(PSnodes_ID_t destnode);

/**
 * @brief Handle a PSP_CD_SETOPTION message.
 *
 * Handle the message @a inmsg of type PSP_CD_SETOPTION.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_SETOPTION(DDOptionMsg_t *msg);

/**
 * @brief Handle a PSP_CD_GETOPTION message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETOPTION.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETOPTION(DDOptionMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDOPTIONS_H */
