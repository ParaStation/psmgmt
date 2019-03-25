/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <dlfcn.h>

#include "plugin.h"
#include "psidplugin.h"
#include "pscommon.h"
#if 0
#include "psaccounthandles.h"
#endif

#include "pspmixlog.h"
#include "pspmixdaemon.h"
#include "pspmixforwarder.h"


/** psid plugin requirements */
char name[] = "pspmix";
int version = 1;
int requiredAPI = 110;
plugin_dep_t dependencies[] = {
#if 0
    { .name = "psaccount", .version = 24 },
#endif
    { .name = NULL, .version = 0 } };

int initialize(void)
{
#if 0
    void *handle = PSIDplugin_getHandle("psaccount");
#endif

    /* init the logger */
    pspmix_initLogger(NULL);

    /* set debug mask */
    pspmix_maskLogger(0);
/*    pspmix_maskLogger(PSPMIX_LOG_CALL | PSPMIX_LOG_ENV | PSPMIX_LOG_COMM
		    | PSPMIX_LOG_LOCK | PSPMIX_LOG_FENCE | PSPMIX_LOG_VERBOSE);

    PSC_setDebugMask(PSC_LOG_COMM);
*/
    /* initialize all modules */
    pspmix_initDaemonModule();
    pspmix_initForwarderModule();

#if 0
    /* get psaccount function handles */
    if (!handle) {
	psAccountSwitchAccounting = NULL;
	mlog("%s: getting psaccount handle failed\n", __func__);
    } else {
	psAccountSwitchAccounting = dlsym(handle, "psAccountSwitchAccounting");
	if (!psAccountSwitchAccounting) {
	    mlog("%s: loading function psAccountSwitchAccounting() failed\n",
		 __func__);
	}
    }
#endif

    mlog("(%i) successfully started\n", version);

    return 0;
}

void cleanup(void)
{
    /* remove registered hooks */
    pspmix_finalizeForwarderModule();
    pspmix_finalizeDaemonModule();

//    if (memoryDebug) fclose(memoryDebug); XXX wozu ist das gut?

    logger_finalize(pmixlogger);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
