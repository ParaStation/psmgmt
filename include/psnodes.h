/*
 *               ParaStation3
 * psnodes.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psnodes.h,v 1.6 2003/10/08 13:44:47 eicker Exp $
 *
 */
/**
 * @file
 * Functions for handling the various informations about the nodes
 * with a ParaStation cluster
 *
 * $Id: psnodes.h,v 1.6 2003/10/08 13:44:47 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSNODES_H
#define __PSNODES_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize the PSnodes module.
 *
 * Initialize the PSnodes module. It will be prepared to handle @a
 * numNodes nodes.
 *
 * @param numNodes Number of nodes the PSnodes module is capable to
 * handle after successful returnd of this function.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_init(int numNodes);

/**
 * @brief Get the number of nodes.
 *
 * Get the actual number of nodes the PSnodes module is currently
 * capable to handle after the last call of PSnodes_init().
 *
 * @return The actual number of nodes.
 */
int PSnodes_getNum(void);

/**
 * @brief Register a new node.
 *
 * Register a new node with ParaStation ID @a id. This node will
 * reside on the host with IP address @a IPaddr. The IP address has to
 * be given in network byteorder.
 *
 * @param id ParaStation ID of the new node.
 *
 * @param IPaddr IP address of the new node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_register(int id, unsigned int IPaddr);


/**
 * @brief Get the ParaStation ID of a node.
 *
 * Get the ParaStation ID of the node with IP address @a IPaddr. The
 * IP address has to be given in network byteorder.
 *
 * @param IPaddr IP address of the node to lookup.
 *
 * @return If the node was found, the ParaStation ID is returned. Or
 * -1, if an error occured.
 */
int PSnodes_lookupHost(unsigned int IPaddr);

/**
 * @brief Get the IP address of a node.
 *
 * Get the IP address of the node with ParaStation ID @a id. The IP
 * address will be given in network byteorder.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the IP address is returned. Or
 * INADDR_ANY, if an error occured.
 */
unsigned int PSnodes_getAddr(int id);

/**
 * @brief Declare a node to be up.
 *
 * Declare the node with ParaStation ID @a id to be up.
 *
 * @param id ParaStation ID of the node to bring up.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_bringUp(int id);

/**
 * @brief Declare a node to be down.
 *
 * Declare the node with ParaStation ID @a id to be shutdown.
 *
 * @param id ParaStation ID of the node to bring down.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_bringDown(int id);

/**
 * @brief Test if a node is up.
 *
 * Test if the node with ParaStation ID @a id is up.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node is up, 1 is returned. 0 is returned, if an
 * error occured or the node is down.
 */
int PSnodes_isUp(int id);


/**
 * @brief Set the hardware type of a node.
 *
 * Set the hardware type of the node with ParaStation ID @a id to @a hwType.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param hwType The hardware type to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setHWType(int id, int hwType);

/**
 * @brief Get the hardware type of a node.
 *
 * Get the hardware type of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the hardware type is returned. Or
 * -1, if an error occured.
 */
int PSnodes_getHWType(int id);

/**
 * @brief Set the jobs flag of a node.
 *
 * Set the jobs flag of the node with ParaStation ID @a id to @a runjobs.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param runjobs The runjobs flag to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setRunJobs(int id, int runjobs);

/**
 * @brief Get the jobs flag of a node.
 *
 * Get the jobs flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the jobs flag is returned. Or
 * -1, if an error occured.
 */
int PSnodes_runJobs(int id);

/**
 * @brief Set the starter flag of a node.
 *
 * Set the starter flag of the node with ParaStation ID @a id to @a starter.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param starter The starter flag to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setIsStarter(int id, int starter);

/**
 * @brief Get the starter flag of a node.
 *
 * Get the starter flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the starter flag is returned. Or
 * -1, if an error occured.
 */
int PSnodes_isStarter(int id);

/**
 * @brief Set the extra IP address of a node.
 *
 * Set the extra IP address of the node with ParaStation ID @a id to @a addr.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param addr The extra IP address to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setExtraIP(int id, unsigned int addr);

/**
 * @brief Get the extra IP address of a node.
 *
 * Get the extra IP address of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the extra IP address is returned. Or
 * INADDR_ANY, if an error occured or the extra IP address was not set.
 */
unsigned int PSnodes_getExtraIP(int id);

/**
 * @brief Set the number of CPUs of a node.
 *
 * Set the number of CPUs of the node with ParaStation ID @a id to @a numCPU.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param numCPU The number of CPUs to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setCPUs(int id, short numCPU);

/**
 * @brief Get the number of CPUs of a node.
 *
 * Get the number of CPUs of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the number of CPUs is returned. Or
 * -1, if an error occured.
 */
short PSnodes_getCPUs(int id);

/**
 * @brief Set the hardware status of a node.
 *
 * Set the hardware status of the node with ParaStation ID @a id to @a
 * hwStatus.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param hwStatus The hardware status to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setHWStatus(int id, int hwStatus);

/**
 * @brief Get the hardware status of a node.
 *
 * Get the hardware status of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the hardware status is returned. Or
 * -1, if an error occured.
 */
int PSnodes_getHWStatus(int id);


/** Pseudo user ID to allow any user to run on a specific node */
#define PSNODES_ANYUSER (uid_t) -1

/**
 * @brief Set the exclusive user of a node.
 *
 * Set the exclusive user of the node with ParaStation ID @a id to @a uid.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param uid The exclusive user to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setUser(int id, uid_t uid);

/**
 * @brief Get the exclusive user of a node.
 *
 * Get the exclusive user of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the exclusive user is returned. Or
 * -1, if an error occured.
 */
uid_t PSnodes_getUser(int id);


/** Pseudo user ID to allow any group to run on a specific node */
#define PSNODES_ANYGROUP (gid_t) -1

/**
 * @brief Set the exclusive group of a node.
 *
 * Set the exclusive group of the node with ParaStation ID @a id to @a gid.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param uid The exclusive group to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setGroup(int id, uid_t uid);

/**
 * @brief Get the exclusive group of a node.
 *
 * Get the exclusive group of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the exclusive group is returned. Or
 * -1, if an error occured.
 */
uid_t PSnodes_getGroup(int id);


/** Pseudo number of processes to allow any job to run on a specific node */
#define PSNODES_ANYPROC -1

/**
 * @brief Set the maximum number of processes of a node.
 *
 * Set the maximum number of processes of the node with ParaStation ID
 * @a id to @a procs.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param procs The maximum number of processes to be set to this node.
 *
 * @return On success, 0 is returned or -1, if an error occured.
 */
int PSnodes_setProcs(int id, int procs);

/**
 * @brief Get the maximum number of processes of a node.
 *
 * Get the maximum number of processes of the node with ParaStation ID
 * @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the maximum number of processes is
 * returned. Or -1, if an error occured.
 */
int PSnodes_getProcs(int id);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSNODES_H */
