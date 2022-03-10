/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
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
#include "psprotocol.h"

/* forward declaration */
struct __fwData__;    // Make IWYU happy
typedef struct __fwData__ Forwarder_Data_t;

#define FW_CHILD_INFINITE -1

/** Structure defining all parameter's of a forwarder */
typedef struct __fwData__ {
    char *pTitle;          /**< Process title to use */
    char *jobID;           /**< Job ID if any */
    void *userData;        /**< Additional information to be attached */
    char *userName;        /**< username used to run the forwarder */
    uid_t uID;             /**< user ID used to run the forwarder */
    gid_t gID;             /**< group ID used to run the forwarder */
    int8_t childRerun;     /**< How many times @ref childFunc() might be run.
			    * Default is 1. Use FW_CHILD_INFINITE to restart the
			    * child endlessly until the forwarder is stopped or
			    * a predefined time limit is reached. */
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
    int32_t fwExitStatus;  /**< Forwarder's exit status (only available in cb)*/
    bool exitRcvd;         /**< Flag chldExitStatus as valid */
    int32_t chldExitStatus;/**< Child's exit status (only available in cb) */
    bool codeRcvd;         /**< Flag hookExitCode as valid */
    int32_t hookExitCode;  /**< Child's hook exit code (only available in cb) */
    int stdIn[2];          /**< stdIn provided to forwarder's child */
    int stdOut[2];         /**< stdOut provided to forwarder's child */
    int stdErr[2];         /**< stdErr provided to forwarder's child */
    bool hideFWctrlMsg;	   /**< hide internal forwarder control messages
			    * from handleMthrMsg() hook. Defaults to true. */
    bool hideCCError;	   /**< hide PSP_CC_ERROR messages from handleMthrMsg()
			    * hook. Defaults to true. */
    bool fwChildOE;	   /**< Automatically forward stdout/stderr from the
			    * child to the mother. The caller is responsible to
			    * handle the PLGN_STDOUT/PLGN_STDERR messages in the
			    * function handleFwMsg() */
    bool jailChild;	   /**< jail myself and all my children by
			    * calling the hook PSIDHOOK_JAIL_CHILD */
    int (*killSession)(pid_t, int);
			   /**< Method to kill all forwarder's children */
    void (*callback)(int32_t, Forwarder_Data_t *);
			   /**< Callback invoked upon forwarder's termination */
    void (*childFunc)(Forwarder_Data_t *, int);
			   /**< Child function forked by forwarder */
    int (*hookFWInit)(Forwarder_Data_t *);
			   /**< Called within forwarder upon initialization
			    * with root privileges */
    int (*hookFWInitUser)(Forwarder_Data_t *);
			   /**< Only called if forwarder is run as user. Called
			    * after jail hook and user context switch */
    void (*hookLoop)(Forwarder_Data_t *);
			   /**< Called within forwarder before entering loop */
    void (*hookChild)(Forwarder_Data_t *, pid_t, pid_t, pid_t);
			   /**< Called within mother when child is ready */
    void (*hookFinalize)(Forwarder_Data_t *);
			   /**< Called within forwarder upon finalization */
    bool (*handleMthrMsg)(DDTypedBufferMsg_t *, Forwarder_Data_t *);
			   /**< Additional mother-msgs handled by forwarder */
    bool (*handleFwMsg)(DDTypedBufferMsg_t *, Forwarder_Data_t *);
			   /**< Additional forwarder-msgs handled by mother */
} ForwarderData_t;

/** Generic message (sub-)types exchanged between mother and forwarder */
typedef enum {
    PLGN_CHILD,          /**< fw -> plgn Child is ready */
    PLGN_SIGNAL_CHLD,    /**< plgn -> fw Signal child */
    PLGN_STDOUT,         /**< fw -> plgn Child's stdout */
    PLGN_STDERR,         /**< fw -> plgn Child's stderr */
    PLGN_START_GRACE,    /**< plgn -> fw Start child's grace period */
    PLGN_SHUTDOWN,       /**< plgn -> fw Shutdown child */
    PLGN_ACCOUNT,        /**< fw -> plgn Resources used by child */
    PLGN_EXITCODE,       /**< fw -> plgn Child hook exit code */
    PLGN_EXITSTATUS,     /**< fw -> plgn Child exit status */
    PLGN_FIN,            /**< fw -> plgn Forwarder going to finalize */
    PLGN_FIN_ACK,        /**< plgn -> fw ACK finalization */
    PLGN_TYPE_LAST = 32, /**< all numbers beyond this might be used privately,
			 e.g. between plugins and their own forwarders */
} ForwarderMsg_t;

/**
 * @page plgnfw_comm_strategy Communication between psid and
 * pluginforwarder
 *
 * The UNIX socket connecting a pluginforwarder and its daemon runs
 * the standard ParaStation protocol (PSP) utilizing the full range of
 * messages including serialized messages. For this, accordingly
 * addressed messages might be sent to the pluginforwarder using the
 * @ref sendMsg() or @ref PSIDclient_send() functions or created via
 * the fragmentation layer of psserial. In order to send messages from
 * the plugin forwarder to the local daemon @ref sendMsgToMother()
 * shall be used either directly or indirectly via psserial's
 * fragmentation layer.
 *
 * Since this connection will be utilized to send and receive messages
 * necessary for the management of the pluginforwarder and its client,
 * too, special care has to be taken when receiving messages on either
 * side. The default behavior is that the mentioned management
 * messages will be handled internally and no additional messages are
 * expected to be received on either side of the connection.
 *
 * In order to allow plugins to received additional messages from its
 * forwarder a @ref handleFwMsg() function might be provided in the
 * forwarder's definition. This function will be called upon receiving
 * a message from the corresponding forwarder and at least one of the
 * following criteria is fulfilled:
 *
 * 1. The type of the message marked in its header is different from
 * @ref PSP_PF_MSG
 *
 * 2. The header's message type is @ref PSP_PF_MSG and its sub-type
 * (as in a @ref DDTypedMsg_t message or in a @ref DDTypedBufferMsg_t
 * message) is beyond PLGN_TYPE_LAST
 *
 * 3. The forwarder's @ref hideFWctrlMsg flag is cleared in its
 * definition (the default is that this flag set!)
 *
 * @ref handleFwMsg() will be called with a pointer to the message
 * received as the first argument and a pointer to the forwarder's
 * definition as its second argument. By its return value @ref
 * handleFwMsg() will flag if the message was handled and no further
 * measures have to be taken by the caller (true) or if the caller
 * shall continue the handling of the message (false). Therefore, with
 * the @ref hideFWctrlMsg flag cleared @ref handleFwMsg() might peek
 * into each messages received from the forwarder and suppress further
 * handling even of management messages by marking the message as
 * handled (by returning true).
 *
 * Beyond this, if the @ref fwChildOE flag in the forwarder's
 * definition is set, management messages of type @ref PLGN_STDOUT and
 * @ref PLGN_STDERR are created by the forwarder presenting the stdout
 * and stderr output of its client process, respectively, and passed
 * to @ref handleFwMsg(), too. These messages will be dropped
 * independent of @ref handleFwMsg()'s return value afterwards.
 *
 * By providing a @ref handleMthrMsg() function in its definition the
 * forwarder itself will be enabled to receive and handle additional
 * messages. Again, messages are presented to this function if one of
 * the following criteria are met:
 *
 * 1. The type of the message marked in its header is different from
 * @ref PSP_PF_MSG
 *
 * 2. The header's message type is @ref PSP_PF_MSG and its sub-type
 * (as in a @ref DDTypedMsg_t message or in a @ref DDTypedBufferMsg_t
 * message) is beyond PLGN_TYPE_LAST
 *
 * 3. The forwarder's @ref hideFWctrlMsg flag is cleared in its
 * definition (the default is that this flag set!)
 *
 * @ref handleMthrMsg() will be called with a pointer to the message
 * received as the first argument and a pointer to the forwarder's
 * definition as its second argument. By its return value @ref
 * handleMthrMsg() will flag if the message was handled and no further
 * measures have to be taken by the caller (true) or if the caller
 * shall continue the handling of the message (false). Therefore, with
 * the @ref hideFWctrlMsg flag cleared @ref handleMthrMsg() might peek
 * into each messages received by the forwarder from the local daemon
 * and suppress further handling even of management messages by
 * marking the message as handled (by returning true).
 *
 * It has to be kept in mind that each message (except management
 * messages, i.e. messages of type PSP_PF_MSG and sub-types mentioned
 * in @ref ForwarderMsg_t) have to be handled explicitly in @ref
 * handleFwMsg() / @ref handleMthrMsg(). No automatic message
 * forwarding (e.g. to remote daemons or other local clients) will be
 * done automatically. Nevertheless, the handling of such type of
 * messages is sufficed by just forwarding (i.e. sending) them.
 *
 * The plugin and its forwarder might use message of type PSP_PF_MSG
 * with sub-types (as in a @ref DDTypedMsg_t message or in a @ref
 * DDTypedBufferMsg_t message) beyond PLGN_TYPE_LAST for their own
 * purposes, i.e. to implement their own extended protocol.
 */

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
ssize_t sendMsgToMother(DDTypedBufferMsg_t *msg);

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
