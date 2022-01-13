/*
 * ParaStation
 *
 * Copyright (C) 2008-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper functions for daemon status handling (reset, shutdown,
 * master-socket,etc.)
 */
#ifndef __PSIDSTATE_H
#define __PSIDSTATE_H

/**
 * @brief Initialize daemon status stuff
 *
 * Initialize the daemon status framework. This includes setting up
 * the master socket and registration of some message handler.
 *
 * @return No return value.
 */
void initState(void);

/**
 * The different internal states of the daemon. The current state
 * might be requested via the @ref PSID_getDaemonState() functions.
 *
 * @see PSID_getDaemonState()
 */
typedef enum {
    PSID_STATE_NONE      = 0x0000, /**< normal operation */
    PSID_STATE_RESET     = 0x0001, /**< daemon reset in progress */
    PSID_STATE_RESET_HW  = 0x0002, /**< also reset communication hardware */
    PSID_STATE_SHUTDOWN  = 0x0004, /**< daemon shutdown in progress */
    PSID_STATE_NOCONNECT = (PSID_STATE_RESET | PSID_STATE_SHUTDOWN),
				   /**< do not allow to connect during
				    * shutdown or reset */
} PSID_DaemonState_t;

/**
 * @brief Get daemon state
 *
 * Get the daemon's internal state. This informs if currently a reset
 * or shutdown of the daemon is under way.
 *
 * @return The daemon's internal state is returned.
 */
PSID_DaemonState_t PSID_getDaemonState(void);

/**
 * @brief Shutdown local node.
 *
 * Shutdown the local node, i.e. stop daemon's operation. This will
 * happen in different phases.
 *
 * Once this function was called for the first time,
 * PSID_STATE_SHUTDOWN is added to the daemon's internal state. This
 * state might be requested from the outside via @ref
 * PSID_getDaemonState().
 *
 * The first call will trigger repeated calls to this function from
 * the main loop. A timer inside this functions assures that each
 * phase lasts at least one second.
 *
 * The different phases used to shutdown the local daemon are
 * organized as follows:
 *
 *  phase 0:
 *     - switch to PSID_STATE_SHUTDOWN
 *     - disable the master socket, i.e. don't except new connections
 *     - kill all client processes
 *
 *  phase 1:
 *     - kill clients again where killing wasn't successful, yet
 *
 *  phase 2:
 *     - hardly kill (SIGKILL) clients where killing wasn't successful, yet.
 *
 *  phase 3:
 *     - kill all remaining processes (forwarder, admin, etc.)
 *
 *  phase 4:
 *     - hardly kill (SIGKILL) all remaining processes (forwarder, admin, etc.)
 *
 *  phase 5:
 *     - finalize all plugins and trigger them to unload
 *
 *  phase 6:
 *     - check if all plugins are gone in the meantime; wait longer if not
 *     - stop all local hardware managed by the daemon
 *     - close connections to other nodes and shutdown RDP
 *     - shutdown daemon's master socket
 *     - say good bye and exit
 *
 * @return No return value
 */
void PSID_shutdown(void);

/**
 * @brief Reset local node.
 *
 * Reset the local node, i.e. kill all client processes. This will
 * happen in different phases. If PSID_STATE_RESET_HW is set within
 * the daemon's internal state, all local communication hardware
 * will be reset after all clients are gone. Resetting the
 * communication hardware is done via calling @ref PSID_stopAllHW()
 * and @ref PSID_startAllHW().
 *
 * Setting PSID_STATE_RESET_HW is only possible from within this
 * module. This is usually done via measures within the internal
 * msg_DAEMONRESET() function, which usually handles
 * PSP_CD_DAEMONRESET messages.
 *
 * Once this function was called for the first time, PSID_STATE_RESET
 * is added to the daemon's internal state. This state might be
 * requested from the outside via @ref PSID_getDaemonState(). If
 * PSID_STATE_RESET is set in the internal state, this has to trigger
 * repeated calls to this function from the main loop. A timer inside
 * this functions assures that each phase lasts at least one second.
 *
 * The different phases used to reset the local daemon are organized
 * as follows:
 *
 *  phase 0:
 *     - switch to PSID_STATE_RESET
 *     - kill all client processes
 *
 *  phase 1:
 *     - kill clients again where killing wasn't successful, yet
 *
 *  phase 2:
 *     - hardly kill (SIGKILL) clients where killing wasn't successful, yet.
 *
 *  phase 3:
 *     - kill all remaining processes (forwarder, admin, etc.)
 *
 *  phase 4:
 *     - hardly kill (SIGKILL) all remaining processes (forwarder, admin, etc.)
 *     - close connections to local clients
 *     - close connections to other nodes
 *     - exit
 *
 * @return No return value
 */
void PSID_reset(void);

#endif  /* __PSIDSTATE_H */
