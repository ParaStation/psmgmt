/*
 *               ParaStation3
 * mcast.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mcast.h,v 1.11 2002/07/05 14:40:24 eicker Exp $
 *
 */
/**
 * \file
 * ParaStation MultiCast facility
 *
 * $Id: mcast.h,v 1.11 2002/07/05 14:40:24 eicker Exp $
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

/** Possible MCast states of a node */
typedef enum {
    DOWN = 0x1,  /**< node is down */
    UP   = 0x2   /**< node is up */
} MCastState;

/** The load info of a node */
typedef struct {
    double load[3];           /**< The actual load parameters */
} MCastLoad;

/** The whole MCast info about a node */
typedef struct {
    MCastState state;    /**< The state info of the node @see MCastState */
    MCastLoad load;      /**< The load info of the node @see MCastLoad */
    int misscounter;     /**< The number of missing pings from this node */
} MCastConInfo;

/** Structure of a MCast message */
typedef struct {
#ifdef __osf__
    /* If we use the same mcastsocket for sending and receiving,
       tru64 uses the multicast address as source address in the IP
       packet -> The receiver cant detect the sender of the message,
       which is needed in handleMCast() for error checking. */
    struct in_addr ip;	 /**< Sender IP */
#endif
    short node;          /**< Sender ID */
    short type;          /**< Message type */
    MCastState state;    /**< The state info @see MCastState */
    MCastLoad load;      /**< The load info @see MCastLoad */
} MCastMsg;

/** Tag to @ref MCastCallback: New connection detected */
#define MCAST_NEW_CONNECTION  0x80
/** Tag to @ref MCastCallback: Connection lost */
#define MCAST_LOST_CONNECTION 0x81
/** Tag to @ref MCastCallback: Connection to license server lost */
#define MCAST_LIC_LOST        0x88
/**
 * Tag to @ref MCastCallback: Connection to license server lost for too long
 * or explicit shutdown.
 */
#define MCAST_LIC_SHUTDOWN    0x89

/**
 * Tag for @ref MCAST_LIC_SHUTDOWN message to @ref MCastCallback: Connection
 * to license server lost for too long.
 */
#define LIC_LOST_CONECTION    0x1
/**
 * Tag for @ref MCAST_LIC_SHUTDOWN message to @ref MCastCallback: Got explicit
 * shutdown from license server.
 */
#define LIC_KILL_MSG          0x2


/**
 * The default MCast-group number. Magic number defined by Joe long time ago.
 * Can be overruled via initMCast().
 */
#define DEFAULT_MCAST_GROUP 237

/**
 * The default MCast-port number. Magic number defined by Joe long time ago.
 * Can be overruled via initMCast().
 */
#define DEFAULT_MCAST_PORT 1889

/**
 * @brief Initialize the MCast module.
 *
 * Initializes the MCast machinery for @a nodes nodes.
 *
 *
 * @param nodes Number of nodes to handle (minus the node of the
 * license-daemon).
 *
 * @param mcastgroup The MCast group to use. If 0, @ref DEFAULT_MCAST_GROUP is
 * used.
 *
 * @param portno The UDP port number in host byteorder to use for sending and
 * receiving packets. If 0, @ref DEFAULT_MCAST_PORT is used.
 *
 * @param usesyslog If true, all error-messages are printed via syslog().
 *
 * @param hosts An array of size @a nodes+1 containing the
 * IP-addresses of the participating nodes in network-byteorder. The
 * first @ref nodes entries represent the ordinary nodes, the last entry
 * is the node of the license-daemon.
 *
 * @param id The id of the actual node within the participating
 * nodes. The license server has to have @a id = @a nodes.
 *
 * @param callback Pointer to a callback-function. This function is called if
 * something exceptional happens. If NULL, no callbacks will be done.
 *
 *
 * @return On success, the filedescriptor of the MCast socket is returned.
 * On error, exit() is called within this function.  */
int initMCast(int nodes, int mcastgroup, unsigned short portno,
	      int usesyslog,  unsigned int hosts[], int id,
	      void (*callback)(int, void*));

/**
 * @brief Shutdown the MCast module.
 *
 * Shutdown the whole MCast machinery.
 *
 * @return No return value.
 */
void exitMCast(void);

/**
 * @brief Tell MCast about a dead node.
 *
 * Tell MCast, that node @a node is dead.
 *
 * @param node The node to be declared dead.
 *
 * @return No return value.
 */
void declareNodeDeadMCast(int node);

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
 * Set the debug-level of the MCast module. Possible values are:
 *  - 0: Critical errors (usually exit). This is the default.
 *  - 2: Basic info about initialization.
 *  - 4: More detailed info about initialization, i.e. from
 *       initConntableMCast().
 *  - 5: Info about interrupted syscalls.
 *  - 6: Info about @ref T_CLOSE and new pings.
 *  - 8: Info about every 5th missing ping.
 *  -10: Info about every missing ping.
 *  -11: Info about every received ping.
 *  -12: Info about every sent ping.
 *
 * @param level The debug-level to set.
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
 *
 * @param node The node, to get MCast connection information about.
 *
 * @param info The @ref MCastConInfo structure holding the connection info
 * on return.
 *
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
 *
 * @param node The node, to get MCast status information about.
 *
 * @param string The string to which the status information is written.
 *
 * @param len The length of @a string.
 *
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
