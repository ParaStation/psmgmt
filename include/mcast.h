/*
 *               ParaStation3
 * mcast.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mcast.h,v 1.3 2002/01/30 18:25:57 eicker Exp $
 *
 */
/**
 * \file
 * ParaStation MultiCast facility
 *
 * $Id: mcast.h,v 1.3 2002/01/30 18:25:57 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __MCAST_H
#define __MCAST_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Create docu */
typedef enum {
    DOWN = 0x1,
    UP   = 0x2
} MCastState;

/** @todo Create docu */
typedef struct {
    double load[3];           /**< The actual load parameters */
} MCastLoad;

/** @todo Create docu */
typedef struct {
    MCastState state;
    MCastLoad  load;
    int        misscounter;
} MCastConInfo;

/** @todo Create docu */
typedef struct {
    short    node;            /**< Sender ID */
    short    type;            /**< Message type */
    MCastState state;         /**< @todo */
    MCastLoad  load;          /**< @todo */
} MCastMsg;

#define MCAST_NEW_CONNECTION    0x80   /* buf == nodeno */
#define MCAST_LOST_CONNECTION   0x81   /* buf == nodeno */

#define MCAST_LIC_LOST          0x88   /* buf == sinaddr */ 
#define MCAST_LIC_SHUTDOWN      0x89   /* buf == reason */ 

#define LIC_LOST_CONECTION      0x1
#define LIC_KILL_MSG            0x2


#define MCASTSERVICE "psmcast"   /**< The symbolic name of MCast-service */

/**
 * The default MCast-group number. Magic number defined by Joe long time ago.
 * Can be overruled via initMCast().
 */
static int DEFAULT_MCAST_GROUP = 237;

/**
 * @brief Initialize the MCast module.
 *
 * Initializes the MCast machinery for @a nodes nodes.
 *
 * @param nodes Number of nodes to handle.
 * @param mgroup The MCast group to use.
 * @param usesyslog If true, all error-messages are printed via syslog().
 * @param hosts An array of size @a nodes containing the IP-addresses of the
 * participating nodes in network-byteorder.
 * @param licServer Flag to mark the calling process as a license server.
 * @param callback Pointer to a callback-function. This function is called if
 * something exceptional happens. If NULL, no callbacks will be done.
 *
 * @return On success, the filedescriptor of the MCast socket is returned.
 * On error, exit() is called within this function.
 */
int initMCast(int nodes, int mgroup, int usesyslog,  unsigned int hosts[],
	      int licServer, void (*callback)(int, void*));

/**
 * @brief Shutdown the MCast module.
 *
 * Shutdown the whole MCast machinery.
 *
 * @return No return value.
 */
void exitMCast(void);

/**
 * @brief Query the debug-level.
 *
 * Get the debug-level of the MCast module.
 *
 * @return The actual debug-level is returned.
 *
 * @see setDebugLevelMCast()
 */
int getDebugLevelMCast(void);

/**
 * @brief Set the debug-level.
 *
 * Set the debug-level of the MCast module. Posible values are:
 *  - 0: Critical errors (usually exit)
 *  - 1: .... @todo More levels to add.
 *
 * @param level The debug-level to set
 *
 * @return No return value.
 *
 * @see getDebugLevelMCast()
 */
void setDebugLevelMCast(int level);

/**
 * @brief Get MCast deadlimit
 *
 * Get the deadlimit of the MCast module. After @a deadlimit consecutively
 * missing MCast pings a node is declared to be dead.
 *
 * @return The actual deadlimit is returned.
 *
 * @see setDeadLimitMCast()
 */
int getDeadLimitMCast(void);

/**
 * @brief Set MCast deadlimit
 *
 * Set the deadlimit of the MCast module. After @a deadlimit consecutively
 * missing MCast pings a node is declared to be dead.
 *
 * @param limit The deadlimit to be set.
 *
 * @return No return value.
 *
 * @see getDeadLimitMCast()
 */
void setDeadLimitMCast(int limit);

/**
 * @brief Get connection info.
 *
 * Get connection information from the MCast module concerning the node
 * @a node. The result is returned in a @ref MCastConInfo structure.
 *
 * @param node The node, to get MCast connection information about.
 * @param info The @ref MCastConInfo structure holding the connection info
 * on return.
 *
 * @return No return value.
 */
void getInfoMCast(int node, MCastConInfo *info);

/**
 * @brief Get status info.
 *
 * Get status information from the MCast module concerning the node @a node.
 * The result is returned in @a string and can be directly
 * put out via printf() and friends.
 *
 * @param node The node, to get MCast status information about.
 * @param string The string to which the status information is written.
 * @param len The length of @a string.
 *
 * @return No return value.
 *
 * @see printf(3)
 */
void getStateInfoMCast(int n, char *s, size_t len);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __MCAST_H */
