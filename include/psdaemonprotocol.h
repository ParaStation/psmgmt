/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * ParaStation daemon-daemon high-level protocol.
 *
 * $Id$
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
#define PSDaemonProtocolVersion  406

/** IDs of the various message types */

   /*******************************************************************/
   /* The IDs below 0x0100 on are reserved for client-daemon messages */
   /*******************************************************************/

/** First messages used for setting up connections between daemons */
#define PSP_DD_DAEMONCONNECT       0x0100  /**< Request to connect daemon */
#define PSP_DD_DAEMONESTABLISHED   0x0101  /**< Connection request accepted */
#define PSP_DD_DAEMONREFUSED       0x0102  /**< Connection request denied */
#define PSP_DD_DAEMONSHUTDOWN      0x0103  /**< Daemon goes down */

/** Message implementing flow control between daemons */
#define PSP_DD_SENDSTOP            0x0108  /**< Stop sending further packets */
#define PSP_DD_SENDCONT            0x0109  /**< Continue sending packets */

/** Messages between daemon and the daemon forwarder part */
#define PSP_DD_CHILDDEAD           0x0110  /**< Tell a child has finished */
#define PSP_DD_CHILDBORN           0x0111  /**< Tell child was created */
#define PSP_DD_CHILDACK            0x0112  /**< Ack the newly created child */

/** Messages used to propagate kinship */
#define PSP_DD_NEWCHILD            0x0118  /**< Tell task about grandchild
					      inherited from it's child */
#define PSP_DD_NEWPARENT           0x0119  /**< Tell task about grandparent
					      since parent died gracefully */

/** Messages between daemon and master */
#define PSP_DD_GETPART             0x0120  /**< Get partition from master */
#define PSP_DD_GETPARTNL           0x0121  /**< Partition request nodelist */
#define PSP_DD_PROVIDEPART         0x0122  /**< Reply partition bound */
#define PSP_DD_PROVIDEPARTSL       0x0123  /**< Partition reply slotlist */
#define PSP_DD_GETNODES            0x0124  /**< Forwarded GETNODES message */
#define PSP_DD_GETTASKS            0x0125  /**< Get tasks from slaves */
#define PSP_DD_PROVIDETASK         0x0126  /**< Reply tasks */
#define PSP_DD_PROVIDETASKSL       0x0127  /**< Task reply slotlist */
#define PSP_DD_CANCELPART          0x0128  /**< Cancel partition request */
#define PSP_DD_TASKDEAD            0x0129  /**< Complete task finished */
#define PSP_DD_TASKSUSPEND         0x012A  /**< Task got SIGTSTP */
#define PSP_DD_TASKRESUME          0x012B  /**< Task got SIGCONT */
#define PSP_DD_GETRANKNODE         0x012C  /**< Forwarded GETRANKNODE mesg */

/** Messages used to find master */
#define PSP_DD_LOAD                0x0130  /**< Load message to master */
#define PSP_DD_ACTIVE_NODES        0x0131  /**< Master's active clients list */
#define PSP_DD_DEAD_NODE           0x0132  /**< Master's dead node info */
#define PSP_DD_MASTER_IS           0x0133  /**< Info about correct master */

/** Messages for ressource allocation */
#define PSP_DD_NODESRES            0x0138  /**< Get nodes from a partition */

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
