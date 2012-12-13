/*
 * ParaStation
 *
 * Copyright (C) 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Handling of hooks within the ParaStation daemon.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDHOOK_H
#define __PSIDHOOK_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif


#ifdef __cplusplus
}/* extern "C" */
#endif

/**
 * @brief Hook function to execute.
 *
 * Function to be executed via the hook it will be registered
 * to. Registration might be done via @ref PSIDhook_add(). The pointer
 * argument might be used to pass extra information to this
 * function. This additional information has to be provided via @ref
 * PSIDhook_call(). Thus, it depends on the hook-type and the use of,
 * if such additional information is available and what type is
 * passed.
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
    /*
     * The following hooks are place-holders for future extension and
     * not yet called by the daemon.
     */
    PSIDHOOK_EXEC_FORWARDER,   /**< Right before starting forwarder. Might
				  be used to prepare the forwarder's env */
    PSIDHOOK_EXEC_CLIENT,      /**< Right before exec()ing the child. Might
				  be used to prepare the child's env */
    PSIDHOOK_FRWRD_INIT,       /**< In forwarder's init() function */
    PSIDHOOK_FRWRD_CINFO,      /**< ??? executed to get info about pre/succ
				  ranks */
    PSIDHOOK_FRWRD_KVS,        /**< Handle KVS messages. arg points to msg */
    PSIDHOOK_FRWRD_RESCLIENT,  /**< executed at forwarder's child release */

    PSIDHOOK_FRWRD_CLIENT_STAT,/**< ??? ask all plugins if we they release the child */

    PSIDHOOK_LAST,             /**< This has to be the last one */
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
 * @return On success, 1 is returned. Or 0, if an error occurred.
 */
int PSIDhook_add(PSIDhook_t hook, PSIDhook_func_t func);

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
 * @return On success, 1 is returned. Or 0, if an error occurred,
 * i.e. the hook to unregister was not found.
 */
int PSIDhook_del(PSIDhook_t hook, PSIDhook_func_t func);

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
 * returned) and if at least one error occured (i.e. some value
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
 * @return No return value.
 */
void initHooks(void);

#endif  /* __PSIDHOOK_H */
