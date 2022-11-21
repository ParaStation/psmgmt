/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
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

#include "psprotocol.h" // IWYU pragma: export

/** Unique version number of the high-level protocol */
#define PSDaemonProtocolVersion  415

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
#define PSP_DD_SENDSTOPACK         0x010A  /**< Ack a SENDSTOP message */

/** Messages between daemon and the daemon forwarder part */
#define PSP_DD_CHILDDEAD           0x0110  /**< Tell a child has finished */
#define PSP_DD_CHILDBORN           0x0111  /**< Tell child was created */
#define PSP_DD_CHILDACK            0x0112  /**< Ack the newly created child */
#define PSP_DD_CHILDRESREL         0x0113  /**< Release a child's resources */

/** Messages used to propagate kinship */
#define PSP_DD_NEWCHILD            0x0118  /**< Tell task about grandchild
					      inherited from it's child */
#define PSP_DD_NEWPARENT           0x0119  /**< Tell task about grandparent
					      since parent died gracefully */
#define PSP_DD_NEWANCESTOR         0x011A  /**< Tell node about released
					      parent to inherit children
					       (obsoletes PSP_OP_NEWPARENT) */
#define PSP_DD_ADOPTCHILDSET       0x011B  /**< Tell task about a set of
					      grandchildren to be inherited
					      (obsoletes PSP_OP_NEWCHILD) */
#define PSP_DD_ADOPTFAILED         0x011D  /**< Tell children about a failed
					      adoption */
#define PSP_DD_INHERITDONE         0x011C  /**< Tell task about a finished
					      adoption of children */
#define PSP_DD_INHERITFAILED       0x011E  /**< Tell task about a failed
					      adoption */

/** Messages between daemon and master */
#define PSP_DD_GETPART             0x0120  /**< Get partition from master */
#define PSP_DD_GETPARTNL           0x0121  /**< Partition request node-list */
#define PSP_DD_PROVIDEPART         0x0122  /**< Reply partition bound */
#define PSP_DD_PROVIDEPARTSL       0x0123  /**< Partition reply slot-list */
#define PSP_DD_GETNODES            0x0124  /**< Forwarded GETNODES message */
#define PSP_DD_GETTASKS            0x0125  /**< Get tasks from slaves */
#define PSP_DD_PROVIDETASK         0x0126  /**< Reply tasks */
#define PSP_DD_PROVIDETASKSL       0x0127  /**< Task reply slot-list */
#define PSP_DD_CANCELPART          0x0128  /**< Cancel partition request */
#define PSP_DD_TASKDEAD            0x0129  /**< Complete task finished */
#define PSP_DD_TASKSUSPEND         0x012A  /**< Task got SIGTSTP */
#define PSP_DD_TASKRESUME          0x012B  /**< Task got SIGCONT */
#define PSP_DD_GETRANKNODE         0x012C  /**< Forwarded GETRANKNODE msg */
#define PSP_DD_PROVIDETASKRP       0x012D  /**< Task reply reserved ports */
#define PSP_DD_PROVIDEPARTRP       0x012E  /**< Partition reply reserved
					      ports */

/** Messages used to find master */
#define PSP_DD_LOAD                0x0130  /**< Load message to master */
#define PSP_DD_ACTIVE_NODES        0x0131  /**< Master's active clients list */
#define PSP_DD_DEAD_NODE           0x0132  /**< Master's dead node info */
#define PSP_DD_MASTER_IS           0x0133  /**< Info about correct master */

/** Messages for resource allocation */
#define PSP_DD_NODESRES            0x0138  /**< Get nodes from a partition */
#define PSP_DD_REGISTERPART        0x0139  /**< Register partition at master */
#define PSP_DD_REGISTERPARTSL      0x013A  /**< Part. registration slot-list */
#define PSP_DD_REGISTERPARTRP      0x013B  /**< Part. registration ports */
#define PSP_DD_GETRESERVATION      0x013C  /**< Forwarded GETRESERVATION msg */
#define PSP_DD_GETSLOTS            0x013D  /**< Forwarded GETSLOTS msg */
#define PSP_DD_SLOTSRES            0x013E  /**< Results of GETSLOTS msg */

/** Messages for resource passing during spawn and finish */
#define PSP_DD_SPAWNLOC            0x0140  /**< Resources for spawn */
#define PSP_DD_RESCREATED          0x0141  /**< Reservation created with info */
#define PSP_DD_RESRELEASED         0x0142  /**< Reservation released */
#define PSP_DD_RESSLOTS            0x0143  /**< Node's reservation slots */

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
