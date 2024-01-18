/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_MESSAGE
#define __PSSLURM_MESSAGE

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"

#include "pscommon.h"
#include "psserial.h"

/** holding Slurm forward tree results */
typedef struct {
    uint32_t error;		/**< possible forward error */
    uint16_t type;		/**< message type of returned message */
    PSnodes_ID_t node;		/**< node which returned the result */
    PS_DataBuffer_t body;	/**< message payload */
} Slurm_Forward_Res_t;

typedef struct {
    uint32_t ip;		/**< IP */
    uint16_t port;		/**< port */
    uint16_t family;		/**< whether to use IPv4 or IPv6 */
} Slurm_Addr_t;

/** Slurm message header */
typedef struct {
    uint16_t version;		/**< Slurm protocol version */
    uint16_t flags;		/**< currently set to SLURM_GLOBAL_AUTH_KEY */
    uint16_t type;		/**< message type (e.g. REQUEST_LAUNCH_TASKS) */
    uint16_t index;		/**< message index */
    uint32_t bodyLen;		/**< length of the message payload */
    Slurm_Addr_t addr;		/**< senders address */
    uint16_t forward;		/**< message forwarding */
    uint16_t returnList;	/**< number of returned results */
    uint32_t fwTimeout;		/**< forward timeout */
    uint16_t fwTreeWidth;	/**< width of the forwarding tree */
    char *fwNodeList;		/**< node-list to forward the message to */
    Slurm_Forward_Res_t *fwRes; /**< returned results on a per node basis */
    uint32_t fwResSize;		/**< size of the forward results */
    uid_t uid;			/**< user ID of the message sender */
    gid_t gid;			/**< group ID of the message sender */
} Slurm_Msg_Header_t;

typedef struct {
    Slurm_Msg_Header_t head;	/**< Slurm message header */
    int sock;			/**< socket the message was red from */
    PStask_ID_t source;		/**< root TID of the forwarding tree or -1 */
    PS_DataBuffer_t *data;	/**< buffer holding the received (packed)
				     message */
    PS_SendDB_t reply;		/**< send data buffer used to save a response */
    void *unpData;		/**< holding the unpacked message payload */
    time_t recvTime;		/**< time the message was received */
    bool authRequired;		/**< need to verify messages authentification */
} Slurm_Msg_t;

// leave after Slurm_Msg_t definition to break include cycle
#include "psslurmauth.h"  // IWYU pragma: keep

/** callback function of a connection structure */
typedef int Connection_CB_t(Slurm_Msg_t *msg, void *info);

/** structure to make information available about the message request
 * when handling a corresponding response */
typedef struct {
    uint16_t type;	    /**< message type of the request */
    uint16_t expRespType;   /**< expected message type of the response */
    uint32_t jobid;	    /**< optional jobid associated with the request */
    uint32_t stepid;	    /**< optional stepid associated with the request */
    uint32_t stepHetComp;   /**< step het component identifier */
    time_t time;	    /**< time the request was sent */
    Connection_CB_t *cb;    /**< callback to handle a reply */
} Req_Info_t;

typedef struct {
    list_t next;		/**< the list element */
    Slurm_Auth_t *auth;		/**< Slurm authentication */
    Slurm_Msg_Header_t head;	/**< Slurm message head */
    PS_DataBuffer_t body;	/**< Slurm message body */
    size_t offset;		/**< bytes already written */
    int sock;			/**< the connected socket */
    int sendRetry;		/**< actual retries to send the message */
    int conRetry;		/**< actual reconnect attempts */
    int maxConRetry;		/**< maximal reconnect attempts */
    int timerID;		/**< reconnect timer ID */
    time_t authTime;		/**< authentication time-stamp */
    Req_Info_t *req;		/**< optional request information */
} Slurm_Msg_Buf_t;

/**
 * @brief Convert a Slurm message type from integer
 * to string representation
 *
 * @param type The message type to convert
 *
 * @return Returns the result or an empty string on error
 */
const char *msgType2String(int type);

/**
 * @brief Initialize a Slurm message
 *
 * @param msg The message to initialize
 */
void initSlurmMsg(Slurm_Msg_t *msg);

/**
 * @brief Free a Slurm message
 *
 * Close the associated connection, free used memory
 * and reset tracking information.
 *
 * @param sMsg The message to free
 */
void freeSlurmMsg(Slurm_Msg_t *sMsg);

/**
 * @brief Duplicate a Slurm message header
 *
 * Create a duplicate of the Slurm message header @a head. The
 * result will be saved in the empty @a dupHead. In order to cleanup
 * the duplicate appropriately @ref freeSlurmMsgHead shall
 * be called when the duplicate is not needed any longer.
 *
 * @param dupHead An empty Slurm header to fill
 *
 * @param head The original Slurm header
 */
void dupSlurmMsgHead(Slurm_Msg_Header_t *dupHead, Slurm_Msg_Header_t *head);

/**
 * @brief Duplicate SLURM message
 *
 * Create a duplicate of the SLURM message @a sMsg and return a
 * pointer to it. All data buffers @a sMsg is referring to are
 * duplicated, too. In order to cleanup the duplicate appropriately
 * @ref releaseSlurmMsg() shall be called when the duplicate is not
 * needed any longer.
 *
 * @param sMsg SLURM messages to duplicate
 *
 * @return Upon success a pointer to the duplicate message is
 * returned. Or NULL in case of error.
 */
Slurm_Msg_t * dupSlurmMsg(Slurm_Msg_t *sMsg);

/**
 * @brief Release a duplicate SLURM message
 *
 * Release the duplicate SLURM message @a sMsg. This will also release
 * all data buffers @a sMsg is referring to. @a sMsg has to be created
 * by @ref dupSlurmMsg().
 *
 * @warning If @a sMsg is not the results of a call to @ref
 * dupSlurmMsg() the result is undefined and might lead to major
 * memory inconsistencies.
 *
 * @param sMsg SLURM messages to release
 */
void releaseSlurmMsg(Slurm_Msg_t *sMsg);

/**
 * @brief Initialize a Slurm message header
 *
 * @param head The header to initialize
 */
void initSlurmMsgHead(Slurm_Msg_Header_t *head);

/**
 * @brief Free a Slurm message header
 *
 * @param head The header to free
 */
void freeSlurmMsgHead(Slurm_Msg_Header_t *head);

/**
 * @brief Save a Slurm message
 *
 * @param head The message head to save
 *
 * @param body The message body to save
 *
 * @param req Request associated to this message if any
 *
 * @param auth The Slurm authentication of the message
 *
 * @param sock The socket to send the message out
 *
 * @param written The number of bytes already written
 *
 * @return Returns the buffer holding the saved message
 */
Slurm_Msg_Buf_t *saveSlurmMsg(Slurm_Msg_Header_t *head, PS_SendDB_t *body,
			      Req_Info_t *req, Slurm_Auth_t *auth,
			      int sock, size_t written);

/**
 * @brief Delete a Slurm message buffer
 *
 * Cleanup leftover assosiated timers and selectors and free
 * used memory.
 *
 * @param msgBuf The Slurm message to delete
 */
void deleteMsgBuf(Slurm_Msg_Buf_t *msgBuf);

/**
 * @brief Clear all leftover messages from buffer
 */
void clearMsgBuf(void);

/**
 * @brief Determine if a Slurm message needs to be resend
 *
 * @param type The Slurm message type
 *
 * @return Returns true if the messages should be resend
 * otherwise false
 */
bool needMsgResend(uint16_t type);

/**
 * @brief Resend a Slurm message
 *
 * This handler is called if the socket becomes writeable
 * again. The saved message is packed into a data buffer and
 * send out using the provided socket @a sock.
 *
 * @param sock The connected socket to use
 *
 * @param msg Pointer to the saved Slurm message
 *
 * @return Always returns 0
 */
int resendSlurmMsg(int sock, void *msg);

/**
 * @brief Setup a reconnect timer
 *
 * Setup a reconnect timer for a Slurm message.
 * The timer handler will try to open a new connection
 * to the slurmctld and resend the message on success.
 *
 * @param savedMsg The saved message to resend
 *
 * @return On success returns the timer ID or
 * -1 on error.
 */
int setReconTimer(Slurm_Msg_Buf_t *savedMsg);

#endif /* __PSSLURM_MESSAGE */
