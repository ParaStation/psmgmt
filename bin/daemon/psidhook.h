/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Handling of hooks within the ParaStation daemon.
 */
#ifndef __PSIDHOOK_H
#define __PSIDHOOK_H

#include <stdbool.h>

/** Return type expected from functions registered to PSIDHOOK_FRWRD_CLNT_RLS */
typedef enum {
    CONNECTED = 0,  /**< Client still connected to plugin, don't release */
    RELEASED,       /**< Client was released by plugin */
    IDLE,           /**< Client ignored the plugin (so far) */
} PSIDhook_ClntRls_t;

/**
 * @brief Hook function to execute.
 *
 * Function to be executed via the hook it will be registered
 * to. Registration might be done via @ref PSIDhook_add(). The pointer
 * argument might be used to pass extra information to this
 * function. This additional information has to be provided via @ref
 * PSIDhook_call(). Thus, it depends on the hook-type if such
 * additional information is available and what type is passed.
 *
 * @return @ref PSIDhook_call() will return the minimum of all the
 * returns provided by the different functions registered to this
 * hook.
 */
typedef int PSIDhook_func_t(void *);

/**
 * The hook-types currently known by the daemon. Some of them pass a
 * pointer to an argument that might be used by the function
 */
typedef enum {
    PSIDHOOK_NODE_UP,         /**< Node appeared, arg is PSnodes_ID_t ID */
    PSIDHOOK_NODE_DOWN,       /**< Node disappeared, arg is PSnodes_ID_t ID */
    PSIDHOOK_CREATEPART,      /**< Handle a partition request, arg is
				pointer to DDBufferMsg_t holding the
				original message received from the
				client. If return code is 0, a message
				was sent from within the hook and
				nothing else is done. Otherwise
				further measure might be taken
				afterwards. */
    PSIDHOOK_CREATEPARTNL,      /**< Handle a partition nodelist request,
				arg is pointer to DDBufferMsg_t holding
				the original message received from the
				client. If return code is 0, a message
				was sent from within the hook and
				nothing else is done. Otherwise
				further measure might be taken
				afterwards. */
    PSIDHOOK_GETRESERVATION,  /**< Handle a get reservation request, arg is
				pointer to PSrsrvtn_t holding the reservation
				request (and will hold the reservation on
				return). If 0 is returned, a validly filled
				reservation is assumed. On error return 1; this
				will suppress creating a reservation. Any other
				return value will trigger to fall back to
				standard creation of reservations. */
    PSIDHOOK_SHUTDOWN,        /**< Daemon got signaled to shutdown, no arg */
    PSIDHOOK_MASTER_GETPART,  /**< Master is creating a new partition, the arg
				is a pointer to PSpart_request_t holding the new
				created request. */
    PSIDHOOK_MASTER_FINJOB,   /**< A job has finished and the master will
				free the corresponding request. The arg
				is a pointer to PSpart_request_t holding
				the request to free. */
    PSIDHOOK_MASTER_RECPART,  /**< Recovery reserved ports on the new master
				from existing partitions. The arg is a pointer
				to PSpart_request_t holding the corresponding
				request. */
    PSIDHOOK_MASTER_EXITPART, /**< The local node is discharged from the
				burden of acting as a master, so all relevant
				resources should be freed. No argument. */
    PSIDHOOK_LOCALJOBCREATED, /**< After creating a new local job triggered by
				receiving a message of type PSP_DD_RESCREATED,
				thus informing us about being involved in a new
				reservation.
				The argument is the job already containing the
				new reservation information.
				The return value of the hook is ignored. */
    PSIDHOOK_LOCALJOBREMOVED, /**< Right before a local job gets removed due
				to its last reservation is removed triggered by
				receiving a message of type PSP_DD_RESRELEASED.
				The argument is the job with no reservation
				information left.
				The return value of the hook is ignored. */
    PSIDHOOK_RECV_SPAWNREQ,    /**< After receiving a message of type
				PSP_CD_SPAWNREQ, thus requests us to actually
				spawn some processes.
				The argument is the task structure prototype
				for the processes to be spawned only containing
				information shared between all of them.
				If the hook's return value is < 0, the spawn
				is canceled and a fail response is sent. */
    PSIDHOOK_EXEC_FORWARDER,  /**< Right before forking the forwarder's child.
				Arg is a pointer to the child's task structure.
				The hook might be used to prepare the child's
				and forwarder's environment. Return -1 if
				preparation failed and the child will
				be terminated. */
    PSIDHOOK_EXEC_CLIENT,     /**< Right before exec()ing the child, arg is a
				pointer to the child's task structure. This
				hook might be used to prepare the child's env */
    PSIDHOOK_FRWRD_INIT,      /**< In forwarder's init() function, arg is a
				pointer to the child's task structure. Might be
				used to register additional sockets and message
				types. */
    PSIDHOOK_FRWRD_CINFO,     /**< When the forwarder is connecting to
				the logger, arg is a pointer to the loggers
				response msg buffer. Might be used to get info
				about pre/succ ranks (obsolete) */
    PSIDHOOK_FRWRD_KVS,       /**< Handle a KVS/Service messages, arg
				points to msg (obsolete) */
    PSIDHOOK_FRWRD_EXIT,      /**< Tell attached plugins that the forwarder
				is going to exit. arg might be a pointer to the
				task ID of the child process but is NULL most
				of the time (in the meantime always). */
    PSIDHOOK_FRWRD_CLNT_RLS,  /**< Ask attached plugins if the client is
				released. The client is described by the task
				structure passed in arg. The plugin is expected
				to return a value of type PSIDhook_ClntRls_t.
				If IDLE or RELEASED is returned, a message of
				type PSP_CD_RELEASE will be sent according to
				some heuristics. */
    PSIDHOOK_FRWRD_SPAWNRES,  /**< A result msg to a spawn request. Arg is a
				pointer to the msg. Used by pspmi to handle the
				result of spawning new service processes.
				(obsolete) */
    PSIDHOOK_FRWRD_CC_ERROR,  /**< The forwarder recv a CC_ERROR msg. Arg is a
				pointer to that message. A plugin may need to
				handle this error if the corresponding msg was
				generated by it. (obsoete) */
    PSIDHOOK_EXEC_CLIENT_USER,/**< Right before exec()ing the child, arg is a
				pointer to the child's task structure. This
				hook might be used to prepare the child's env */
    PSIDHOOK_XTND_PART_DYNAMIC,/** Dynamically extend a partition. The handler
				of this hook is expected to call the function
				PSIDpart_extendRes() either in a synchronous or
				asynchronous way. Arg is a pointer of type
				PSrsrvtn_t to the reservation created so far.
				I.e. the hook might rely on the content of nMin,
				nMax, tpp, hwType, nSlots, task and rid. nMin
				and nMax are not corrected, i.e. we are actually
				requesting between nMin-nSlots and nMax-nSlots
				slots of type hwType with tpp threads each.*/
    PSIDHOOK_RELS_PART_DYNAMIC,/** Release dynamically extended  resources.
				Arg is a pointer of type PSrsrvtn_dynRes_t which
				contains the reservation id and the actual slot
				to be released. */
    PSIDHOOK_PELOGUE_START,   /** Right before exec()ing the child to start a
				prologue/epilogue script. Used by batch-system
				plugins to get information about allocations.
				Arg is pointer to PElogueChild_t */
    PSIDHOOK_PELOGUE_PREPARE, /** Prepare argument vector and environment of a
				prologue/epilogue script. Used by batch-system
				plugins in order to provide the script the
				expected arguments and environment. Arg is
				pointer to PElogueChild_t */
    PSIDHOOK_PELOGUE_FINISH,  /** The result of a prologue/epilogue run
				executed by the pelogue plugin can be inspected.
				Used by the psslurm plugin. Arg is pointer to
				PElogueChild_t */
    PSIDHOOK_PELOGUE_RES,     /**< Hook for requesting additional resources, arg
				is a pointer to PElogueResource_t. Used by the
				psgw plugin */
    PSIDHOOK_PELOGUE_OE,      /**< The stdout/stderr messages of the prologue
				and epilogue script are provided. Used by
				psslurm to collect job errors. Arg is pointer to
				PElogue_OEdata_t */
    PSIDHOOK_PELOGUE_GLOBAL,  /**< The result of a global prologue/epilogue run
				executed by the pelogue plugin can be inspected.
				Used by the psslurm plugin. Arg is pointer to
				PElogue_Global_Res_t */
    PSIDHOOK_PELOGUE_DROP,    /**< A pelogue message got dropped. This
				may happen if pspelogue gets killed before the
				prologue is complete. Used by psslurm to cleanup
				allocation information. Arg is pointer to
				message which got dropped. */
    PSIDHOOK_FRWRD_DSOCK,     /**< In forwarder's init() function, arg is a
				pointer to the daemon socket. (obsolete!) */
    PSIDHOOK_JAIL_CHILD,      /**< Jail child into cgroup, arg points to PID.
				Return -1 if jailing failed and the child
				will be terminated. */
    PSIDHOOK_CLEARMEM,        /**< Release memory after forking before handling
				other tasks, e.g. becoming a forwarder.
				arg points to aggressive flag of type bool */
    PSIDHOOK_RANDOM_DROP,     /**< Determine if a message shall be dropped by
				sendMsg(), arg points to the message to be
				inspected. If the message shall	be dropped,
				return 0, otherwise return 1 */
    PSIDHOOK_PSSLURM_FINALLOC,/**< An allocation has finished and will be
				deleted by psslurm. The arg is a pointer to
				Alloc_t holding the allocation to free. Used by
				the psgw plugin. */
    PSIDHOOK_FRWRD_CLNT_RES,  /**< Tell attached plugins about client's exit
				status in the arg. */
    PSIDHOOK_PSSLURM_JOB_FWINIT,/**< In psslurm job forwarder's init()
				function. Arg is job owner's username.
				Called by pamservice plugin */
    PSIDHOOK_PSSLURM_JOB_FWFIN,/**< In psslurm job forwarder's finalize()
				function. Arg is job owner's username.
				Called by pamservice plugin */
    PSIDHOOK_PSSLURM_JOB_EXEC,/**< In the psslurm job forwarder as root before
				switching to job owner and executing the
				jobscript. Arg is job owner's username.
				Called by pamservice plugin */
    PSIDHOOK_DIST_INFO,       /**< Called in msg_SETOPTION when
				information is updated. Shall trigger
				distribution of option updates. Arg is
				type of info to be updated. */
    PSIDHOOK_JAIL_TERM,	      /**< Terminate all jailed children of a cgroup,
				arg points to PID identifying the cgroup */
    PSIDHOOK_EXEC_CLIENT_PREP,/**< Before testing child's executable, arg is
				a pointer to the child's task structure.
				Utilized by Spank in psslurm to change the
				child's namespace */
    PSIDHOOK_LAST,            /**< This has to be the last one */
} PSIDhook_t;

/**
 * @brief Add hook
 *
 * Add the function @a func to the set of functions be called when the
 * hook @a hook is reached.
 *
 * @param hook The hook the function shall be registered to.
 *
 * @param func The function to register to the hook.
 *
 * @return On success, true is returned, or false if an error
 * occurred. The latter might happen if a function shall be added to an
 * obsolete hook.
 */
bool PSIDhook_add(PSIDhook_t hook, PSIDhook_func_t func);

/**
 * @brief Remove hook
 *
 * Remove the function @a func from the set of functions to be called
 * when the hook @a hook is reached.
 *
 * @param hook The hook the function shall be registered to.
 *
 * @param func The function to register to the hook.
 *
 * @return On success, true is returned. Or false if an error occurred,
 * i.e. the hook to unregister was not found.
 */
bool PSIDhook_del(PSIDhook_t hook, PSIDhook_func_t func);

/** Magic value to find out, if any hook was called by @ref PSIDhook_call() */
#define PSIDHOOK_NOFUNC 42

/**
 * Execute functions
 *
 * Execute all functions registered to the hook @a hook. Each function
 * is executed and the return-value is inspected. This function will
 * return the minimum over all return values seen during the call. The
 * first call of a hook function is compared against @ref
 * PSIDHOOK_NOFUNC.
 *
 * The functions will be called in the same order as they were
 * registered to the hook.
 *
 * A possible strategy to use return values is as follows:
 *
 * - As long as the function registered to the hook was executed
 *   successfully, give some return value greater or equal to 0 and
 *   smaller than @ref PSIDHOOK_NOFUNC
 *
 * - If an error occurred, use a return value smaller than 0
 *
 * With that strategy it's possible to detect both, if a hook was
 * executed at all (this is not the case, if @ref PSIDHOOK_NOFUNC is
 * returned) and if at least one error occurred (i.e. some value
 * smaller than 0 is returned).
 *
 * @param hook The hook reached and to be handled.
 *
 * @param arg Pointer to additional information to be passed to the hooks.
 *
 * @return The minimum of all return-values of the called functions
 * registered to the hook is returned.
 */
int PSIDhook_call(PSIDhook_t hook, void *arg);

/**
 * @brief Init hooks
 *
 * Initialize the hook framework. This allocates the structures used
 * to manage the functions registered to the various hooks available.
 *
 * The hook framework is also initialized implicitly on the first
 * call of PSIDhook_add(). Thus, calling this function explicitly is
 * not required.
 *
 * @deprecated Not required any longer.
 *
 * @return No return value.
 */
void initHooks(void) __attribute__ ((deprecated));

#endif  /* __PSIDHOOK_H */
