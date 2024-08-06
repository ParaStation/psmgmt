#include <dlfcn.h>
#include <slurm/slurm_errno.h>
#include <slurm/spank.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#include "psslurmhandles.h"

#include "logging.h"
#include "psidplugin.h"

static logger_t psSpank_logger;

typedef enum {
    SPANK_LOG_ERROR = 2,
    SPANK_LOG_INFO,
    SPANK_LOG_VERBOSE,
    SPANK_LOG_DEBUG,
    SPANK_LOG_DEBUG2,
    SPANK_LOG_DEBUG3,
} psSpank_Log_Level_t;

static int psSpank_loglevel = SPANK_LOG_VERBOSE;

#define mlog(...) logger_print(psSpank_logger, -1, __VA_ARGS__)
#define flog(...) logger_funcprint(psSpank_logger, __func__, -1, __VA_ARGS__)

#define PSLOG(level, logger, fmt) {                     \
    va_list ap;                                         \
    if (!psSpank_logger ||                              \
	psSpank_loglevel < level) return;               \
    mlog("spank(L%i): ", level);                        \
    va_start(ap, fmt);                                  \
    logger_vprint(logger, -1, fmt, ap);                 \
    va_end(ap);                                         \
    mlog("\n");                                         \
}

bool psSpank_Init(bool verbose)
{
    psSpank_logger = logger_new(NULL, NULL);

    void *pluginHandle = PSIDplugin_getHandle("psslurm");

    if (!pluginHandle) {
	flog("getting psslurm handle failed\n");
	return false;
    }

    psSpankGetenv = dlsym(pluginHandle, "psSpankGetenv");
    if (!psSpankGetenv) {
	flog("loading psSpankGetenv() failed\n");
	return false;
    }

    psSpankSetenv = dlsym(pluginHandle, "psSpankSetenv");
    if (!psSpankSetenv) {
	flog("loading psSpankSetenv() failed\n");
	return false;
    }

    psSpankUnsetenv = dlsym(pluginHandle, "psSpankUnsetenv");
    if (!psSpankUnsetenv) {
	flog("loading psSpankUnsetenv() failed\n");
	return false;
    }

    psSpankGetItem = dlsym(pluginHandle, "psSpankGetItem");
    if (!psSpankGetItem) {
	flog("loading psSpankGetItem() failed\n");
	return false;
    }

    psSpankPrependArgv = dlsym(pluginHandle, "psSpankPrependArgv");
    if (!psSpankPrependArgv) {
	flog("loading psSpankPrependArgv() failed\n");
	return false;
    }

    psSpankSymbolSup = dlsym(pluginHandle, "psSpankSymbolSup");
    if (!psSpankSymbolSup) {
	flog("loading psSpankSymbolSup() failed\n");
	return false;
    }

    psSpankGetContext = dlsym(pluginHandle, "psSpankGetContext");
    if (!psSpankGetContext) {
	flog("loading psSpankGetContext() failed\n");
	return false;
    }

    psSpankOptGet = dlsym(pluginHandle, "psSpankOptGet");
    if (!psSpankOptGet) {
	flog("loading psSpankOptGet() failed\n");
	return false;
    }

    psSpankOptRegister = dlsym(pluginHandle, "psSpankOptRegister");
    if (!psSpankOptRegister) {
	flog("loading psSpankOptRegister() failed\n");
	return false;
    }

    psSpankPrint = dlsym(pluginHandle, "psSpankPrint");
    if (!psSpankPrint) {
	flog("loading psSpankPrint() failed\n");
	return false;
    }

    if (verbose) mlog("spank api successfully started\n");

    return true;
}

/***
 ** The following functions are directly called by Spank plugins
 ***/

void slurm_spank_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    psSpankPrint(fmt, ap, NULL);
    va_end(ap);
}

void slurm_info(const char *fmt, ...)
{
    PSLOG(SPANK_LOG_INFO, psSpank_logger, fmt);
}

void slurm_error(const char *fmt, ...)
{
    PSLOG(SPANK_LOG_ERROR, psSpank_logger, fmt);
}

void slurm_verbose(const char *fmt, ...)
{
    PSLOG(SPANK_LOG_VERBOSE, psSpank_logger, fmt);
}

void slurm_debug(const char *fmt, ...)
{

    PSLOG(SPANK_LOG_DEBUG, psSpank_logger, fmt);
}

void slurm_debug2(const char *fmt, ...)
{

    PSLOG(SPANK_LOG_DEBUG2, psSpank_logger, fmt);
}

void slurm_debug3(const char *fmt, ...)
{
    PSLOG(SPANK_LOG_DEBUG3, psSpank_logger, fmt);
}

/*
 *  Return the string representation of a spank_err_t error code.
 */
const char *spank_strerror (spank_err_t err)
{
    if (err == ESPANK_SUCCESS) return "Success";

    switch (err) {
	case ESPANK_ERROR:
	    return "Generic error";
	case ESPANK_BAD_ARG:
	    return "Bad argument";
	case ESPANK_NOT_TASK:
	    return "Not in task context";
	case ESPANK_ENV_EXISTS:
	    return "Environment variable exists";
	case ESPANK_ENV_NOEXIST:
	    return "No such environment variable";
	case ESPANK_NOSPACE:
	    return "Buffer too small";
	case ESPANK_NOT_REMOTE:
	    return "Valid only in remote context";
	case ESPANK_NOEXIST:
	    return "Id/PID does not exist on this node";
	case ESPANK_NOT_EXECD:
	    return "Lookup by PID requested, but no tasks running";
	case ESPANK_NOT_AVAIL:
	    return "Item not available from this callback";
	case ESPANK_NOT_LOCAL:
	    return "Valid only in local or allocator context";
	default:
	    return "Unknown";
    }
    return "Unknown";
}

/*
 *  Determine whether a given spank plugin symbol is supported
 *   in this version of SPANK interface.
 *
 *  Returns:
 *  = 1   The symbol is supported
 *  = 0   The symbol is not supported
 *  = -1  Invalid argument
 */
int spank_symbol_supported (const char *symbol)
{
    return psSpankSymbolSup(symbol);
}

/*
 *  Determine whether plugin is loaded in "remote" context
 *
 *  Returns:
 *  = 1   remote context, i.e. plugin is loaded in /slurmstepd.
 *  = 0   not remote context
 *  < 0   spank handle was not valid.
 */
int spank_remote (spank_t spank)
{
    if (!spank) return -1;
    if (psSpankGetContext(spank) == S_CTX_REMOTE) return 1;
    return 0;
}

/*
 *  Return the context in which the calling plugin is loaded.
 *
 *  Returns the spank_context for the calling plugin, or SPANK_CTX_ERROR
 *   if the current context cannot be determined.
 */
spank_context_t spank_context (void)
{
    return psSpankGetContext(NULL);
}

/*
 *  Register a plugin-provided option dynamically. This function
 *   is only valid when called from slurm_spank_init(), and must
 *   be guaranteed to be called in all contexts in which it is
 *   used (local, remote, allocator).
 *
 *  This function is the only method to register options in
 *   allocator context.
 *
 *  May be called multiple times to register many options.
 *
 *  Returns ESPANK_SUCCESS on successful registration of the option
 *   or ESPANK_BAD_ARG if not called from slurm_spank_init().
 */
spank_err_t spank_option_register (spank_t spank, struct spank_option *opt)
{
    if (!spank || !opt) return ESPANK_BAD_ARG;
    return psSpankOptRegister(spank, opt);
}

/*
 *  Check whether spank plugin option [opt] has been activated.
 *   If the option takes an argument, then the option argument
 *   (if found) will be returned in *optarg.
 *  This function can be invoked from the following functions:
 *  slurm_spank_job_prolog, slurm_spank_local_user_init, slurm_spank_user_init,
 *  slurm_spank_task_init_privileged, slurm_spank_task_init,
 *  slurm_spank_task_exit, and slurm_spank_job_epilog.
 *
 *  Returns
 *   ESPANK_SUCCESS if the option was used by user. In this case
 *    *optarg will contain the option argument if opt->has_arg != 0.
 *   ESPANK_ERROR if the option wasn't used.
 *   ESPANK_BAD_ARG if an invalid argument was passed to the function,
 *    such as NULL opt, NULL opt->name, or NULL optarg when opt->has_arg != 0.
 *   ESPANK_NOT_AVAIL if called from improper context.
 */
spank_err_t spank_option_getopt (spank_t spank, struct spank_option *opt,
	                         char **optarg)
{
    if (!spank || !opt) return ESPANK_BAD_ARG;
    return psSpankOptGet(spank, opt, optarg);
}

/*  Get the value for the current job or task item specified,
 *   storing the result in the subsequent pointer argument(s).
 *   Refer to the spank_item_t comments for argument types.
 *   For S_JOB_ARGV, S_JOB_ENV, and S_SLURM_VERSION* items
 *   the result returned to the caller should not be freed or
 *   modified.
 *
 *  Returns ESPANK_SUCCESS on success, ESPANK_NOTASK if an S_TASK*
 *   item is requested from outside a task context, ESPANK_BAD_ARG
 *   if invalid args are passed to spank_get_item or spank_get_item
 *   is called from an invalid context, and ESPANK_NOT_REMOTE
 *   if not called from slurmstepd context or spank_local_user_init.
 */
spank_err_t spank_get_item (spank_t spank, spank_item_t item, ...)
{
    va_list ap;
    spank_err_t ret = ESPANK_BAD_ARG;

    va_start(ap, item);
    ret = psSpankGetItem(spank, item, ap);
    va_end(ap);

    return ret;
}

/*  Place a copy of environment variable "var" from the job's environment
 *   into buffer "buf" of size "len."
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *    ESPANK_BAD_ARG      = spank handle invalid or len < 0.
 *    ESPANK_ENV_NOEXIST  = environment variable doesn't exist in job's env.
 *    ESPANK_NOSPACE      = buffer too small, truncation occurred.
 *    ESPANK_NOT_REMOTE   = not called in remote context (i.e. from slurmd).
 */
spank_err_t spank_getenv (spank_t spank, const char *var, char *buf, int len)
{
    if (!spank || !var || !buf || len <=0) return ESPANK_BAD_ARG;
    return psSpankGetenv(spank, var, buf, len);
}

/*
 *  Set the environment variable "var" to "val" in the environment of
 *   the current job or task in the spank handle. If overwrite != 0 an
 *   existing value for var will be overwritten.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *     ESPANK_ENV_EXISTS  = var exists in job env and overwrite == 0.
 *     ESPANK_BAD_ARG     = spank handle invalid or var/val are NULL.
 *     ESPANK_NOT_REMOTE  = not called from slurmstepd.
 */
spank_err_t spank_setenv (spank_t spank, const char *var, const char *val,
                          int overwrite)
{
    if (!spank || !var || !val) return ESPANK_BAD_ARG;
    return psSpankSetenv(spank, var, val, overwrite);
}

/*
 *  Unset environment variable "var" in the environment of current job or
 *   task in the spank handle.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *    ESPANK_BAD_ARG   = spank handle invalid or var is NULL.
 *    ESPANK_NOT_REMOTE = not called from slurmstepd.
 */
spank_err_t spank_unsetenv (spank_t spank, const char *var)
{
    if (!spank || !var) return ESPANK_BAD_ARG;
    return psSpankUnsetenv(spank, var);
}

/*
 *  Set an environment variable "name" to "value" in the "job control"
 *   environment, which is an extra set of environment variables
 *   included in the environment of the Slurm prolog and epilog
 *   programs. Environment variables set via this function will
 *   be prepended with SPANK_ to differentiate them from other env
 *   vars, and to avoid security issues.
 *
 *  Returns ESPANK_SUCCESS on success, o/w/ spank_err_t on failure:
 *     ESPANK_ENV_EXISTS  = var exists in control env and overwrite == 0.
 *     ESPANK_NOT_LOCAL   = not called in local context
 */
spank_err_t spank_job_control_setenv (spank_t sp, const char *name,
                                      const char *value, int overwrite)
{
    return ESPANK_NOT_LOCAL;
}

/*
 *  Place a copy of environment variable "name" from the job control
 *   environment into a buffer buf of size len.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *     ESPANK_BAD_ARG     = invalid spank handle or len <= 0
 *     ESPANK_ENV_NOEXIST = environment var does not exist in control env
 *     ESPANK_NOSPACE     = buffer too small, truncation occurred.
 *     ESPANK_NOT_LOCAL   = not called in local context
 */
spank_err_t spank_job_control_getenv (spank_t sp, const char *name,
				      char *buf, int len)
{
    if (!sp || !name || !buf || len <=0) return ESPANK_BAD_ARG;
    return ESPANK_NOT_LOCAL;
}

/*
 *  Unset environment variable "name" in the job control environment.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *     ESPANK_BAD_ARG   = invalid spank handle or var is NULL
 *     ESPANK_NOT_LOCAL   = not called in local context
 */
spank_err_t spank_job_control_unsetenv (spank_t sp, const char *name)
{
    if (!sp || !name) return ESPANK_BAD_ARG;
    return ESPANK_NOT_LOCAL;
}

/*
 *  Prepend the argument vector "argv" of length "argc" to the
 *  argument vector of the task to be spawned
 *  This function can be invoked from the following functions to take effect:
 *  slurm_spank_task_init_privileged, and slurm_spank_task_init.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *    ESPANK_BAD_ARG   = spank handle invalid or argv is NULL.
 *    ESPANK_NOT_TASK  = called from outside a task context.
 */

spank_err_t spank_prepend_task_argv (spank_t spank, int argc, const char *argv[])
{
    if (!spank || !argv) return ESPANK_BAD_ARG;

    return psSpankPrependArgv(spank, argc, argv);
}
