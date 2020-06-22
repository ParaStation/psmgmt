/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_FORWARDER
#define __PLUGIN_LIB_FORWARDER

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "pstaskid.h"
#include "pslog.h"

#define PLUGINFW_PROTO_VERSION 2

/* forward declaration */
typedef struct __fwData__ Forwarder_Data_t;

/** Structure defining all parameter's of a forwarder */
typedef struct __fwData__ {
    char *pTitle;          /**< Process title to use */
    char *jobID;           /**< Job ID if any */
    void *userData;        /**< Additional information to be attached */
    int8_t childRerun;     /**< How many times @ref childFunc() might be run */
    int32_t timeoutChild;  /**< Maximum runtime of @ref childFunc() */
    int32_t graceTime;     /**< Grace time before sending SIGKILL */
    bool accounted;        /**< Flag to get accounting data on forwarder.
			    * Defaults to false. Setting this flag true
			    * requires a loaded psaccount plugin and to
			    * register this forwarder's child via
			    * psAccountRegisterJob().
			    * Not doing so might confuse accounters. */
    PStask_ID_t tid;       /**< Forwarder's task ID */
    pid_t cPid;            /**< PID of forwarder's child if any */
    pid_t cSid;            /**< Session ID of forwarder's child if any */
    bool exitRcvd;         /**< Flag estatus as valid */
    int32_t estatus;       /**< Child's exit status (only available in cb) */
    bool codeRcvd;         /**< Flag ecode as valid */
    int32_t ecode;         /**< Child's hook exit code (only available in cb) */
    int stdIn[2];          /**< stdIn provided to forwarder's child */
    int stdOut[2];         /**< stdOut provided to forwarder's child */
    int stdErr[2];         /**< stdErr provided to forwarder's child */
    bool hideFWctrlMsg;	   /**< hide internal forwarder control messages
			    * from handleMthrMsg() hook. Defaults to true. */
    bool fwChildOE;	   /**< Automatically forward STDOUT/STDERR from the
			    * child to the mother. The caller is responsible to
			    * handle the STDOUT/STDERR messages in the function
			    * handleFwMsg() */
    int (*killSession)(pid_t, int);
			   /**< Method to kill forwarder and all its children */
    int (*callback)(int32_t, Forwarder_Data_t *);
			   /**< Callback invoked upon forwarder's termination */
    void (*childFunc)(Forwarder_Data_t *, int);
			   /**< Child function forked by forwarder */
    int (*hookFWInit)(Forwarder_Data_t *);
			   /**< Called within forwarder upon initialization */
    void (*hookLoop)(Forwarder_Data_t *);
			   /**< Called within forwarder before entering loop */
    void (*hookChild)(Forwarder_Data_t *, pid_t, pid_t, pid_t);
			   /**< Called within mother when child is ready */
    void (*hookFinalize)(Forwarder_Data_t *);
			   /**< Called within forwarder upon finalization */
    int (*handleMthrMsg)(PSLog_Msg_t *, Forwarder_Data_t *);
			   /**< Additional mother-msgs handled by forwarder */
    int (*handleFwMsg)(PSLog_Msg_t *, Forwarder_Data_t *);
			   /**< Additional forwarder-msgs handled by mother */
} ForwarderData_t;

/* ------------- Functions to be executed in forwarder ------------------ */

/**
 * @brief Send message to mother
 *
 * Send the message @a msg to the mother process, i.e. the local
 * daemon process that has spawned the forwarder.
 *
 * @param msg Message to be sent
 *
 * @return The number of bytes sent or -1 on error
 */
int sendMsgToMother(PSLog_Msg_t *msg);

/* --------------- Functions to be executed in mother ------------------- */

/**
 * @brief Create new forwarder structure
 *
 * Allocate and initialize a new forwarder structure
 *
 * @return Return an initialized forwarder structure or NULL on error
 */
Forwarder_Data_t *ForwarderData_new(void);

/**
 * @brief Delete forwarder structure
 *
 * Delete forwarder structure @a fw. This includes to free all
 * allocate memory.
 *
 * @param fw Forwarder structure to be deleted
 *
 * @return No return value
 */
void ForwarderData_delete(Forwarder_Data_t *fw);

/**
 * @brief Start forwarder
 *
 * Start a forwarder described by the structure @a fw.
 * To reduce the forwarders memory footprint @ref PSID_clearMem()
 * is called. Therefore certain facilities e.g. psserial will
 * require to be initialized again before they can be used.
 *
 * @param fw Structure to describe the forwarder to start
 *
 * @return On success true is returned or false on error
 */
bool startForwarder(Forwarder_Data_t *fw);

/**
 * @brief Tell forwarder to shutdown
 *
 * The forwarder described by the structure @a fw to shutdown. For
 * this first SIGTERM is sent to the child process if any. After
 * providing some grace-time SIGKILL is sent to the child process and
 * all its children. Once the child process is gone, the forwarder's
 * loop is terminated.
 *
 * The actual grace-time might be given in @a fw's @ref graceTime
 * attribute or by the modules DEFAULT_GRACE_TIME.
 *
 * @param fw Forwarder to shutdown
 *
 * @return No return value
 */
void shutdownForwarder(Forwarder_Data_t *fw);

/**
 * @brief Signal forwarder's child
 *
 * Send the signal @a signal to the child of the forwarder described
 * by the structure @a fw. To send the signal either the forwarder's
 * @ref killSession() method is used from within the calling process
 * if the child's session ID is known. Otherwise the forwarder process
 * will be contacted in order to deliver the signal. In both cases the
 * forwarder process will be informed in order to prepare for its own
 * finalization. If both methods are not available, false will be
 * returned.
 *
 * @param fw Forwarder to trigger
 *
 * @param signal Signal to send
 *
 * @return Return true if the signal can be delivered. Otherwise false
 * is returned.
 */
bool signalForwarderChild(Forwarder_Data_t *fw, int signal);

/**
 * @brief Tell forwarder to start grace time
 *
 * Tell the forwarder described by the structure @a fw to start its
 * grace-time, i.e. to wait for the defined amount of time before
 * killing the child process and all its children via the forwarder's
 * @ref killSession() method.
 *
 * The actual grace-time might be given in @a fw's @ref graceTime
 * attribute or by the modules DEFAULT_GRACE_TIME.
 *
 * @param fw Forwarder to trigger
 *
 * @return No return value
 */
void startGraceTime(Forwarder_Data_t *fw);

#endif  /* __PLUGIN_LIB_FORWARDER */
