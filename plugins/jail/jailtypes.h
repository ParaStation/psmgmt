/*
 * ParaStation
 *
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __JAIL_TYPES_H
#define __JAIL_TYPES_H

/**
 * @brief Get name of jail scripts
 *
 * Determine the names of the helper scripts currently configured for
 * the jail plugin. Script names will only be reported if the
 * corresponding file exist and is executable.
 *
 * @param jailScriptName Name of the script used to jail processes
 *
 * @param termScriptName Name of the script called after a jailed
 * process has terminated and cleanup might be required
 *
 * @return No return value
 */
typedef void (jailGetScripts_t)(const char **jailScriptName,
				const char **termScriptName);

#endif /* __JAIL_TYPES_H */
