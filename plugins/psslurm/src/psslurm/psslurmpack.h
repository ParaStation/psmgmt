/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_PACK
#define __PS_SLURM_PACK

#include <netinet/in.h>
#include <stdbool.h>

#include "psserial.h"
#include "psslurmauth.h"
#include "psslurmbcast.h"
#include "psslurmio.h"
#include "psslurmproto.h"
#include "psslurmjob.h"
#include "psslurmaccount.h"
#include "psaccounttypes.h"

/**
 * @brief Pack a Slurm authentication
 *
 * Pack a Slurm authentication and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param auth The authentication structure to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmAuth(PS_SendDB_t *data, Slurm_Auth_t *auth,
		     const char *caller, const int line);

#define packSlurmAuth(data, auth) \
    __packSlurmAuth(data, auth, __func__, __LINE__)

/**
 * @brief Unpack a Slurm authentication
 *
 * Unpack a Slurm authentication from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The Slurm message to unpack
 *
 * @param auth The authentication structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackSlurmAuth(Slurm_Msg_t *sMsg, Slurm_Auth_t **authPtr,
		       const char *caller, const int line);

#define unpackSlurmAuth(sMsg, authPtr) \
    __unpackSlurmAuth(sMsg, authPtr, __func__, __LINE__)

/**
 * @brief Unpack a munge credential
 *
 * Unpack a munge credential from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The Slurm message to unpack
 *
 * @param auth The authentication structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackMungeCred(Slurm_Msg_t *sMsg, Slurm_Auth_t *auth,
		       const char *caller, const int line);

#define unpackMungeCred(sMsg, authPtr) \
    __unpackMungeCred(sMsg, authPtr, __func__, __LINE__)

/**
 * @brief Unpack a job credential
 *
 * Unpack a job credential including the embedded gres
 * credential. The memory is allocated using umalloc().
 * The caller is responsible to free the memory using ufree().
 *
 * @param sMsg The Slurm message to unpack
 *
 * @param cred The job credential holding the result
 *
 * @param gres The gres credential holding the result
 *
 * @param credEnd Pointer updated to the end of the credential
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackJobCred(Slurm_Msg_t *sMsg, JobCred_t **credPtr,
		     list_t *gresList, char **credEnd, const char *caller,
		     const int line);

#define unpackJobCred(sMsg, credPtr, gresList, credEnd) \
    __unpackJobCred(sMsg, credPtr, gresList, credEnd, __func__, __LINE__)

/**
 * @brief Unpack a BCast credential
 *
 * Unpack a BCast credential from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @param bcast The bcast credential holding the result
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackBCastCred(Slurm_Msg_t *sMsg, BCast_Cred_t *cred,
		       const char *caller, const int line);

#define unpackBCastCred(sMsg, bcast) \
    __unpackBCastCred(sMsg, bcast, __func__, __LINE__)

/**
 * @brief Pack a Slurm message header
 *
 * Pack a Slurm message header and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param head The Slurm message header to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmHeader(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
		       const char *caller, const int line);

#define packSlurmHeader(data, head) \
    __packSlurmHeader(data, head, __func__, __LINE__)

/**
 * @brief Pack a Slurm I/O message
 *
 * Pack a Slurm I/O message and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param ioMsg The Slurm I/O head to pack
 *
 * @param body The message body to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmIOMsg(PS_SendDB_t *data, IO_Slurm_Header_t *ioh, char *body,
		      const char *caller, const int line);

#define packSlurmIOMsg(data, ioMsg, body) \
    __packSlurmIOMsg(data, ioMsg, body, __func__, __LINE__)

/**
 * @brief Unpack a Slurm message header
 *
 * Unpack a Slurm message header from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param ptr The message to unpack the data from
 *
 * @param head The Slurm message header holding the result
 *
 * @param fw The Slurm forward header holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a ptr might be not updated.
 */
bool __unpackSlurmHeader(char **ptr, Slurm_Msg_Header_t *head,
			 Msg_Forward_t *fw, const char *caller, const int line);

#define unpackSlurmHeader(ptr, head, fw) \
    __unpackSlurmHeader(ptr, head, fw, __func__, __LINE__)

/**
 * @brief Unpack a Slurm I/O message header
 *
 * Unpack a Slurm I/O message header from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param ptr The message to unpack the data from
 *
 * @param iohPtr The Slurm I/O message header holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a ptr might be not updated.
 */
bool __unpackSlurmIOHeader(char **ptr, IO_Slurm_Header_t **iohPtr,
			   const char *caller, const int line);

#define unpackSlurmIOHeader(ptr, iohPtr) \
    __unpackSlurmIOHeader(ptr, iohPtr, __func__, __LINE__)

/**
 * @brief Pack a ping response
 *
 * Pack a ping response and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param ping The ping structure to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packRespPing(PS_SendDB_t *data, Resp_Ping_t *ping,
		    const char *caller, const int line);

#define packRespPing(data, ping) \
    __packRespPing(data, ping, __func__, __LINE__)

/**
 * @brief Pack Slurm account data
 *
 * Pack Slurm account data structure and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param slurmAccData The account structure to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmAccData(PS_SendDB_t *data, SlurmAccData_t *slurmAccData,
			const char *caller, const int line);

#define packSlurmAccData(data, slurmAccData) \
    __packSlurmAccData(data, slurmAccData, __func__, __LINE__)

/**
 * @brief Pack a Slurm message
 *
 * Pack a Slurm message and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param head The Slurm head to pack
 *
 * @param body The message body to pack
 *
 * @param auth The Slurm authentication to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmMsg(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
		    PS_DataBuffer_t *body, Slurm_Auth_t *auth,
		    const char *caller, const int line);

#define packSlurmMsg(data, head, body, auth) \
    __packSlurmMsg(data, head, body, auth, __func__, __LINE__)

/**
 * @brief Pack a daemon status response
 *
 * Pack a daemon status response and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param stat The status structure to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packRespDaemonStatus(PS_SendDB_t *data, Resp_Daemon_Status_t *stat,
			    const char *caller, const int line);

#define packRespDaemonStatus(data, stat) \
    __packRespDaemonStatus(data, stat, __func__, __LINE__)

/**
 * @brief Pack a launch tasks response
 *
 * Pack a launch tasks response and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param ltasks The launch tasks structure to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packRespLaunchTasks(PS_SendDB_t *data, Resp_Launch_Tasks_t *ltasks,
			   const char *caller, const int line);

#define packRespLaunchTasks(data, ltasks) \
    __packRespLaunchTasks(data, ltasks, __func__, __LINE__)

/**
 * @brief Pack dummy energy data
 *
 * Pack dummy energy data and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packEnergyData(PS_SendDB_t *data, const char *caller,
		      const int line);

#define packEnergyData(data) __packEnergyData(data, __func__, __LINE__)

/**
 * @brief Pack TRes (trackable resources) data
 *
 * Pack TRes data and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packTResData(PS_SendDB_t *data, TRes_t *tres, const char *caller,
		    const int line);

#define packTResData(data, tres) __packTResData(data, tres, __func__, __LINE__)

/**
 * @brief Pack message task exit data
 *
 * Pack message task exit data and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param msg The data to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packMsgTaskExit(PS_SendDB_t *data, Msg_Task_Exit_t *msg,
		       const char *caller, const int line);

#define packMsgTaskExit(data, msg) \
    __packMsgTaskExit(data, msg, __func__, __LINE__)

/**
 * @brief Pack Slurm PIDs
 *
 * Pack Slurm PIDs and add it to the provided data buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param pids The data to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmPIDs(PS_SendDB_t *data, Slurm_PIDs_t *pids,
		     const char *caller, const int line);
#define packSlurmPIDs(data, req) __packSlurmPIDs(data, req, __func__, __LINE__)

/**
 * @brief Unpack a Slurm message
 *
 * For the given Slurm message the apropriate unpack function
 * will be called. The unpack function is selected by the message type.
 * The unpacked data is stored in the Slurm message @ref sMsg in
 * the unpData field. If the message does not contain a payload to unpack,
 * the unpData filed will be set to NULL.
 *
 * The memory for the unpacked data is allocated using umalloc().
 * The caller is responsible to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If unpacking was not successful, @a msg might be not updated.
 */
bool __unpackSlurmMsg(Slurm_Msg_t *sMsg, const char *caller, const int line);
#define unpackSlurmMsg(sMsg) __unpackSlurmMsg(sMsg, __func__, __LINE__)

/**
 * @brief Pack a Slurm request
 *
 * @param req The request meta information
 *
 * @param msg Message buffer which holds the result
 *
 * @param reqData The data to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a msg might be not updated.
 */
bool packSlurmReq(Req_Info_t *reqInfo, PS_SendDB_t *msg, void *reqData,
		  const char *caller, const int line);

#endif  /* __PS_SLURM_PACK */
