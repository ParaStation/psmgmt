/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginconfig.h"
#include "psmomlog.h"

#include "psmomconfig.h"

const ConfDef_t confDef[] =
{
    { "PBS_SERVER", false, "string", NULL,
      "Address/Hostname of the PBS Server" },
    { "PORT_SERVER", true, "num", "15001",
      "Server-port of the PBS server" },
    { "PORT_MOM", true, "num", "15002",
      "MOM-port (for batch requests)" },
    { "PORT_RM", true, "num", "15003",
      "RM-port (for rm requests)" },
    { "TIME_OBIT", true, "sec", "10",
      "Number of seconds to wait for jobs to exit" },
    { "TIME_OBIT_RESEND", true, "sec", "45",
      "Interval for trying to resend job obit messages in seconds" },
    { "TIME_KEEP_ALIVE", true, "sec", "0",
      "Keep alive interval in seconds" },
    { "TIME_UPDATE", true, "sec", "45",
      "Number of seconds between a status update is send to the pbs_server" },
    { "DIR_NODE_FILES", false, "path", SPOOL_DIR "/nodefiles",
      "Directory to store node-files" },
    { "DIR_JOB_FILES", false, "path", SPOOL_DIR "/jobs",
      "Directory to store jobfiles" },
    { "DIR_SCRIPTS", false, "path", SPOOL_DIR "/scripts",
      "Directory to search for prologue/epilogue scripts" },
    { "DIR_SPOOL", false, "path", SPOOL_DIR "/temp",
      "Directory for the job output and error files" },
    { "DIR_TEMP", false, "path", NULL,
      "Directory for the job specific scratch space" },
    { "DIR_JOB_ACCOUNT", false, "path", SPOOL_DIR "/account",
      "Directory to store accounting data" },
    { "DIR_LOCAL_BACKUP", false, "path", SPOOL_DIR "/backup",
      "Directory for local backups" },
    { "DISABLE_PELOGUE", true, "bool", "0",
      "Disable pro-/epilogue scripts" },
    { "DISABLE_PAM", true, "bool", "1",
      "Disable the use of PAM when starting new user sessions" },
    { "TIMEOUT_PROLOGUE", true, "sec", "300",
      "Number of seconds to allow the prologue scripts to run" },
    { "TIMEOUT_EPILOGUE", true, "sec", "300",
      "Number of seconds to allow the epilogue scripts to run" },
    { "TIMEOUT_PE_GRACE", true, "sec", "60",
      "Number of seconds until the local PE-logue timeout will be enforced" },
    { "TIMEOUT_COPY", true, "sec", "0",
      "Number of seconds to allow the copy process to run" },
    { "TIMEOUT_CHILD_CONNECT", true, "sec", "10",
      "Number of seconds until a child must connect to mother superior" },
    { "TIMEOUT_CHILD_GRACE", true, "sec", "30",
      "Timeout in seconds until SIGKILL is sent when terminating a child" },
    { "TIMEOUT_BACKUP", true, "sec", "0",
      "Number of seconds until a backup process will be terminated" },
    { "CLEAN_JOBS_FILES", true, "bool", "1",
      "Auto clean the job-files directory" },
    { "CLEAN_NODE_FILES", true, "bool", "1",
      "Auto clean the node-files directory" },
    { "CLEAN_TEMP_DIR", true, "bool", "1",
      "Clean scratch space directory on startup" },
    { "REPORT_FS_SIZE", false, "path", NULL,
      "Total and available disc space for a partition is reported" },
    { "CMD_COPY", false, "string", "/bin/cp",
      "Command for the local copy-out process" },
    { "OPT_COPY", false, "string", "-rp",
      "Options for the local copy-out process" },
    { "COPY_REWRITE", false, "string", NULL,
      "Rewrite the destination for the copy-out process" },
    { "SET_ARCH", false, "string", NULL,
      "Set architecture of localhost" },
    { "SET_OPSYS", false, "string", NULL,
      "Set the operating system of localhost" },
    { "TORQUE_VERSION", true, "num", "3",
      "The torque protocol version support (2|3)" },
    { "JOB_ENV", false, "string", NULL,
      "Set additional environment variables" },
    { "JOB_UMASK", true, "num", NULL,
      "Set default umask for job stdout/error files" },
    { "ENFORCE_BATCH_START", true, "bool", "1",
      "Enforce jobs to use the Batchsystem, only admin user may use mpiexec"
      " directly" },
    { "WARN_USER_DAEMONS", true, "bool", "1",
      "Warn if leftover user daemons are found" },
    { "KILL_USER_DAEMONS", true, "bool", "0",
      "Kill leftover user daemons" },
    { "DEBUG_MASK", true, "num", "0x018000",
      "The debug mask for logging" },
    { "RLIMITS_SOFT", false, "list", NULL,
      "Set soft resource limits for user processes" },
    { "RLIMITS_HARD", false, "list", NULL,
      "Set hard resource limits for user processes" },
    { "OFFLINE_PELOGUE_TIMEOUT", true, "bool", "1",
      "Set my node offline if a pro/epilogue script timed out" },
    { "LOG_ACCOUNT_RECORDS", true, "bool", "0",
      "Write accounting records to the end of the stderror file" },
    { "BACKUP_SCRIPT", false, "string", NULL,
      "Script which can backup output/error/node files" },
    { "OPT_BACKUP_COPY", false, "string", "-rpl",
      "Options for local backup copy command" },
    { "TIMEOUT_SCRIPT", false, "string", NULL,
      "Script which is called when a prologue/epilogue timeout occurs" },
    { NULL, false, NULL, NULL, NULL },
};

LIST_HEAD(config);

static bool verifyVisitor(char *key, char *value, const void *info)
{
    const ConfDef_t *confEntry = info;
    int res = verifyConfigEntry(confEntry, key, value);

    switch (res) {
    case 0:
	break;
    case 1:
	mlog("Unknown config option '%s'\n", key);
	break;
    case 2:
	mlog("Option '%s' shall be numeric but is '%s'\n", key, value);
	return true;
    default:
	mlog("unexpected return %d from verifyConfigEntry()\n", res);
    }
    return false;
}

bool initConfig(char *cfgName)
{
    if (parseConfigFile(cfgName, &config, false /* trimQuotes */) < 0) {
	mlog("%s: failed to open '%s'\n", __func__, cfgName);
	return false;
    }

    if (traverseConfig(&config, verifyVisitor, confDef)) {
	return false;
    }

    setConfigDefaults(&config, confDef);
    return true;
}
