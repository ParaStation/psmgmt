/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>

#include "psslurmenv.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmconfig.h"

#include "pluginmalloc.h"

#include "psslurmlimits.h"

/** structure holding resource limits */
typedef struct {
    rlim_t limit;   /**< resource limit */
    char *name;     /**< Slurm rlimit name */
    char *psname;   /**< PSI rlimit name */
    bool propagate; /**< if true the limit is propagated */
} Limits_t;

/** holding all supported limits */
static Limits_t slurmConfLimits[] =
{
    { RLIMIT_CPU,     "CPU",	 "__PSI_CPU",       false },
    { RLIMIT_FSIZE,   "FSIZE",   "__PSI_FSIZE",     false },
    { RLIMIT_DATA,    "DATA",	 "__PSI_DATASIZE",  false },
    { RLIMIT_STACK,   "STACK",   "__PSI_STACKSIZE", false },
    { RLIMIT_CORE,    "CORE",	 "__PSI_CORESIZE",  false },
    { RLIMIT_RSS,     "RSS",	 "__PSI_RSS",       false },
    { RLIMIT_NPROC,   "NPROC",   "__PSI_NPROC",     false },
    { RLIMIT_NOFILE,  "NOFILE",  "__PSI_NOFILE",    false },
    { RLIMIT_MEMLOCK, "MEMLOCK", "__PSI_MEMLOCK",   false },
    { RLIMIT_AS,      "AS",	 "__PSI_ASSIZE",    false },
    { 0,	      NULL,	 NULL,		    false },
};

void printLimits(void)
{
    int i = 0;

    while (slurmConfLimits[i].name) {
	mlog("%s: 'RLIMIT_%s' '%i'\n", __func__, slurmConfLimits[i].name,
		slurmConfLimits[i].propagate);
	i++;
    }
}

static void setPropagateFlags(bool prop)
{
    int i = 0;

    while (slurmConfLimits[i].name) {
	slurmConfLimits[i].propagate = prop;
	i++;
    }
}

bool initLimits(void)
{
    const char delimiters[] =", \t\n";
    char *next, *saveptr;
    char *conf;
    int i = 0, found = 0;

    if ((conf = getConfValueC(&SlurmConfig, "PropagateResourceLimits"))) {

	next = strtok_r(conf, delimiters, &saveptr);

	while (next) {
	    if (!(strcasecmp(next, "ALL"))) {
		setPropagateFlags(true);
	    } else if (!(strcasecmp(next, "NONE"))) {
		setPropagateFlags(false);
	    } else {
		found = i = 0;
		while (slurmConfLimits[i].name) {
		    if (!(strcasecmp(next, slurmConfLimits[i].name))) {
			slurmConfLimits[i].propagate = true;
			found = 1;
			break;
		    }
		    i++;
		}
		if (!found) {
		    mlog("%s: invalid entry '%s' for PropagateResourceLimits\n",
			 __func__, next);
		    return false;
		}
	    }
	    next = strtok_r(NULL, delimiters, &saveptr);
	}
    } else {
	/* default is to propagate all limits */
	setPropagateFlags(true);
    }

    if ((conf = getConfValueC(&SlurmConfig, "PropagateResourceLimitsExcept"))) {

	next = strtok_r(conf, delimiters, &saveptr);

	while (next) {
	    if (!(strcasecmp(next, "ALL"))) {
		setPropagateFlags(false);
	    } else if (!(strcasecmp(next, "NONE"))) {
		/* nothing to do here */
	    } else {
		found = i = 0;
		while (slurmConfLimits[i].name) {
		    if (!(strcasecmp(next, slurmConfLimits[i].name))) {
			slurmConfLimits[i].propagate = false;
			found = 1;
			break;
		    }
		    i++;
		}
		if (!found) {
		    mlog("%s: invalid entry '%s' for PropagateResourceLimits"
			    "Except\n", __func__, next);
		    return false;
		}
	    }
	    next = strtok_r(NULL, delimiters, &saveptr);
	}
    }

    return true;
}

static int resString2Limit(char *limit)
{
    char *name;
    int i = 0;

    if (!limit || strlen(limit) < 8) return -1;
    name = limit + 7;

    while (slurmConfLimits[i].name) {
	if (!(strcasecmp(name, slurmConfLimits[i].name))) {
	    return  slurmConfLimits[i].limit;
	}
	i++;
    }

    return -1;
}

static void doSetDefaultRlimits(char *limits, int soft)
{
    char *cp_limits, *toksave, *next, *lname, *lvalue, *tmp;
    const char delim[] = ",";
    int iLimit;
    unsigned long iValue;
    struct rlimit limit;

    cp_limits = ustrdup(limits);
    next = strtok_r(cp_limits, delim, &toksave);

    while (next) {
	if (!(tmp = strchr(next, '='))) {
	    mlog("%s: invalid rlimit '%s'\n", __func__, next);
	    next = strtok_r(NULL, delim, &toksave);
	    continue;
	}
	tmp[0] = '\0';
	lvalue = tmp + 1;
	lname = next;

	if ((iLimit = resString2Limit(lname)) == -1) {
	    mlog("%s: invalid rlimit name '%s'\n", __func__, lname);
	    next = strtok_r(NULL, delim, &toksave);
	    continue;
	}

	if ((sscanf(lvalue, "%lu", &iValue)) != 1) {
	    mlog("%s: invalid rlimit value '%s'\n", __func__, lvalue);
	    next = strtok_r(NULL, delim, &toksave);
	    continue;
	}

	if ((getrlimit(iLimit, &limit)) < 0) {
	    mlog("%s: getting rlimit failed :  '%s'\n", __func__,
		    strerror(errno));
	    limit.rlim_cur = limit.rlim_max = iValue;
	} else {
	    if (soft) {
		limit.rlim_cur = iValue;
	    } else {
		limit.rlim_max = iValue;
		if (limit.rlim_cur > limit.rlim_max) {
		    limit.rlim_cur = limit.rlim_max;
		}
	    }
	}

	if ((setrlimit(iLimit, &limit)) == -1) {
	    mlog("%s: setting default rlimit '%s' soft '%li' hard '%lu' failed:"
		    " %s\n", __func__, lname, limit.rlim_cur,
		    limit.rlim_max, strerror(errno));
	}

	next = strtok_r(NULL, delim, &toksave);
    }

    ufree(cp_limits);
}

void setDefaultRlimits(void)
{
    char *limits;

    /* set default hard rlimits */
    if ((limits = getConfValueC(&Config, "RLIMITS_HARD"))) {
	doSetDefaultRlimits(limits, 0);
    }

    /* set default soft rlimits */
    if ((limits = getConfValueC(&Config, "RLIMITS_SOFT"))) {
	doSetDefaultRlimits(limits, 1);
    }
}

void setRlimitsFromEnv(env_t *env, bool psi)
{
    struct rlimit limit;
    unsigned long softLimit;
    char *val, climit[128], pslimit[128];
    int i = 0;
    bool propagate = false;

    while (slurmConfLimits[i].name) {

	snprintf(climit, sizeof(climit), "SLURM_RLIMIT_%s",
		    slurmConfLimits[i].name);
	if ((val = envGet(env, climit))) {
	    /* user request to propagate value */
	    if (val[0] == 'U') {
		val++;
		propagate = true;
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
