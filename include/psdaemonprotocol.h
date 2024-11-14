/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation daemon-daemon high-level protocol.
 */
#ifndef __PSDAEMONPROTOCOL_H
#define __PSDAEMONPROTOCOL_H

#include <stdint.h>

#include "psprotocol.h" // IWYU pragma: export

/** Unique version number of the high-level protocol */
#define PSDaemonProtocolVersion  418

/** IDs of the various message types */

   /*******************************************************************/
   /* The IDs below 0x0100 on are reserved for client-daemon messages */
   /*******************************************************************/

/** First messages used for setting up connections between daemons */
#define PSP_DD_DAEMONCONNECT       0x0100  /**< Request to connect daemon */
#define PSP_DD_DAEMONESTABLISHED   0x0101  /**< Connection request accepted */
#define PSP_DD_DAEMONSHUTDOWN      0x0102  /**< Daemon goes down */

/** Message implementing flow control between daemons */
#define PSP_DD_SENDSTOP            0x0108  /**< Stop sending further packets */
#define PSP_DD_SENDCONT            0x0109  /**< Continue sending packets */
#define PSP_DD_SENDSTOPACK         0x010A  /**< Ack a SENDSTOP message */

/** Messages between daemon and the daemon forwarder part */
#define PSP_DD_CHILDDEAD           0x0110  /**< Tell a child has finished */
#define PSP_DD_CHILDBORN           0x0111  /**< Tell child was created */
#define PSP_DD_CHILDACK            0x0112  /**< Ack the newly created child */
#define PSP_DD_CHILDRESREL         0x0113  /**< Release a child's resources */

/** Messages used to propagate kinship */
#define PSP_DD_NEWANCESTOR         0x0118  /**< Tell node about released
					      parent to inherit children */
#define PSP_DD_ADOPTCHILDSET       0x0119  /**< Tell task about a set of
					      grandchildren to be inherited */
#define PSP_DD_ADOPTFAILED         0x011A  /**< Tell children about a failed
					      adoption */
#define PSP_DD_INHERITDONE         0x011B  /**< Tell task about a finished
					      adoption of children */
#define PSP_DD_INHERITFAILED       0x011C  /**< Tell task about a failed
					      adoption */

/** Messages between daemon and master */
#define PSP_DD_CREATEPART          0x0120  /**< Create partition at master */
#define PSP_DD_PROVIDEPART         0x0121  /**< Reply newly created partition */
#define PSP_DD_REGISTERPART        0x0122  /**< Register partition at master */
#define PSP_DD_CANCELPART          0x0123  /**< Cancel partition request */
#define PSP_DD_GETTASKS            0x0124  /**< Get tasks from slaves */
#define PSP_DD_PROVIDETASK         0x0125  /**< Reply tasks */
#define PSP_DD_TASKDEAD            0x0126  /**< Complete task finished */
#define PSP_DD_TASKSUSPEND         0x0127  /**< Task got SIGTSTP */
#define PSP_DD_TASKRESUME          0x0128  /**< Task got SIGCONT */

/** Messages used to find master */
#define PSP_DD_LOAD                0x0130  /**< Load message to master */
#define PSP_DD_ACTIVE_NODES        0x0131  /**< Master's active clients list */
#define PSP_DD_DEAD_NODE           0x0132  /**< Master's dead node info */
#define PSP_DD_MASTER_IS           0x0133  /**< Info about correct master */

/** Messages for resource allocation */
#define PSP_DD_GETRESERVATION      0x0138  /**< Forwarded GETRESERVATION msg */
#define PSP_DD_RESERVATIONRES      0x0139  /**< Answer dummy GETRESERVATION */
#define PSP_DD_GETSLOTS            0x013A  /**< Forwarded GETSLOTS msg */
#define PSP_DD_SLOTSRES            0x013B  /**< Results of GETSLOTS msg */
#define PSP_DD_GETNODES            0x013C  /**< Forwarded GETNODES message */
#define PSP_DD_NODESRES            0x013D  /**< Get nodes from a partition */

/** Messages for resource passing during spawn and finish */
#define PSP_DD_RESCREATED          0x0140  /**< Reservation created with info */
#define PSP_DD_RESRELEASED         0x0141  /**< Reservation released */
#define PSP_DD_RESSLOTS            0x0142  /**< Node's reservation slots */
#define PSP_DD_RESCLEANUP          0x0143  /**< Cleanup reservations */
#define PSP_DD_RESFINALIZED        0x0144  /**< No more reservations expected */
#define PSP_DD_JOBCOMPLETE         0x0145  /**< All information on provided */

    /***********************************************************/
    /* The IDs from 0x0200 on are reserved for plugin messages */
    /***********************************************************/

/**
 * @brief Generate a string describing the message type
 *
 * Generate a character string describing the message type @a
 * msgtype. The message type has to contain one of the PSP_DD_,
 * PSP_CD_ PSP_CC_, or PSP_PLUG_ message type constants. For the
 * PSP_CD_ and PSP_CC_ type messages, PSP_printMsg() is used.
 *
 * @param msgtype Message type the name should be generated for
 *
 * @return A pointer to the '\0' terminated character string
 * containing the name of the message type or a special message
 * containing @a msgtype if the type is unknown
 */
char *PSDaemonP_printMsg(int msgtype);

/**
 * @brief Resolve message type
 *
 * Analyse the character string @a typeStr and return the
 * corresponding message type. This is basically the inverse operation
 * of @ref PSDaemonP_printMsg() and uses the identical type
 * strings. For types covered in psprotocol.h @ref PSP_resolveType()
 * will be used implicitly.
 *
 * @param typeStr Name of the message type to resolve
 *
 * @return The identified message type or -1 otherwise
 */
int16_t PSDaemonP_resolveType(char *typeStr);

#endif /* __PSDAEMONPROTOCOL_H */
