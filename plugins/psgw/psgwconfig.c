/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "psgwlog.h"

#include "psgwconfig.h"

#define SCRIPT_DIR LOCALSTATEDIR "/spool/parastation/scripts"
#define PSGWD_BINARY BINDIR "/psgwd"
#define ROUTE_PLUGIN PSGWLIBDIR "/plugin01.py"

const ConfDef_t CONFIG_VALUES[] =
{
    { "DIR_SCRIPTS", 0,
	"dir",
	SCRIPT_DIR,
	"Directory to search for pelogue scripts" },
    { "DIR_ROUTE_SCRIPTS", 0,
	"dir",
	PSGWLIBDIR,
	"Directory routing scripts are located" },
    { "ROUTE_SCRIPT", 0,
	"file",
	"psroute.py",
	"The filename of the routing script" },
    { "STRICT_MODE", 0,
	"flag",
	"1",
	"Strict security checks" },
    { "DEFAULT_ROUTE_PLUGIN", 0,
	"string",
	ROUTE_PLUGIN,
	"Default route plugin" },
    { "DEFAULT_ROUTE_PREFIX", 0,
	"string",
	"psgw-route",
	"Default prefix of the routing file" },
    { "PSGWD_BINARY", 0,
	"string",
	PSGWD_BINARY,
	"The location of the psgwd binary" },
    { "TIMEOUT_ROUTE_SCRIPT", 0,
	"seconds",
	"60",
	"Maximal execution time of the route script in seconds" },
    { "TIMEOUT_PROLOGUE", 1,
	"sec",
	"300",
	"Number of seconds to allow the prologue scripts to run" },
    { "TIMEOUT_EPILOGUE", 1,
	"sec",
	"300",
	"Number of seconds to allow the epilogue scripts to run" },
    { "TIMEOUT_PE_GRACE", 1,
	"sec",
	"60",
	"Number of seconds until the local PE-logue timeout will be enforced" },
    { "DEBUG_MASK", 1,
	"int",
	"0",
	"Set the psgw debug mask" },
    { "GATEWAY_TPP", 1,
	"int",
	"8",
	"Set to max tpp of gateway nodes" },
    { NULL, 0, NULL, NULL, NULL },
};

bool initConfig(char *filename)
{
    /* parse psslurm config file */
    if (parseConfigFile(filename, &Config, false /*trimQuotes*/) < 0) return 0;
    setConfigDefaults(&Config, CONFIG_VALUES);
    if (verifyConfig(&Config, CONFIG_VALUES) != 0) {
	mlog("%s: verfiy of %s failed\n", __func__, filename);
	return false;
    }
    return true;
}
