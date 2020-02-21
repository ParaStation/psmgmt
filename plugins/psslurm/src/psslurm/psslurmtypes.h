/*
 * ParaStation
 *
 * Copyright (C) 2017-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_TYPES
#define __PSSLURM_TYPES

#include "psslurmmsg.h"
#include "psenv.h"

#ifdef HAVE_SPANK
#include "slurm/spank.h"
#endif

typedef struct {
    list_t next;            /**< used to put into some allocation-lists */
    uint32_t id;	    /**< unique allocation identifier */
    uint32_t packID;	    /**< unique pack identifier */
    uid_t uid;		    /**< user ID of the allocation owner */
    gid_t gid;		    /**< group of the allocation owner */
    uint32_t nrOfNodes;	    /**< number of nodes */
    PSnodes_ID_t *nodes;    /**< all participating nodes in the allocation */
    char *slurmHosts;	    /**< Slurm compressed host-list (SLURM_NODELIST) */
    env_t env;		    /**< environment variables */
    uint8_t terminate;	    /**< track number of terminate requests */
    int state;		    /**< current state of the allocation */
    char *username;	    /**< username of allocation owner */
    time_t firstKillReq;    /**< time the first kill request was received */
    time_t startTime;       /**< time the allocation started */
    uint32_t localNodeId;   /**< local node ID for this allocation */
    bool *epilogRes;	    /**< track epilogue results per node */
    uint32_t epilogCnt;     /**< number of nodes finished epilogue */
    bool nodeFail;	    /**< flag to save node failure */
} Alloc_t;

/** Handler type for SLURMd messages */
typedef void(*slurmdHandlerFunc_t)(Slurm_Msg_t *);

/**
 * @brief Register message handler function
 *
 * Register the function @a handler to handle all messages of type @a
 * msgType. If @a handler is NULL, all messages of type @a msgType
 * will be silently ignored in the future.
 *
 * @param msgType The message-type to handle.
 *
 * @param handler The function to call whenever a message of type @a
 * msgType has to be handled.
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If this is the
 * first handler registered for this message-type, NULL is returned.
 */
typedef slurmdHandlerFunc_t psSlurmRegMsgHandler_t(int msgType,
						   slurmdHandlerFunc_t handler);

/**
 * @brief Unregister message handler function
 *
 * Unregister the message-type @a msgType such that it will not be
 * handled in the future. This includes end of silent ignore of this
 * message-type.
 *
 * @param msgType The message-type not to handle any longer.
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If no handler was
 * registered or the message-type was unknown before, NULL is
 * returned.
  */
typedef slurmdHandlerFunc_t psSlurmClrMsgHandler_t(int msgType);

/**
 * @brief Duplicate SLURM message
 *
 * Create a duplicate of the SLURM message @a msg and return a pointer
 * to it. All data buffers @a msg is referring to are duplicated,
 * too. In order to cleanup the duplicate appropriately @ref
 * psSlurmReleaseMsg() shall be called when the duplicate is no longer
 * needed.
 *
 * @param msg SLURM messages to duplicate
 *
 * @return Upon success a pointer to the duplicate message is
 * returned. Or NULL in case of error.
 */
typedef Slurm_Msg_t * psSlurmDupMsg_t(Slurm_Msg_t *msg);

/**
 * @brief Release a duplicate SLURM message
 *
 * Release the duplicate SLURM message @a msg. This will also release
 * all data buffers @a sMsg is referring to. @a msg has to be created
 * by @ref psSlurmDupMsg().
 *
 * @warning If @a msg is not the results of a call to @ref
 * psSlurmDupMsg() the result is undefined and might lead to major
 * memory inconsistencies.
 *
 * @param msg SLURM messages to release
 *
 * @return No return value
 */
typedef void psSlurmReleaseMsg_t(Slurm_Msg_t *msg);

#ifdef HAVE_SPANK

typedef spank_err_t psSpankSetenv_t(spank_t, const char *, const char *, int);

typedef spank_err_t psSpankGetenv_t(spank_t, const char *, char *buf, int len);

typedef spank_err_t psSpankUnsetenv_t(spank_t, const char *);

typedef spank_err_t psSpankGetItem_t(spank_t, spank_item_t, va_list);

typedef int psSpankSymbolSup_t(const char *);

#endif

#endif /* __PSSLURM_TYPES */
