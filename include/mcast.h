/*
 *               ParaStation3
 * mcast.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: mcast.h,v 1.1 2002/01/28 19:09:17 eicker Exp $
 *
 */
/**
 * \file
 * mcast: ParaStation MultiCast facility
 *
 * $Id: mcast.h,v 1.1 2002/01/28 19:09:17 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __RDP_H
#define __RDP_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

typedef enum {
    DOWN = 0x1,
    UP   = 0x2
} MCastState;

typedef struct MCastLoad_ {
    double load[3];           /* Load parameters of that node */
} MCastLoad;

typedef struct MCastConInfo_ {
    MCastState state;
    MCastLoad  load;
    int        misscounter;
} MCastConInfo;

typedef enum {
    T_INFO = 0x01,
    T_CLOSE,
    T_LIC,
    T_KILL
} MCastMsgType;

typedef struct Mmsg_ {
    short    node;     /* Sender ID */
    short    type;
    MCastState state;
    MCastLoad  load;
} MCastMsg;

#define MCAST_NEW_CONNECTION    0x80   /* buf == nodeno */
#define MCAST_LOST_CONNECTION   0x81   /* buf == nodeno */

#define MCAST_LIC_LOST          0x88   /* buf == sinaddr */ 
#define MCAST_LIC_SHUTDOWN      0x89   /* buf == reason */ 

#define LIC_LOST_CONECTION      0x1
#define LIC_KILL_MSG            0x2

/*
 * Initialize MCast
 * Parameters:  nodes: Nr of Nodes in the Cluster
 *              mgroup: Id of Multicastgroup (0 < id < 255)
 *              usesyslog (1=yes,0=no): Use syslog() to log error/info messages
 *              address of callback function to handle MCast exceptions / infos
 *                 (func == NULL allowed to prevent callback) 
 *                 callback func is called with (type, void *buf)
 */
int initMCast(int nodes, int mgroup, int usesyslog,  unsigned int hosts[],
	      void (*func)(int, void*));

/*
 * Shutdown MCast
 */
void exitMCast(void);

/**
 * @brief Query the debug-level.
 *
 * Get the debug-level of the MCast module.
 *
 * @return The actual debug-level is returned.
 *
 * @see setMCastDebugLevel()
 */
int getMCastDebugLevel(void);

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
 * @see getMCastDebugLevel()
 */
void setMCastDebugLevel(int level);

/**
 * @brief Get the dead-limit.
 *
 * Get the dead-limit of the MCast module, i.e. the number of pings
 * from a node allowed to be absent until the node is declared to be dead.
 *
 * @return The actual dead-limit.
 *
 * @see setMCastDeadLimit()
 */
int getMCastDeadLimit(void);

/**
 * @brief Set the dead-limit.
 *
 * Set the dead-limit of the MCast module, i.e. the number of pings
 * from a node allowed to be absent until the node is declared to be dead.
 *
 * @return No return value.
 *
 * @see getMCastDeadLimit()
 */
void setMCastDeadLimit(int limit);

/*
 * Get Info for node n
 */
void getMCastInfo(int n, MCastConInfo *info);

void getMCastStateInfo(int n, char *s, size_t len);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __RDP_H */
