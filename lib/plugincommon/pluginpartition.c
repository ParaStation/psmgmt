/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginpartition.h"

#include "pscommon.h"
#include "psidnodes.h"

bool isPSAdminUser(uid_t uid, gid_t gid)
{
    if (!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
		(PSIDnodes_guid_t){.u=uid})
	    && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
		(PSIDnodes_guid_t){.g=gid})) {
	return false;
    }
    return true;
}
