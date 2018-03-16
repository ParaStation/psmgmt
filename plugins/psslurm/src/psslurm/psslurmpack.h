/*
 * ParaStation
 *
 * Copyright (C) 2016-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_PACK
#define __PS_SLURM_PACK

#include <netinet/in.h>
#include <stdbool.h>

#include "plugincomm.h"
#include "psslurmauth.h"
#include "psslurmio.h"
#include "psslurmproto.h"
#include "psslurmjob.h"
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
bool __packSlurmAuth(PS_DataBuffer_t *data, Slurm_Auth_t *auth,
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
bool __packSlurmHeader(PS_DataBuffer_t *data, Slurm_Msg_Header_t *head,
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
bool __packSlurmIOMsg(PS_DataBuffer_t *data, Slurm_IO_Header_t *ioh, char *body,
			const char *caller, const int line);

#define packSlurmIOMsg(data, ioMsg, body) \
    __packSlurmIOMsg(data, ioMsg, body, __func__, __LINE__)

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
bool __unpackSlurmIOHeader(char **ptr, Slurm_IO_Header_t **iohPtr,
			   const char *caller, const int line);

#define unpackSlurmIOHeader(ptr, iohPtr) \
    __unpackSlurmIOHeader(ptr, iohPtr, __func__, __LINE__)

/**
 * @brief Unpack a terminate request
 *
 * Unpack a terminate request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @param reqPtr The request structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackReqTerminate(Slurm_Msg_t *sMsg, Req_Terminate_Job_t **reqPtr,
			  const char *caller, const int line);

#define unpackReqTerminate(sMsg, reqPtr) \
    __unpackReqTerminate(sMsg, reqPtr, __func__, __LINE__)

/**
 * @brief Unpack a signal tasks request
 *
 * Unpack a signal tasks request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @param reqPtr The request structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackReqSignalTasks(Slurm_Msg_t *sMsg, Req_Signal_Tasks_t **reqPtr,
			    const char *caller, const int line);
#define unpackReqSignalTasks(sMsg, reqPtr) \
    __unpackReqSignalTasks(sMsg, reqPtr, __func__, __LINE__)

/**
 * @brief Unpack a task launch request
 *
 * Unpack a task launch request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @param stepPtr The step structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackReqLaunchTasks(Slurm_Msg_t *sMsg, Step_t **stepPtr,
			    const char *caller, const int line);

#define unpackReqLaunchTasks(sMsg, stepPtr) \
    __unpackReqLaunchTasks(sMsg, stepPtr, __func__, __LINE__)

/**
 * @brief Unpack a job launch request
 *
 * Unpack a job launch request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @param jobPPtr The job structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackReqBatchJobLaunch(Slurm_Msg_t *sMsg, Job_t **jobPtr,
			       const char *caller, const int line);

#define unpackReqBatchJobLaunch(sMsg, jobPtr) \
    __unpackReqBatchJobLaunch(sMsg, jobPtr, __func__, __LINE__)

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
bool __packRespPing(PS_DataBuffer_t *data, Resp_Ping_t *ping,
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
bool __packSlurmAccData(PS_DataBuffer_t *data, SlurmAccData_t *slurmAccData,
		        const char *caller, const int line);

#define packSlurmAccData(data, slurmAccData) \
    __packSlurmAccData(data, slurmAccData, __func__, __LINE__)

/**
 * @brief Pack a node status response
 *
 * Pack a node status response and add it to the provided data
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
bool __packRespNodeRegStatus(PS_DataBuffer_t *data, Resp_Node_Reg_Status_t *stat,
			     const char *caller, const int line);

#define packRespNodeRegStatus(data, stat) \
    __packRespNodeRegStatus(data, stat, __func__, __LINE__)

/**
 * @brief Unpack a file bcast request
 *
 * Unpack a file bcast request from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param sMsg The message to unpack
 *
 * @param bcastPtr The bcast structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a sMsg might be not updated.
 */
bool __unpackReqFileBcast(Slurm_Msg_t *sMsg, BCast_t **bcastPtr,
			  const char *caller, const int line);

#define unpackReqFileBcast(sMsg, bcastPtr) \
    __unpackReqFileBcast(sMsg, bcastPtr, __func__, __LINE__)

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
bool __packSlurmMsg(PS_DataBuffer_t *data, Slurm_Msg_Header_t *head,
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
bool __packRespDaemonStatus(PS_DataBuffer_t *data, Resp_Daemon_Status_t *stat,
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
bool __packRespLaunchTasks(PS_DataBuffer_t *data, Resp_Launch_Tasks_t *ltasks,
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
bool __packEnergyData(PS_DataBuffer_t *data, const char *caller,
		      const int line);

#define packEnergyData(data) __packEnergyData(data, __func__, __LINE__)

#endif  /* __PS_SLURM_PACK */
