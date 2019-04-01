/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "slurm/spank.h"

#define DEBUG 0

/*
 * All spank plugins must define this macro for the Slurm plugin loader.
 */
SPANK_PLUGIN(psgw, 1);

static int gwNum = 0;

static char *routeFile = NULL;

static char *routePlugin = NULL;

static char *gwEnv = NULL;

static char *gwBinary = NULL;

static bool gwCleanup = false;

static bool writeInfo = true;

int setGwNum(int val, const char *optarg, int remote);
int setGwFile(int val, const char *optarg, int remote);
int setGwPlugin(int val, const char *optarg, int remote);
int setGwEnv(int val, const char *optarg, int remote);
int setGwCleanup(int val, const char *optarg, int remote);
int setGwBinary(int val, const char *optarg, int remote);

/*
 * Additional options for salloc/sbatch/srun
 */
static struct spank_option spank_opt[] =
{
    { "gw_file", "path",
      "Path to the gateway routing file", 1, 0,
      (spank_opt_cb_f) setGwFile },
    { "gw_plugin", "string",
      "Name of the route plugin", 1, 0,
      (spank_opt_cb_f) setGwPlugin },
    { "gw_num", "number",
      "Number of gateway nodes", 1, 0,
      (spank_opt_cb_f) setGwNum },
    { "gw_env", "string",
      "Additional gateway environment variables", 1, 0,
      (spank_opt_cb_f) setGwEnv },
    { "gw_cleanup", NULL,
      "Automatically cleanup the route file", 0, 0,
      (spank_opt_cb_f) setGwCleanup },
    { "gw_binary", "path", "debug psgwd", 1, 0,
      (spank_opt_cb_f) setGwBinary },
    SPANK_OPTIONS_TABLE_END
};

/**
 * @brief Register additional options
 */
int slurm_spank_init(spank_t sp, int ac, char **av)
{
    int i, numOptions = sizeof(spank_opt) / sizeof(spank_opt[0]);

    for (i=0; i<numOptions; i++) {
	spank_option_register(sp, &spank_opt[i]);
    }

    if (DEBUG) slurm_info("set JOB_CONTROL in context: %i", spank_context());

    return 0;
}

/**
 * @brief Set environment variables after option parsing
 */
int slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{
    if (!gwNum && (routeFile || routePlugin)) {
        if (writeInfo) slurm_error("psgw: specify the number of gateway nodes");
        writeInfo = false;
        return -1;
    }

    if (gwNum) {
        char buf[1024];
	snprintf(buf, sizeof(buf), "%i", gwNum);
	spank_job_control_setenv(sp, "SLURM_SPANK_PSGW_NUM", buf, 1);
        if (writeInfo) slurm_info("psgw: requesting %i gateway nodes", gwNum);
    }

    if (routeFile) {
	spank_job_control_setenv(sp, "SLURM_SPANK_PSGW_ROUTE_FILE", routeFile, 1);
        if (writeInfo) slurm_info("psgw: using route file %s", routeFile);
    }

    if (routePlugin) {
	spank_job_control_setenv(sp, "SLURM_SPANK_PSGW_PLUGIN", routePlugin, 1);
        if (writeInfo) slurm_info("psgw: using route plugin %s", routePlugin);
    }

    if (gwEnv) {
	spank_job_control_setenv(sp, "SLURM_SPANK_PSGW_ENV", gwEnv, 1);
        if (writeInfo) slurm_info("psgw: using psgw env %s", gwEnv);
    }

    if (gwCleanup) {
	spank_job_control_setenv(sp, "SLURM_SPANK_PSGW_CLEANUP", "1", 1);
        if (writeInfo) slurm_info("psgw: automatic cleanup of route file");
    }

    if (gwBinary) {
	spank_job_control_setenv(sp, "SLURM_SPANK_PSGWD_BINARY", gwBinary, 1);
        if (writeInfo) slurm_info("psgw: using psgwd binary %s", gwBinary);
    }

    writeInfo = false;

    return 0;
}

/**
 * @brief Parse and set name of the route plugin
 */
int setGwPlugin(int val, const char *optarg, int remote)
{
    if (optarg == NULL) {
        slurm_error("psgw: specify the name of the route plugin");
        return -1;
    }
    routePlugin = strdup(optarg);

    if (DEBUG) slurm_info("set gw_plugin to %s", routePlugin);

    return 0;
}

/**
 * @brief Parse and set path to route file
 */
int setGwFile(int val, const char *optarg, int remote)
{
    if (optarg == NULL) {
        slurm_error("psgw: specify the path to the route file");
        return -1;
    }
    routeFile = strdup(optarg);

    if (DEBUG) slurm_info("set gw_file to %s", routeFile);

    return 0;
}

/**
 * @brief Parse and set number of gateways option
 */
int setGwNum(int val, const char *optarg, int remote)
{
    if (optarg == NULL) {
        slurm_error ("psgw: specify the number of gateway nodes");
        return -1;
    }

    int ret = sscanf(optarg, "%i", &gwNum);
    if (ret != 1) {
        slurm_error ("gw_num: not a vaild number");
	return -1;
    }
    if (DEBUG) slurm_info("set gw_num to %i", gwNum);

    return 0;
}

/**
 * @brief Parse and set the gateway environment
 */
int setGwEnv(int val, const char *optarg, int remote)
{
    if (optarg == NULL) {
        slurm_error("psgw: specify the psgwd environment variable");
        return -1;
    }
    gwEnv = strdup(optarg);

    if (DEBUG) slurm_info("set gw_env to %s", gwEnv);

    return 0;
}

/**
 * @brief Set automatic route file cleanup
 */
int setGwCleanup(int val, const char *optarg, int remote)
{
    gwCleanup = true;

    if (DEBUG) slurm_info("set gw_cleanup to true");

    return 0;
}

/**
 * @brief Parse and set path to the psgwd binary
 */
int setGwBinary(int val, const char *optarg, int remote)
{
    if (optarg == NULL) {
        slurm_error("psgw: specify the path to the psgwd binary");
        return -1;
    }
    gwBinary = strdup(optarg);

    if (DEBUG) slurm_info("set gw_binary to %s", gwBinary);

    return 0;
}
