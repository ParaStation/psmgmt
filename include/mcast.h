/*
 * ParaStation
 *
 * Copyright (C) 2002-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation MultiCast facility
 */
#ifndef __MCAST_H
#define __MCAST_H

#include <stdio.h>
#include <stdint.h>

/** Possible MCast states of a node */
typedef enum {
    DOWN = 0x1,  /**< node is down */
    UP   = 0x2   /**< node is up */
} MCastState_t;

/** The load info of a node */
typedef struct {
    double load[3];           /**< The actual load parameters */
} MCastLoad_t;

/** The jobs of a node */
typedef struct {
    short total;              /**< The total number of jobs */
    short normal;             /**< Number of "normal" jobs (i.e. without
				   admin, logger etc.) */
} MCastJobs_t;

/** The MCast status info about a node */
typedef struct {
    MCastLoad_t load;    /**< The load info of the node @see MCastLoad_t */
    MCastJobs_t jobs;    /**< The job info of the node @see MCastJobs_t */
} MCastConInfo_t;

/** Structure of a MCast message */
typedef struct {
    short node;          /**< Sender ID */
    short type;          /**< Message type */
    MCastState_t state;  /**< The state info @see MCastState_t */
    MCastLoad_t load;    /**< The load info @see MCastLoad_t */
    MCastJobs_t jobs;    /**< The job info @see MCastJobs_t */
} MCastMsg_t;

/** Tag to @ref MCastCallback: New connection detected */
#define MCAST_NEW_CONNECTION  0x80
/** Tag to @ref MCastCallback: Connection lost */
#define MCAST_LOST_CONNECTION 0x81

/**
 * @brief Initialize the MCast module.
 *
 * Initializes the MCast machinery for @a nodes nodes.
 *
 *
 * @param nodes Number of nodes to handle.
 *
 * @param mcastgroup The MCast group to use. If 0, @ref DEFAULT_MCAST_GROUP is
 * used.
 *
 * @param portno The UDP port number in host byteorder to use for sending and
 * receiving packets. If 0, @ref DEFAULT_MCAST_PORT is used.
 *
 * @param logfile File to use for logging. If NULL, syslog(3) is used.
 *
 * @param hosts An array of size @a nodes containing the IP-addresses
 * of the participating nodes in network-byteorder.
 *
 * @param id The id of the actual node within the participating
 * nodes.
 *
 * @param callback Pointer to a callback-function. This function is called if
 * something exceptional happens. If NULL, no callbacks will be done.
 *
 *
 * @return On success, the filedescriptor of the MCast socket is returned.
 * On error, exit() is called within this function.
 *
 * @see syslog()
 */
int initMCast(int nodes, int mcastgroup, unsigned short portno,
	      FILE* logfile,  unsigned int hosts[], int id,
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
 * Various message classes for logging. These define the different
 * bits of the debug-mask set via @ref setDebugMaskMCast().
 */
typedef enum {
    MCAST_LOG_INIT = 0x0001, /**< Info from initialization (IP etc.) */
    MCAST_LOG_INTR = 0x0002, /**< Interrupted syscalls */
    MCAST_LOG_CONN = 0x0004, /**< @ref T_CLOSE and new pings */
    MCAST_LOG_5MIS = 0x0008, /**< Every 5th missing ping */
    MCAST_LOG_MSNG = 0x0010, /**< Every missing ping */
    MCAST_LOG_RCVD = 0x0020, /**< Every received ping */
    MCAST_LOG_SENT = 0x0040, /**< Every sent ping */
} MCast_log_key_t;

/**
 * @brief Query the debug-mask.
 *
 * Get the debug-mask of the MCast module.
 *
 * @return The actual debug-mask is returned.
 *
 * @see setDebugMaskMCast()
 */
int32_t getDebugMaskMCast(void);

/**
 * @brief Set the debug-mask.
 *
 * Set the debug-mask of the MCast module. @a mask is a bit-wise OR of
 * the different keys defined within @ref MCast_log_key_t. If the
 * respective bit is set within @a mask, the log-messages marked with
 * the corresponding bits are put out to the selected channel
 * (i.e. stderr of syslog() as defined within @ref
 * initMCast()). Accordingly a @mask of -1 means to put out all
 * messages defined.
 *
 * All messages marked with -1 represent fatal messages that are
 * always put out independently of the choice of @a mask, i.e. even if
 * it is 0.
 *
 * @a mask's default value is 0, i.e. only fatal messages are put out.
 *
 * @param mask The debug-mask to set.
 *
 * @return No return value.
 *
 * @see getDebugMaskMCast(), MCast_log_key_t
 */
void setDebugMaskMCast(int32_t mask);

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
 * @brief Set job info.
 *
 * Set the information on running jobs on node @a node. If @a total is
 * different from 0, the absolute number of jobs is increased by
 * one. The same hold for @a normal and the number of normal jobs,
 * i.e. jobs that are intended for computation and not for
 * administrational tasks.
 *
 * If the number of jobs on the local node is modified, an extra MCast
 * ping is triggered.
 *
 * @param node The ParaStation ID of the node which job numbers are
 * modified.
 *
 * @param total Flag if total count of jobs has to be increased.
 *
 * @param normal Flag if count of normal jobs has to be increased.
 *
 * @return No return value
 */
void incJobsMCast(int node, int total, int normal);

/**
 * @brief Set job info.
 *
 * Set the information on running jobs on node @a node. If @a total is
 * different from 0, the absolute number of jobs is decreased by
 * one. The same hold for @a normal and the number of normal jobs,
 * i.e. jobs that are intended for computation and not for
 * administrational tasks.
 *
 * If the number of jobs on the local node is modified, an extra MCast
 * ping is triggered.
 *
 * @param node The ParaStation ID of the node which job numbers are
 * modified.
 *
 * @param total Flag if total count of jobs has to be decreased.
 *
 * @param normal Flag if count of normal jobs has to be decreased.
 *
 * @return No return value
 */
void decJobsMCast(int node, int total, int normal);

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
void getInfoMCast(int node, MCastConInfo_t* info);

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
void getStateInfoMCast(int node, char* string, size_t len);

#endif  /* __MCAST_H */
