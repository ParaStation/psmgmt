/*
 *               ParaStation
 * psidoption.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidoption.h,v 1.4 2004/01/28 14:01:05 eicker Exp $
 *
 */
/**
 * @file
 * Handle option requests to the ParaStation daemon.
 *
 * $Id: psidoption.h,v 1.4 2004/01/28 14:01:05 eicker Exp $
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
 * If the final destination of @a msg is the local daemon, the options
 * provided within this message are set to the corresponding
 * values. Otherwise this message es forwarded, either to the
 * corresponding local or remote client process or a remote daemon.
 *
 * PSP_CD_SETOPTION messages to client processes are usually responses
 * to PSP_CD_GETOPTION request of this processes.
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
 * If the final destination of @a msg is the local daemon, the
 * requested options within this message are determined and send
 * within a PSP_CD_SETOPTION to the requestor. Otherwise this message
 * es forwarded to the corresponding remote daemon.
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
