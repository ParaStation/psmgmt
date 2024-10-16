/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Basic types for ParaStation tasks
 */
#ifndef __PSTASKID_H
#define __PSTASKID_H

#include <stdint.h>

/** Type to store unique task IDs in */
typedef int64_t PStask_ID_t;

/** Task Group constants */
typedef enum {
    TG_ANY,         /**< normal task */
    TG_ADMIN,       /**< taskgroup for psiadmin (and GUI client) */
    TG_RESET,       /**< normal task */
    TG_LOGGER,      /**< special task, the logger */
    TG_FORWARDER,   /**< special task, psid's forwarder to control clients */
    TG_SPAWNER,     /**< special task, the spawner (helper to spawn p4 jobs)
		       @deprecated due to ditched p4 support */
    TG_MONITOR,     /**< special task that monitors the daemon. Don't kill
		       @deprecated */
    TG_PSCSPAWNER,  /**< special task, the pscspawner (helper to spawn PSC)
		       @deprecated due to ditched PathScale support */
    TG_ADMINTASK,   /**< admin-task, i.e. unaccounted task */
    TG_SERVICE,     /**< service task, e.g. used by mpiexec to spawn procs */
    TG_ACCOUNT,     /**< accounter, will receive and log accounting info */
    TG_SERVICE_SIG, /**< service task, used by mpirun_openib to spawn
		     * procs; will receive SIGTERM on child's termination */
    TG_KVS,         /**< special task, the KVS used by the PMI interface */
    TG_DELEGATE,    /**< special task, used to hold resources */
    TG_PLUGINFW,    /**< forwarders started and controlled by plugins */
} PStask_group_t;

#endif  /* __PSTASKID_H */
