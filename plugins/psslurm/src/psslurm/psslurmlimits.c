/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>

#include "psslurmenv.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"

#include "psslurmlimits.h"

Limits_t slurmConfLimits[] =
{
    { RLIMIT_CPU,     "CPU",	 "__PSI_CPU",       0 },
    { RLIMIT_FSIZE,   "FSIZE",   "__PSI_FSIZE",     0 },
    { RLIMIT_DATA,    "DATA",	 "__PSI_DATASIZE",  0 },
    { RLIMIT_STACK,   "STACK",   "__PSI_STACKSIZE", 0 },
    { RLIMIT_CORE,    "CORE",	 "__PSI_CORESIZE",  0 },
    { RLIMIT_RSS,     "RSS",	 "__PSI_RSS",       0 },
    { RLIMIT_NPROC,   "NPROC",   "__PSI_NPROC",     0 },
    { RLIMIT_NOFILE,  "NOFILE",  "__PSI_NOFILE",    0 },
    { RLIMIT_MEMLOCK, "MEMLOCK", "__PSI_MEMLOCK",   0 },
    { RLIMIT_AS,      "AS",	 "__PSI_ASSIZE",    0 },
    { 0, NULL, NULL, 0 },
};

void printLimits()
{
    int i = 0;

    while (slurmConfLimits[i].name) {
	mlog("%s: 'RLIMIT_%s' '%i'\n", __func__, slurmConfLimits[i].name,
		slurmConfLimits[i].propagate);
	i++;
    }
}

static void setPropagateFlags(int prop)
{
    int i = 0;

    while (slurmConfLimits[i].name) {
	slurmConfLimits[i].propagate = prop;
	i++;
    }
}

int initLimits()
{
    const char delimiters[] =", \t\n";
    char *next, *saveptr;
    char *conf;
    int i = 0, found = 0;

    if ((conf = getConfValueC(&SlurmConfig, "PropagateResourceLimits"))) {

	next = strtok_r(conf, delimiters, &saveptr);

	while (next) {
	    if (!(strcasecmp(next, "ALL"))) {
		setPropagateFlags(1);
	    } else if (!(strcasecmp(next, "NONE"))) {
		setPropagateFlags(0);
	    } else {
		found = i = 0;
		while (slurmConfLimits[i].name) {
		    if (!(strcasecmp(next, slurmConfLimits[i].name))) {
			slurmConfLimits[i].propagate = 1;
			found = 1;
			break;
		    }
		    i++;
		}
		if (!found) {
		    mlog("%s: invalid entry '%s' for PropagateResourceLimits\n",
			 __func__, next);
		    return 0;
		}
	    }
	    next = strtok_r(NULL, delimiters, &saveptr);
	}
    } else {
	/* default is to propagate all limits */
	setPropagateFlags(1);
    }

    if ((conf = getConfValueC(&SlurmConfig, "PropagateResourceLimitsExcept"))) {

	next = strtok_r(conf, delimiters, &saveptr);

	while (next) {
	    if (!(strcasecmp(next, "ALL"))) {
		setPropagateFlags(0);
	    } else if (!(strcasecmp(next, "NONE"))) {
		/* nothing to do here */
	    } else {
		found = i = 0;
		while (slurmConfLimits[i].name) {
		    if (!(strcasecmp(next, slurmConfLimits[i].name))) {
			slurmConfLimits[i].propagate = 0;
			found = 1;
			break;
		    }
		    i++;
		}
		if (!found) {
		    mlog("%s: invalid entry '%s' for PropagateResourceLimits"
			    "Except\n", __func__, next);
		    return 0;
		}
	    }
	    next = strtok_r(NULL, delimiters, &saveptr);
	}
    }

    return 1;
}

/*
TODO: set default hard limits from config
void setHardRlimits()
{
    struct rlimit limit;

    if (!getrlimit(RLIMIT_CORE, &limit)) {
	limit.rlim_max = RLIM_INFINITY;
	if ((setrlimit(RLIMIT_CORE, &limit)) != 0) {
	    mlog("%s: setting RLIMIT_CORE failed\n", __func__);
	    return;
	}
	mlog("%s: limit set successful\n", __func__);
    } else {
	    mlog("%s: getting RLIMIT_CORE failed\n", __func__);
    }
}
*/

void setRlimitsFromEnv(env_t *env, int psi)
{
    struct rlimit limit;
    unsigned long softLimit;
    char *val, climit[128], pslimit[128];
    int i = 0, propagate = 0;

    while (slurmConfLimits[i].name) {

	snprintf(climit, sizeof(climit), "SLURM_RLIMIT_%s",
		    slurmConfLimits[i].name);
	if ((val = envGet(env, climit))) {
	    /* user wants us to propagate value */
	    if (val[0] == 'U') {
		val++;
		propagate = 1;
	    } else {
		propagate = slurmConfLimits[i].propagate;
	    }

	    if (propagate) {
		if ((sscanf(val, "%lu", &softLimit)) != 1) {
		    mlog("%s: invalid %s limit '%s'\n", __func__,
			    slurmConfLimits[i].name, val);
		} else {
		    if (!getrlimit(slurmConfLimits[i].limit, &limit)) {
			limit.rlim_cur = softLimit;
			if (softLimit > limit.rlim_max) {
			    softLimit = limit.rlim_max;
    			}
			/*
			mlog("%s: %s propagate: '%lu'\n", __func__, climit,
				softLimit);
			*/
			if ((setrlimit(slurmConfLimits[i].limit, &limit)) !=0) {
			    mwarn(errno, "%s: setting '%s' to '%lu' failed: ",
				    __func__, climit, softLimit);
			}
			if (psi) {
			    if (softLimit == RLIM_INFINITY) {
				envSet(env, slurmConfLimits[i].psname,
					    "infinity");
			    } else {
				snprintf(pslimit, sizeof(pslimit), "%lx",
					    softLimit);
				envSet(env, slurmConfLimits[i].psname,
					    pslimit);
			    }
			}
		    }
		}
	    }

	    /* remove from user env */
	    envUnset(env, climit);
	}
	i++;
    }
}
