/*
 * ParaStation
 *
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "plugincpufreq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pscpu.h"
#include "psstrv.h"
#include "psidhw.h"

#include "pluginlog.h"
#include "pluginmalloc.h"
#include "pluginscript.h"
#include "pluginhelper.h"

/* path to default CPU frequency script */
#define FREQ_SCRIPT PKGLIBEXECDIR "/cpufreq.py"

/** default /sys-path holding CPU scaling parameters */
#define DEFAULT_SYS_PATH "/sys/devices/system/cpu"

/** number of maximum supported frequencies for a single CPU */
#define MAX_FREQ 64

/** holding frequency scaling information for a single CPU */
typedef struct {
    CPUfreq_governors_t availGov;   /* available governor */
    uint32_t availMinFreq;	    /* available minimum frequency */
    uint32_t availMaxFreq;	    /* available maximum frequency */
    uint32_t availFreq[MAX_FREQ];   /* available frequency list */
    uint32_t numAvailFreq;	    /* number of available frequencies */
    CPUfreq_governors_t defGov;	    /* default governor */
    uint32_t defMinFreq;	    /* default minimum frequency */
    uint32_t defMaxFreq;	    /* default maximum frequency */
    CPUfreq_governors_t curGov;	    /* current governor */
    uint32_t curMinFreq;	    /* current minimum frequency */
    uint32_t curMaxFreq;	    /* current maximum frequency */
} CPUfreq_CPUs_t;

/** used as an index for Command_Map */
typedef enum {
    CMD_LIST_CPUS = 0,
    CMD_GET_AVAIL_GOV,
    CMD_GET_AVAIL_FREQ,
    CMD_GET_FREQ,
    CMD_GET_CUR_GOV,
    CMD_SET_MIN_FREQ,
    CMD_SET_MAX_FREQ,
    CMD_SET_GOV,
} Script_CMDs_t;

/** list of supported CPU frequency ranges */
typedef enum {
    FREQ_RANGE_FLAG     = 0x80000000,
    FREQ_RANGE_LOW	= 0x80000001,
    FREQ_RANGE_MEDIUM   = 0x80000002,
    FREQ_RANGE_HIGH     = 0x80000003,
    FREQ_RANGE_SEC_HIGH = 0x80000004,
} CPUfreq_range_t;

/** map commands and arguments */
typedef struct {
    char *name;
    char *arg;
    void (*fp)(char *output, void *info);
    void (*cb)(int32_t status, Script_Data_t *script);
} Command_Map_t;

/** forward declaration */
static Command_Map_t Command_Map[];

/** all CPUs including cached frequency values */
static CPUfreq_CPUs_t *cpus;

/** CPU frequency path in /sys */
static char *sysPath;

/** number of CPUs which support frequency scaling */
static int numCPUs;

/** set to common default governor (if any)  */
static CPUfreq_governors_t defaultGov = GOV_UNDEFINED;

/** set to common default minimum frequency (if any)  */
static uint32_t defaultMinFreq;

/** set to common default maximum frequency (if any)  */
static uint32_t defaultMaxFreq;

/** flag to indicate if all CPUs have the same available frequencies */
static bool equalAvailFreq = true;

/** list of initialization flags */
typedef enum {
    INIT_LIST_CPUS	= 0x0001,
    INIT_GET_AVAIL_GOV	= 0x0002,
    INIT_GET_CUR_GOV	= 0x0004,
    INIT_GET_FREQ	= 0x0008,
    INIT_GET_AVAIL_FREQ	= 0x0010,
} Init_Flags_t;

/** used to track information gather scripts */
static Init_Flags_t initFlags;

/** indicate failure while initialization */
static bool initFailure;

/** callback to return result of the initialization */
static CPUfreq_initCB_t *initCB;

/** governor string map */
static const struct {
    CPUfreq_governors_t gov;
    char *name;
} Governors_Map[] = {
    { GOV_UNDEFINED,	    "undefined"	   },
    { GOV_CONSERVATIVE,     "conservative" },
    { GOV_ONDEMAND,	    "ondemand",	   },
    { GOV_PERFORMANCE,	    "performance", },
    { GOV_POWERSAVE,	    "powersave",   },
    { GOV_USERSPACE,	    "userspace",   },
    { GOV_SCHEDUTIL,	    "schedutil",   },
    { 0,		    NULL,	   }
};

bool CPUfreq_isInitialized(void)
{
    return (sysPath && cpus && !initFailure);
}

/**
 * @brief Cleanup on failure and return result via callback
 */
static void retInitResult()
{
    if (initFailure) CPUfreq_finalize();
    initCB(!initFailure);
}

char *CPUfreq_gov2Str(CPUfreq_governors_t gov)
{
    for (int i = 0; Governors_Map[i].name; i++) {
	if (Governors_Map[i].gov == gov) return Governors_Map[i].name;
    }

    return "unknown";
}

CPUfreq_governors_t CPUfreq_str2Gov(char *govName)
{
    if (!govName) return GOV_UNDEFINED;

    for (int i = 0; Governors_Map[i].name; i++) {
	if (!strcmp(Governors_Map[i].name, trim(govName))) {
	    return Governors_Map[i].gov;
	}
    }

    return GOV_UNDEFINED;
}

/**
 * @brief Extract list of available CPUs
 *
 * @param output Script output holding CPU information
 *
 * @param info Script command name which was executed
 */
static void cmdListCPUs(char *output, void *info)
{
    int scriptCPUs;
    if (sscanf(output, "%i:", &scriptCPUs) != 1) {
	pluginflog("unknown number of CPUs : %s\n", output);
	initFailure = true;
	return;
    }

    numCPUs = PSIDhw_getHWthreads();
    if (numCPUs != scriptCPUs) {
	pluginflog("mismatch number of CPUs, psid %i script %i\n", numCPUs,
		   scriptCPUs);
	initFailure = true;
	return;
    }

    cpus = ucalloc(sizeof(*cpus) * numCPUs);
}

/**
 * @brief Extract all available CPU frequencies
 *
 * Depending on the hardware this optional information
 * might not be available.
 *
 * @param output Script output holding frequency information
 *
 * @param info Script command name which was executed
 */
static void cmdGetAvailFreq(char *output, void *info)
{
    if (!CPUfreq_isInitialized()) return;

    int idx;
    if (sscanf(output, " cpu %i avail_freq", &idx) != 1) {
	pluginflog("no CPU frequencies in %s\n", output);
	initFailure = true;
	return;
    }
    if (idx < 0 || idx >= numCPUs) {
	pluginflog("invalid index %u\n", idx);
	initFailure = true;
	return;
    }

    char *end = strstr(output, "avail_freq ");
    if (!end) {
	pluginflog("invalid frequencies: %s\n", output);
	initFailure = true;
	return;
    }
    end += 11;

    char *toksave, *next;
    const char delimiters[] =" \t";
    next = strtok_r(end, delimiters, &toksave);

    int numFreq = cpus[idx].numAvailFreq;
    while (next) {
	if (numFreq == MAX_FREQ) {
	    pluginflog("maximum %i supported frequencies exceeded\n", MAX_FREQ);
	    initFailure = true;
	    return;
	}
	long freq = atol(next);
	if (!freq) {
	    pluginflog("skipping invalid frequency %s\n", next);
	    continue;
	}
	cpus[idx].availFreq[numFreq++] = freq;
	next = strtok_r(NULL, delimiters, &toksave);
    }
    cpus[idx].numAvailFreq = numFreq;
}

/**
 * @brief Extract all available CPU governor
 *
 * @param output Script output holding governor information
 *
 * @param info Script command name which was executed
 */
static void cmdGetGovernors(char *output, void *info)
{
    if (!CPUfreq_isInitialized()) return;

    int cpu;
    if (sscanf(output, " cpu %i avail_gov", &cpu) != 1) {
	pluginflog("no cpu detected in %s\n", output);
	initFailure = true;
	return;
    }

    if (cpu < 0 || cpu >= numCPUs) {
	pluginflog("invalid index %u\n", cpu);
	initFailure = true;
	return;
    }

    char *end = strstr(output, "avail_gov ");
    if (!end) {
	pluginflog("invalid governors: %s\n", output);
	initFailure = true;
	return;
    }
    end += 10;

    char *toksave, *next;
    const char delimiters[] =" \t";
    next = strtok_r(end, delimiters, &toksave);

    while (next) {
	cpus[cpu].availGov |= CPUfreq_str2Gov(next);
	next = strtok_r(NULL, delimiters, &toksave);
    }
}

/**
 * @brief Extract available and current CPU frequencies
 *
 * @param output Script output holding frequency information
 *
 * @param info Script command name which was executed
 */
static void cmdGetFreq(char *output, void *info)
{
    if (!CPUfreq_isInitialized()) return;

    int idx, availMinFreq, availMaxFreq, curMinFreq, curMaxFreq;
    if (sscanf(output, "cpu %i avail_min_freq %i avail_max_freq %i "
	       "cur_min_freq %i cur_max_freq %i", &idx, &availMinFreq,
	       &availMaxFreq, &curMinFreq, &curMaxFreq) != 5) {
	pluginflog("failed to parse CPU frequencies %s\n", output);
	initFailure = true;
	return;
    }

    if (idx < 0 || idx >= numCPUs) {
	pluginflog("invalid index %u\n", idx);
	initFailure = true;
	return;
    }

    cpus[idx].availMinFreq = availMinFreq;
    cpus[idx].availMaxFreq = availMaxFreq;
    cpus[idx].curMinFreq = cpus[idx].defMinFreq = curMinFreq;
    cpus[idx].curMaxFreq = cpus[idx].defMaxFreq = curMaxFreq;
}

/**
 * @brief Extract current CPU governor
 *
 * @param output Script output holding governor information
 *
 * @param info Script command name which was executed
 */
static void cmdGetCurGov(char *output, void *info)
{
    if (!CPUfreq_isInitialized()) return;

    int idx;
    if (sscanf(output, " cpu %i cur_gov", &idx) != 1) {
	pluginflog("get CPU failed: %s\n", output);
	initFailure = true;
	return;
    }

    if (idx < 0 || idx >= numCPUs) {
	pluginflog("invalid index %u\n", idx);
	initFailure = true;
	return;
    }

    char *end = strstr(output, "cur_gov ");
    if (!end) {
	pluginflog("invalid governor: %s\n", output);
	initFailure = true;
	return;
    }
    end += 8;

    cpus[idx].curGov = cpus[idx].defGov = CPUfreq_str2Gov(end);
}

/**
 * @brief Log script output
 *
 * @param output Script output to log
 *
 * @param info Script command name which was executed
 */
static void cmdPrintOutput(char *output, void *info)
{
    char *name = info;
    pluginflog("%s: %s\n", name, output);
}

/**
 * @brief Test if initialization has completed
 *
 * Test if all initialization scripts finished gathering CPU information
 * and invoke callback.
 *
 */
static void testInitComplete(void)
{
    if (initFlags) {
	plugindbg(PLUGIN_LOG_FREQ, "gathering progress: %i\n", initFlags);
	return;
    }

    if (pluginmset(PLUGIN_LOG_FREQ)) {
	for (int i = 0; i < numCPUs; i++) {
	    pluginlog("cpu=%i avail gov 0x%x avail fmin %i avail fmax %i"
		      " def gov %s def fmin %i def fmax %i # avail freq %i \n",
		      i, cpus[i].availGov, cpus[i].availMinFreq,
		      cpus[i].availMaxFreq, CPUfreq_gov2Str(cpus[i].defGov),
		      cpus[i].defMinFreq, cpus[i].defMaxFreq,
		      cpus[i].numAvailFreq);
	}
    }

    if (!numCPUs || initFailure) {
	pluginlog("CPUfreq initialization failed\n");
    } else {
	pluginflog("%i CPUs", numCPUs);
	if (defaultGov != GOV_UNDEFINED) {
	    pluginlog(" def-governor '%s'", CPUfreq_gov2Str(defaultGov));
	}
	if (defaultMinFreq) {
	    pluginlog(" def-minFreq '%u'", defaultMinFreq);
	}
	if (defaultMaxFreq) {
	    pluginlog(" def-maxFreq '%u'", defaultMaxFreq);
	}
	if (equalAvailFreq) {
	    pluginlog(", equal frequencies");
	}
	pluginlog("\n");

	if (!defaultMinFreq || !defaultMaxFreq || !equalAvailFreq) {
	    pluginlog("warning: different CPU scaling settings can lead"
		       " to slower job launch times\n");
	}
    }

    retInitResult();
}

/* forward declaration */
static bool execCPUFreqScriptEx(Script_CMDs_t cmd, strv_t addArgV);

#define execCPUFreqScript(cmd) execCPUFreqScriptEx(cmd, NULL)

/**
 * @brief Callback for CMD_LIST_CPUS
 *
 * This initializes the main CPU data structures and
 * therefore has to be completed before any further data
 * gathering.
 */
static void cbListCPUs(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_LIST_CPUS;

    if (status) goto ERROR;

    if (execCPUFreqScript(CMD_GET_AVAIL_GOV)) {
	pluginflog("unable to determine governors\n");
	goto ERROR;
    }
    initFlags |= INIT_GET_AVAIL_GOV;

    if (execCPUFreqScript(CMD_GET_CUR_GOV)) {
	pluginflog("unable to determine current governor\n");
	goto ERROR;
    }
    initFlags |= INIT_GET_CUR_GOV;

    if (execCPUFreqScript(CMD_GET_FREQ)) {
	pluginflog("unable to determine CPU frequencies\n");
	goto ERROR;
    }
    initFlags |= INIT_GET_FREQ;

    /* read (optional) list all of CPU frequencies */
    execCPUFreqScript(CMD_GET_AVAIL_FREQ);
    initFlags |= INIT_GET_AVAIL_FREQ;

    Script_destroy(script);
    return;

ERROR:
    initFailure = true;
    retInitResult();
    Script_destroy(script);
}

/**
 * @brief Callback for CMD_GET_AVAIL_GOV
 */
static void cbGetAvailGov(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_GET_AVAIL_GOV;
    if (status) initFailure = true;
    testInitComplete();
    Script_destroy(script);
}

/**
 * @brief qsort compare function
 */
static int compareFreq(const void *entry1, const void *entry2)
{
    uint32_t *x = (uint32_t *) entry1;
    uint32_t *y = (uint32_t *) entry2;
    return *x - *y;
}

/**
 * @brief Calculate available CPU frequencies
 *
 * Not all hardware will define valid CPU frequencies. In that case
 * a list of sensible frequencies is calculated.
 */
static void calcAvailCPUfreq()
{
    plugindbg(PLUGIN_LOG_FREQ, "calculate %i CPU frequencies\n", MAX_FREQ);

    for (int c = 0; c < numCPUs; c++) {
	uint32_t delta = cpus[c].availMaxFreq - cpus[c].availMinFreq;
	delta /= MAX_FREQ -1;

	for (uint32_t i=0; i<(MAX_FREQ - 1); i++) {
	    cpus[c].availFreq[i] = cpus[c].availMinFreq + (delta * i);
	}
	cpus[c].availFreq[MAX_FREQ -1] = cpus[c].availMaxFreq;
	cpus[c].numAvailFreq = MAX_FREQ;
    }
}

/**
 * @brief Callback for CMD_GET_AVAIL_FREQ
 */
static void cbGetAvailFreq(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_GET_AVAIL_FREQ;

    /* not all systems define available frequencies, this is no error */
    if (status) {
	calcAvailCPUfreq();
	Script_destroy(script);
	return;
    }

    /* sort red frequencies */
    for (int i = 0; i < numCPUs; i++) {
	qsort(cpus[i].availFreq, cpus[i].numAvailFreq,
	      sizeof(cpus[i].availFreq[0]), compareFreq);
    }
    /* test if all CPUs have the same available frequencies */
    for (int c = 1; c < numCPUs; c++) {
	if (cpus[c].numAvailFreq != cpus[0].numAvailFreq) {
	    equalAvailFreq = false;
	    break;
	}
	for (uint32_t i = 0; i < cpus[0].numAvailFreq; i++) {
	    if (cpus[0].availFreq[i] != cpus[c].availFreq[i]) {
		equalAvailFreq = false;
		break;
	    }
	}
    }

    testInitComplete();
    Script_destroy(script);
}

/**
 * @brief Callback for CMD_GET_FREQ
 */
static void cbGetFreq(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_GET_FREQ;
    if (status) initFailure = true;

    /* test if all CPUs have the same frequencies */
    for (int i = 0; i < numCPUs; i++) {
	if (!defaultMinFreq) {
	    defaultMinFreq = cpus[i].defMinFreq;
	    continue;
	}
	if (defaultMinFreq != cpus[i].defMinFreq) {
	    defaultMinFreq = 0;
	    break;
	}
    }
    for (int i = 0; i < numCPUs; i++) {
	if (!defaultMaxFreq) {
	    defaultMaxFreq = cpus[i].defMaxFreq;
	    continue;
	}
	if (defaultMaxFreq != cpus[i].defMaxFreq) {
	    defaultMaxFreq = 0;
	    break;
	}
    }

    testInitComplete();
    Script_destroy(script);
}

/**
 * @brief Callback for CMD_GET_CUR_GOV
 */
static void cbGetCurGov(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_GET_CUR_GOV;
    if (status) initFailure = true;

    /* test if all CPUs have the same default governor */
    for (int i = 0; i < numCPUs; i++) {
	if (defaultGov == GOV_UNDEFINED) {
	    defaultGov = cpus[i].defGov;
	    continue;
	}
	if (defaultGov != cpus[i].defGov) {
	    defaultGov = GOV_UNDEFINED;
	    break;
	}
    }

    testInitComplete();
    Script_destroy(script);
}

/* map containing description, argument and function for a command */
static Command_Map_t Command_Map[] = {
    { "LIST_CPUS",	"--list-cpus",      cmdListCPUs,     cbListCPUs },
    { "GET_AVAIL_GOV",	"--get-avail-gov",  cmdGetGovernors, cbGetAvailGov },
    { "GET_AVAIL_FREQ",	"--get-avail-freq", cmdGetAvailFreq, cbGetAvailFreq },
    { "GET_FREQ",	"--get-freq",	    cmdGetFreq,      cbGetFreq },
    { "GET_CUR_GOV",	"--get-cur-gov",    cmdGetCurGov,    cbGetCurGov },
    { "SET_MIN_FREQ",	"--set-min-freq",   cmdPrintOutput,  NULL },
    { "SET_MAX_FREQ",	"--set-max-freq",   cmdPrintOutput,  NULL },
    { "SET_GOV",	"--set-gov",        cmdPrintOutput,  NULL },
};

static ssize_t cmdSize = sizeof(Command_Map) / sizeof(Command_Map[0]);

/**
 * @brief Execute the CPU frequency script doing all the modifications
 *
 * @param cmd Command passed to the script to execute the change
 *
 * @param addArgV Additional script arguments appended at the end
 *
 * @return Returns true on success otherwise false is returned
 */
static bool execCPUFreqScriptEx(Script_CMDs_t cmd, strv_t addArgV)
{
    if (cmd < 0 || cmd >= cmdSize) {
	pluginflog("invalid command %i\n", cmd);
	return false;
    }

    Script_Data_t *script = ScriptData_new(FREQ_SCRIPT);
    script->runtime = 30;
    script->cbOutput = Command_Map[cmd].fp;
    script->info = Command_Map[cmd].name;
    script->cbResult = Command_Map[cmd].cb;
    strvAdd(script->argV, "--cpu-sys-path");
    strvAdd(script->argV, sysPath);
    strvAdd(script->argV, Command_Map[cmd].arg);
    strvAppend(script->argV, addArgV);

    int ret = Script_exec(script);
    if (!script->cbResult) Script_destroy(script);

    return ret;
}

void CPUfreq_init(const char *cpuSysPath, CPUfreq_initCB_t *cb)
{
    initCB = cb;

    sysPath = cpuSysPath ? ustrdup(cpuSysPath) : ustrdup(DEFAULT_SYS_PATH);
    if (!sysPath) {
	pluginflog("out of memory\n");
	goto ERROR;
    }

    struct stat sb;
    if (stat(sysPath, &sb) == -1) {
	pluginflog("invalid CPU-frequency /sys path %s\n", sysPath);
	goto ERROR;
    }

    /* get basic CPU list and spawn additional gather scripts in
     * callback */
    if (execCPUFreqScript(CMD_LIST_CPUS)) {
	pluginflog("unable to initialize CPUs\n");
	goto ERROR;
    }
    initFlags |= INIT_LIST_CPUS;

    return;

ERROR:
    initFailure = true;
    retInitResult();
}

void CPUfreq_finalize(void)
{
    ufree(sysPath);
    sysPath = NULL;
    ufree(cpus);
    cpus = NULL;
}

/**
 * @brief Map frequency range to an actual frequency
 *
 * @param idx Index in CPU array
 *
 * @param range Frequency range to map
 *
 * @return Returns the mapped frequency on success otherwise
 * 0 is returned
 */
static uint32_t mapFreqRange(int16_t idx, uint32_t range)
{
    if (!(range & FREQ_RANGE_FLAG)) {
	pluginflog("error: no range but normal frequency given\n");
	return 0;
    }

    /* calculate node dependent frequency */
    int numFreq = cpus[idx].numAvailFreq;

    switch (range) {
	case FREQ_RANGE_LOW: /* lowest available frequency */
	    return cpus[idx].availFreq[0];
	case FREQ_RANGE_MEDIUM: /* middle of available range */
	{
	    if (numFreq == 1) return cpus[idx].availFreq[0];
	    int f = (numFreq - 1) / 2;

	    return cpus[idx].availFreq[f];
	}
	case FREQ_RANGE_SEC_HIGH: /* second highest available frequency */
	    if (numFreq > 1) {
		return cpus[idx].availFreq[numFreq-2];
	    }
	    return cpus[idx].availFreq[numFreq-1];
	case FREQ_RANGE_HIGH: /* highest available frequency */
	    return cpus[idx].availFreq[numFreq-1];
	default:
	    pluginflog("unknown frequency range %u\n", range);
    }

    return 0;
}

/**
 * @brief Map given frequency to supported frequencies of a CPU
 *
 * @param idx Index in CPU array
 *
 * @param newFreq Frequency to map
 *
 * @return Returns a valid frequency for the selected CPU on success
 * otherwise 0 is returned
 * */
static uint32_t mapValidFrequencies(int16_t idx, uint32_t newFreq)
{
    if (newFreq < cpus[idx].availMinFreq) return cpus[idx].availMinFreq;
    if (newFreq > cpus[idx].availMaxFreq) return cpus[idx].availMaxFreq;

    for (uint32_t i = 0; i < cpus[idx].numAvailFreq; i++) {
	if (newFreq == cpus[idx].availFreq[i]) {
	    return newFreq;
	}
	if (newFreq < cpus[idx].availFreq[i]) {
	    return cpus[idx].availFreq[i];
	}
    }

    return 0;
}

/**
 * @brief Call a script to change the frequency of one or more CPUs
 *
 * @param set Set containing all CPUs where the frequency should be modified
 *
 * @param setSize Size of the CPU set
 *
 * @param newFreq Target frequency
 *
 * @param cmd Command passed to the script to execute the change
 *
 * @return Returns true on success otherwise false is returned
 */
static bool doSetFreq(PSCPU_set_t set, uint16_t setSize, uint32_t newFreq,
			 Script_CMDs_t cmd)
{
    /* remove CPUs which already have correct frequency set */
    for (uint16_t i = 0; i < numCPUs; i++) {
	if (!PSCPU_isSet(set, i)) continue;
	uint32_t curFreq = (cmd == CMD_SET_MIN_FREQ) ?
			    cpus[i].curMinFreq : cpus[i].curMaxFreq;
	if (curFreq == newFreq) {
	    PSCPU_clrCPU(set, i);
	}
    }
    if (!PSCPU_any(set, setSize)) {
	plugindbg(PLUGIN_LOG_FREQ, "all CPUs on requested frequency\n");
	return true;
    }

    strv_t argV = strvNew(NULL);
    if (!argV) {
	pluginflog("strNew() failed\n");
	return false;
    }

    /* add frequency as first additional argument */
    char buf[64];
    snprintf(buf, sizeof(buf), "%u", newFreq);
    strvAdd(argV, buf);

    /* add list of CPUs to change */
    strvAdd(argV, "--cpus");
    strvAdd(argV, PSCPU_print_part(set, numCPUs));

    int ret = execCPUFreqScriptEx(cmd, argV);
    strvDestroy(argV);

    if (ret) {
	pluginflog("unable to set maximum CPU frequency to %u\n", newFreq);
    } else {
	for (uint16_t i = 0; i < numCPUs; i++) {
	    if (!PSCPU_isSet(set, i)) continue;
	    if (cmd == CMD_SET_MIN_FREQ) {
		plugindbg(-1, "set CPU %i to minimum freq %u\n", i, newFreq);
		cpus[i].curMinFreq = newFreq;
	    } else {
		plugindbg(-1, "set CPU %i to maximum freq %u\n", i, newFreq);
		cpus[i].curMaxFreq = newFreq;
	    }
	}
    }

    return ret;
}

static bool CPUfreq_setFreq(PSCPU_set_t set, uint16_t setSize, uint32_t newFreq,
			    Script_CMDs_t cmd)
{
    if (!CPUfreq_isInitialized()) return false;
    if (setSize > numCPUs) setSize = numCPUs;

    PSCPU_set_t setFreq;
    PSCPU_copy(setFreq, set);

    /* no frequency range */
    if (!(newFreq & FREQ_RANGE_FLAG)) {
	/* single frequency for all CPUs */
	int16_t first = PSCPU_first(setFreq, setSize);
	if (first == -1) {
	    pluginflog("no CPU in set\n");
	    return false;
	}

	/* map user given frequency to CPU supported frequency */
	uint32_t mapFreq = mapValidFrequencies(first, newFreq);
	if (!mapFreq) {
	    pluginflog("invalid CPU frequency %u\n", newFreq);
	    return false;
	}

	plugindbg(PLUGIN_LOG_FREQ, "single frequency %u mapped %u "
		  "for all CPUs\n", newFreq, mapFreq);
	return doSetFreq(setFreq, setSize, mapFreq, cmd);
    }

    /* one frequency range for all CPUs */
    if (equalAvailFreq) {
	int16_t first = PSCPU_first(setFreq, setSize);
	if (first == -1) {
	    pluginflog("no CPU in set\n");
	    return false;
	}
	uint32_t mapFreq = mapFreqRange(first, newFreq);
	if (!mapFreq) {
	    pluginflog("invalid CPU frequency %u\n", newFreq);
	    return false;
	}
	plugindbg(PLUGIN_LOG_FREQ, "frequency range %u mapped %u for all CPUs\n",
		  mapFreq, mapFreq);

	return doSetFreq(setFreq, setSize, mapFreq, cmd);
    }

    /* every CPU might have a different frequency range */
    for (uint16_t i = 0; i < setSize; i++) {
	if (!PSCPU_isSet(setFreq, i)) continue;
	PSCPU_set_t nextSet;
	PSCPU_clrAll(nextSet);
	PSCPU_setCPU(nextSet, i);
	uint32_t nextFreq = mapFreqRange(i, newFreq);
	if (!nextFreq) {
	    pluginflog("invalid CPU frequency %u\n", newFreq);
	    return false;
	}

	/* find all CPUs with the same frequency */
	for (uint16_t x = i + 1; x < setSize; x++) {
	    if (!PSCPU_isSet(setFreq, x)) continue;
	    if (nextFreq != mapFreqRange(x, newFreq)) continue;
	    PSCPU_setCPU(nextSet, x);
	    PSCPU_clrCPU(setFreq, x);
	}

	plugindbg(PLUGIN_LOG_FREQ, "set CPUs %s to frequency %u\n",
		  PSCPU_print_part(nextSet, setSize), nextFreq);
	if (!doSetFreq(nextSet, setSize, nextFreq, cmd)) {
	    return false;
	}
    }

    return true;
}

bool CPUfreq_setMinFreq(PSCPU_set_t set, uint16_t setSize, uint32_t newFreq)
{
    return CPUfreq_setFreq(set, setSize, newFreq, CMD_SET_MIN_FREQ);
}

bool CPUfreq_setMaxFreq(PSCPU_set_t set, uint16_t setSize, uint32_t newFreq)
{
    return CPUfreq_setFreq(set, setSize, newFreq, CMD_SET_MAX_FREQ);
}

bool CPUfreq_setGov(PSCPU_set_t set, uint16_t setSize,
		    CPUfreq_governors_t newGov)
{
    if (!CPUfreq_isInitialized()) return false;
    if (setSize > numCPUs) setSize = numCPUs;

    /* ensure only one governor is given */
    if ((newGov & (newGov - 1)) != 0) {
	pluginflog("error: multiple governors are set: %i\n", newGov);
	return false;
    }

    PSCPU_set_t setGov;
    PSCPU_copy(setGov, set);

    /* remove CPUs which already have correct governor set */
    for (uint16_t i = 0; i < numCPUs; i++) {
	if (!PSCPU_isSet(setGov, i)) continue;
	if (cpus[i].curGov == newGov) {
	    PSCPU_clrCPU(setGov, i);
	    continue;
	}
	if (!(newGov & cpus[i].availGov)) {
	    pluginflog("erro: CPU %i does not support governor %s\n",
		       i, CPUfreq_gov2Str(newGov));
	    return false;
	}
    }

    if (!PSCPU_any(setGov, setSize)) {
	plugindbg(PLUGIN_LOG_FREQ, "all CPUs on requested governor %s\n",
		  CPUfreq_gov2Str(newGov));
	return true;
    }

    strv_t argV = strvNew(NULL);
    if (!argV) {
	pluginflog("strNew() failed\n");
	return false;
    }

    /* add governor string as first additional argument */
    char *strGov = CPUfreq_gov2Str(newGov);
    if (!strGov) {
	pluginlog("converting governor %i to string failed\n", newGov);
	strvDestroy(argV);
	return false;
    }
    strvAdd(argV, strGov);

    /* add list of CPUs to change */
    strvAdd(argV, "--cpus");
    strvAdd(argV, PSCPU_print_part(setGov, numCPUs));

    int ret = execCPUFreqScriptEx(CMD_SET_GOV, argV);
    strvDestroy(argV);

    if (ret) {
	pluginflog("unable to set CPU governor to %s\n", strGov);
    } else {
    for (uint16_t i = 0; i < numCPUs; i++) {
	if (!PSCPU_isSet(setGov, i)) continue;
	    plugindbg(PLUGIN_LOG_FREQ, "set CPU %i governor %s\n", i, strGov);
	    cpus[i].curGov = newGov;
	}
    }

    return true;
}

bool CPUfreq_resetGov(PSCPU_set_t set, uint16_t setSize)
{
    if (!CPUfreq_isInitialized()) return false;
    if (setSize > numCPUs) setSize = numCPUs;

    PSCPU_set_t setGov;
    PSCPU_copy(setGov, set);

    if (defaultGov != GOV_UNDEFINED) {
	/* set all selected CPUs to default governor */
	plugindbg(PLUGIN_LOG_FREQ, "set all CPUs to default governor\n");
	return CPUfreq_setGov(setGov, setSize, defaultGov);
    }

    /* different default governors, reset single CPUs */
    for (uint16_t i = 0; i < setSize; i++) {
	if (!PSCPU_isSet(setGov, i)) continue;
	PSCPU_set_t nextGov;
	PSCPU_clrAll(nextGov);
	PSCPU_setCPU(nextGov, i);

	/* find all CPUs with the same governor */
	for (uint16_t x = i + 1; x < setSize; x++) {
	    if (!PSCPU_isSet(setGov, x)) continue;
	    if (cpus[i].defGov != cpus[x].defGov) continue;
	    PSCPU_setCPU(nextGov, x);
	    PSCPU_clrCPU(setGov, x);
	}

	plugindbg(PLUGIN_LOG_FREQ, "set CPUs %s to governor %s\n",
		   PSCPU_print_part(nextGov, setSize),
		    CPUfreq_gov2Str(cpus[i].defGov));
	CPUfreq_setGov(nextGov, setSize, cpus[i].defGov);
    }

    return true;
}

bool CPUfreq_resetMinFreq(PSCPU_set_t set, uint16_t setSize)
{
    if (!CPUfreq_isInitialized()) return false;
    if (setSize > numCPUs) setSize = numCPUs;

    PSCPU_set_t setFreq;
    PSCPU_copy(setFreq, set);

    if (defaultMinFreq) {
	/* set all selected CPUs to default minimal frequency */
	plugindbg(PLUGIN_LOG_FREQ, "set all CPUs to default minimum"
		  " frequency %u\n", defaultMinFreq);
	return doSetFreq(setFreq, setSize, defaultMinFreq, CMD_SET_MIN_FREQ);
    }

    /* different default minimum frequencies, reset single CPUs */
    for (uint16_t i = 0; i < setSize; i++) {
	if (!PSCPU_isSet(setFreq, i)) continue;
	PSCPU_set_t nextSet;
	PSCPU_clrAll(nextSet);
	PSCPU_setCPU(nextSet, i);

	/* find all CPUs with the same frequency */
	for (uint16_t x = i + 1; x < setSize; x++) {
	    if (!PSCPU_isSet(setFreq, x)) continue;
	    if (cpus[i].defMinFreq != cpus[x].defMinFreq) continue;
	    PSCPU_setCPU(nextSet, x);
	    PSCPU_clrCPU(setFreq, x);
	}

	plugindbg(PLUGIN_LOG_FREQ, "set CPUs %s to defMinFreq %u\n",
		   PSCPU_print_part(nextSet, setSize), cpus[i].defMinFreq);
	doSetFreq(nextSet, setSize, cpus[i].defMinFreq, CMD_SET_MIN_FREQ);
    }

    return true;
}

bool CPUfreq_resetMaxFreq(PSCPU_set_t set, uint16_t setSize)
{
    if (!CPUfreq_isInitialized()) return false;
    if (setSize > numCPUs) setSize = numCPUs;

    PSCPU_set_t setFreq;
    PSCPU_copy(setFreq, set);

    if (defaultMaxFreq) {
	/* set all selected CPUs to maximum frequency */
	plugindbg(PLUGIN_LOG_FREQ, "set all CPUs to default maximum"
		  " frequency %u\n", defaultMaxFreq);
	return doSetFreq(setFreq, setSize, defaultMaxFreq, CMD_SET_MAX_FREQ);
    }

    /* different default maximum frequencies, reset single CPUs */
    for (uint16_t i = 0; i < setSize; i++) {
	if (!PSCPU_isSet(setFreq, i)) continue;
	PSCPU_set_t nextSet;
	PSCPU_clrAll(nextSet);
	PSCPU_setCPU(nextSet, i);

	/* find all CPUs with the same frequency */
	for (uint16_t x = i + 1; x < setSize; x++) {
	    if (!PSCPU_isSet(setFreq, x)) continue;
	    if (cpus[i].defMaxFreq != cpus[x].defMaxFreq) continue;
	    PSCPU_setCPU(nextSet, x);
	    PSCPU_clrCPU(setFreq, x);
	}

	plugindbg(PLUGIN_LOG_FREQ, "set CPUs %s to defMaxFreq %u\n",
		   PSCPU_print_part(nextSet, setSize), cpus[i].defMaxFreq);

	doSetFreq(nextSet, setSize, cpus[i].defMaxFreq, CMD_SET_MAX_FREQ);
    }

    return true;
}

bool CPUfreq_resetAll(void)
{
    if (!CPUfreq_isInitialized()) return false;

    PSCPU_set_t set;
    PSCPU_setAll(set);

    CPUfreq_resetGov(set, numCPUs);
    CPUfreq_resetMinFreq(set, numCPUs);
    CPUfreq_resetMaxFreq(set, numCPUs);

    return true;
}
