/*
 *               ParaStation3
 * psdaemonprotocol.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psdaemonprotocol.h,v 1.1 2003/03/19 17:05:42 eicker Exp $
 *
 */
/**
 * @file
 * ParaStation daemon-daemon high-level protocol.
 *
 * $Id: psdaemonprotocol.h,v 1.1 2003/03/19 17:05:42 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSDAEMONPROTOCOL_H
#define __PSDAEMONPROTOCOL_H

#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Unique version number of the high-level protocol */
#define PSDaemonprotocolversion  400

/** IDs of the various message types */

   /*******************************************************************/
   /* The IDs below 0x0100 on are reserved for client-daemon messages */
   /*******************************************************************/

/** First messages used for setting up connections between daemons */
#define PSP_DD_DAEMONCONNECT       0x0100  /**< Request to connect daemon */
#define PSP_DD_DAEMONESTABLISHED   0x0101  /**< Connection request accepted */
#define PSP_DD_DAEMONREFUSED       0x0102  /**< Connection request denied */

/** Message between daemon and the daemon forwarder part */
#define PSP_DD_CHILDDEAD           0x0110  /**< Tell a child has finished */

/**
 * @brief Generate a string describing the message type.
 *
 * Generate a character string describing the message type @a
 * msgtype. The message type has to contain one of the PSP_DD_,
 * PSP_CD_ or PSP_CC_ message type constants. For the PSP_CD_ and
 * PSP_CC_ type message, PSP_printMsg() is used.
 *
 * @param msgtype Message type the name should be generated for.
 *
 * @return A pointer to the '\0' terminated character string
 * containing the name of the message type or a special message
 * containing @a msgtype if the type is unknown.
 */
char *PSDaemonP_printMsg(int msgtype);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSDAEMONPROTOCOL_H */
