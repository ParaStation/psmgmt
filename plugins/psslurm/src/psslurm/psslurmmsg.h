/*
 * ParaStation
 *
 * Copyright (C) 2017-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_MESSAGE
#define __PSSLURM_MESSAGE

#include <stdint.h>
#include <time.h>
#include <sys/types.h>

#include "psnodes.h"
#include "pstaskid.h"
#include "plugincomm.h"
#include "slurmmsg.h"

typedef struct {
    char *method;
    uint32_t version;
    char *cred;
} Slurm_Auth_t;

typedef struct {
    uint32_t error;
    uint16_t type;
    PSnodes_ID_t node;
    PS_DataBuffer_t body;
} Slurm_Forward_Data_t;

typedef struct {
    uint16_t version;
    uint16_t flags;
    uint16_t type;
    uint32_t bodyLen;
    uint16_t forward;
    uint16_t returnList;
    uint32_t addr;
    uint16_t port;
    uint32_t timeout;
    uint16_t index;
    uint16_t treeWidth;
    char *nodeList;
    Slurm_Forward_Data_t *fwdata;
    uint32_t fwSize;
    uid_t uid;
    gid_t gid;
} Slurm_Msg_Header_t;

typedef struct {
    Slurm_Msg_Header_t head;	/* Slurm message header */
    int sock;			/* socket the message was red from */
    PStask_ID_t source;		/* root TID of the forwarding tree */
    PS_DataBuffer_t *data;
    char *ptr;
    time_t recvTime;		/* time the message was received */
} Slurm_Msg_t;

typedef struct {
    Slurm_Auth_t *auth;		/* Slurm authentication */
    Slurm_Msg_Header_t head;	/* Slurm message head */
    PS_DataBuffer_t *body;	/* Slurm message body */
    size_t offset;		/* bytes already written */
    int sock;			/* the connected socket */
    int sendRetry;		/* actual retries to send the message */
    int conRetry;		/* actual reconnect attempts */
    int maxConRetry;		/* maximal reconnect attempts */
    int timerID;		/* reconnect timer id */
    time_t authTime;		/* authentication timestamp */
    list_t list;		/* the list element */
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
 * @param auth The Slurm authentication of the message
 *
 * @param sock The socket to send the message out
 *
 * @param written The number of bytes already written
 *
 * @return Returns the buffer holding the saved message
 */
Slurm_Msg_Buf_t *saveSlurmMsg(Slurm_Msg_Header_t *head, PS_DataBuffer_t *body,
			      Slurm_Auth_t *auth, int sock, size_t written);

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
