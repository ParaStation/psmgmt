/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "plugingpufreq.h"

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

/* path to default GPU frequency script */
#define FREQ_SCRIPT PKGLIBEXECDIR "/gpufreq.py"

/* initial size of available frequency arrays */
#define START_FREQ_SIZE 256

/* additional allocated size if frequency array is grown */
#define GROW_FREQ_SIZE 32

/** holding frequency scaling information for a frequency type */
typedef struct {
    uint32_t *availFreq;	/* available frequency list */
    uint32_t availFreqNum;	/* number of available frequencies */
    uint32_t availFreqSize;	/* size of available frequencies */
    uint32_t defFreq;		/* default frequency */
    uint32_t minFreq;		/* minimum frequency */
    uint32_t maxFreq;		/* maximum frequency */
} Freq_Def_t;

/** holding supported frequency types */
typedef struct {
    Freq_Def_t gra;
    Freq_Def_t mem;
} GPUFreq_t;

/** used as an index for Command_Map */
typedef enum {
    CMD_LIST_GPUS = 0,
    CMD_GET_AVAIL_FREQ,
    CMD_GET_CUR_FREQ,
    CMD_SET_GRA_FREQ,
    CMD_SET_MEM_FREQ,
    CMD_RST_GRA_FREQ,
    CMD_RST_MEM_FREQ
} Script_CMDs_t;

/** map commands and arguments */
typedef struct {
    char *name;
    char *arg;
    void (*fp)(char *output, void *info);
    void (*cb)(int32_t status, Script_Data_t *script);
} Command_Map_t;

/** path to GPU frequency script */
static char *fScriptPath;

/** all GPUs including cached frequency values */
static GPUFreq_t *gpus;

/** number of GPUs which support frequency scaling */
static int numGPUs;

/** set to common default graphics frequency for all GPUs (if any)  */
static uint32_t defaultGraFreq;

/** set to common default memory frequency for all GPUs (if any)  */
static uint32_t defaultMemFreq;

/** flag to indicate if all GPUs have the same graphics frequencies */
static bool commonGraFreq = true;

/** flag to indicate if all GPUs have the same memory frequencies */
static bool commonMemFreq = true;

/** list of initialization flags */
typedef enum {
    INIT_LIST_GPUS	= 0x0001,
    INIT_GET_FREQ	= 0x0002,
    INIT_GET_AVAIL_FREQ	= 0x0004,
} Init_Flags_t;

/** used to track information gather scripts */
static Init_Flags_t initFlags;

/** indicate failure while initialization */
static bool initFailure;

/** callback to return result of the initialization */
static GPUfreq_initCB_t *initCB;

bool GPUfreq_isInitialized(void)
{
    return (gpus && !initFailure);
}

/**
 * @brief Cleanup on failure and return result via callback
 */
static void retInitResult()
{
    if (initFailure) GPUfreq_finalize();
    if (initCB) initCB(!initFailure);
}

/**
 * @brief Extract list of available GPUs
 *
 * @param output Script output holding GPU information
 *
 * @param info Script command name which was executed
 */
static void cmdListGPUs(char *output, void *info)
{
    int scriptGPUs;
    if (sscanf(output, "%i:", &scriptGPUs) != 1) {
	pluginflog("unknown number of GPUs : %s\n", output);
	initFailure = true;
	return;
    }

    /* TODO: compare GPU list to psid/hwloc information? */
    numGPUs = scriptGPUs;

    gpus = ucalloc(sizeof(*gpus) * numGPUs);

    /* initialize graphics frequencies */
    for (int i = 0; i < numGPUs; i++) {
	gpus[i].gra.availFreqSize = START_FREQ_SIZE;
	gpus[i].gra.availFreq =
	    umalloc(sizeof(*gpus[i].gra.availFreq) * START_FREQ_SIZE);
    }

    /* initialize memory frequencies */
    for (int i = 0; i < numGPUs; i++) {
	gpus[i].mem.availFreqSize = START_FREQ_SIZE;
	gpus[i].mem.availFreq =
	    umalloc(sizeof(*gpus[i].mem.availFreq) * START_FREQ_SIZE);
    }
}

/**
 * @brief Save additional frequency
 *
 * @param def Frequency array to save result in
 *
 * @param newFreq New frequency to save
 */
static void saveNewFreq(Freq_Def_t *def, uint32_t newFreq)
{
    uint32_t numF = def->availFreqNum;
    if (numF >= def->availFreqSize) {
	def->availFreqSize += GROW_FREQ_SIZE;
	def->availFreq = urealloc(def->availFreq,
	    sizeof(*def->availFreq) * def->availFreqSize);
    }

    if (!def->minFreq || newFreq < def->minFreq) {
	def->minFreq = newFreq;
    }
    if (!def->maxFreq || newFreq > def->maxFreq) {
	def->maxFreq = newFreq;
    }

    def->availFreq[numF] = newFreq;
    def->availFreqNum++;
}

/**
 * @brief Test if frequency already saved
 *
 * @param def Frequency array to search
 *
 * @param freq The frequency to find
 *
 * @return Returns true if frequency was found otherwise
 * false is returned
 */
static bool hasFreq(Freq_Def_t *def, uint32_t freq)
{
    for (uint32_t i = 0; i < def->availFreqNum; i++) {
	if (def->availFreq[i] == freq) return true;
    }
    return false;
}

/**
 * @brief Extract all available GPU frequencies
 *
 * All frequencies are red in MHz and consists of a pair of
 * graphics and memory frequency.
 *
 * @param output Script output holding frequency information
 *
 * @param info Script command name which was executed
 */
static void cmdGetAvailFreq(char *output, void *info)
{
    if (!GPUfreq_isInitialized()) return;

    int idx;
    uint32_t newGraFreq, newMemFreq;

    if (sscanf(output, "gpu %i graphics %u memory %u", &idx,
	       &newGraFreq, &newMemFreq) != 3) {
	pluginflog("no GPU frequencies in '%s'\n", output);
	initFailure = true;
	return;
    }

    if (idx < 0 || idx >= numGPUs) {
	pluginflog("invalid index %i #GPUs %u\n", idx, numGPUs);
	initFailure = true;
	return;
    }

    if (!hasFreq(&gpus[idx].gra, newGraFreq)) {
	saveNewFreq(&gpus[idx].gra, newGraFreq);
	plugindbg(PLUGIN_LOG_GPU, "gpu=%i freq num=%u gra=%u\n",
		  idx, gpus[idx].gra.availFreqNum, newGraFreq);
    }

    if (!hasFreq(&gpus[idx].mem, newMemFreq)) {
	saveNewFreq(&gpus[idx].mem, newMemFreq);
	plugindbg(PLUGIN_LOG_GPU, "gpu=%i freq num=%u mem=%u\n",
		  idx, gpus[idx].mem.availFreqNum, newMemFreq);
    }
}

/**
 * @brief Extract current graphics and memory GPU frequencies
 *
 * @param output Script output holding frequency information
 *
 * @param info Script command name which was executed
 */
static void cmdGetFreq(char *output, void *info)
{
    if (!GPUfreq_isInitialized()) return;

    int idx;
    uint32_t curGraFreq, curMemFreq;

    if (sscanf(output, "gpu %i graphics %u memory %u", &idx,
	       &curGraFreq, &curMemFreq) != 3) {
	pluginflog("no GPU frequencies in '%s'\n", output);
	initFailure = true;
	return;
    }

    if (idx < 0 || idx >= numGPUs) {
	pluginflog("invalid index %u\n", idx);
	initFailure = true;
	return;
    }

    plugindbg(PLUGIN_LOG_GPU, "gpu=%i cur-freq gra=%u mem=%u\n",
	      idx, curGraFreq, curMemFreq);

    gpus[idx].gra.defFreq = curGraFreq;
    gpus[idx].mem.defFreq = curMemFreq;
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
 * Test if all initialization scripts finished gathering GPU information
 * and invoke callback.
 *
 */
static void testInitComplete(void)
{
    if (initFlags) {
	plugindbg(PLUGIN_LOG_GPU, "gathering progress: 0x%x\n", initFlags);
	return;
    }

    if (pluginmset(PLUGIN_LOG_GPU)) {
	for (int i = 0; i < numGPUs; i++) {
	    pluginlog("gpu=%i # avail freq gra=%u mem=%u\n", i,
		      gpus[i].gra.availFreqNum, gpus[i].mem.availFreqNum);
	}
    }

    if (!numGPUs || initFailure) {
	pluginlog("GPUfreq initialization failed\n");
    } else {
	pluginflog("%i GPUs", numGPUs);
	if (defaultGraFreq) {
	    pluginlog(" def-graFreq '%u'", defaultGraFreq);
	}
	if (defaultMemFreq) {
	    pluginlog(" def-memFreq '%u'", defaultMemFreq);
	}
	if (commonGraFreq) {
	    pluginlog(", %u equal frequencies", gpus[0].gra.availFreqNum);
	}
	pluginlog("\n");

	if (!defaultGraFreq || !defaultMemFreq || !commonGraFreq ||
	    !commonMemFreq) {
	    pluginlog("warning: different GPU scaling settings can lead"
		       " to slower job launch times\n");
	}
    }

    retInitResult();
}

/* forward declaration */
static bool execGPUFreqScriptEx(Script_CMDs_t cmd, strv_t addArgV);

#define execGPUFreqScript(cmd) execGPUFreqScriptEx(cmd, NULL)

/**
 * @brief Callback for CMD_LIST_GPUS
 *
 * This initializes the main GPU data structures and
 * therefore has to be completed before any further data
 * gathering.
 */
static void cbListGPUs(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_LIST_GPUS;

    if (status) {
	pluginflog("unable to list GPUs (status %d)\n", status);
	goto ERROR;
    }

    if (execGPUFreqScript(CMD_GET_CUR_FREQ)) {
	pluginflog("failed getting current GPU frequencies\n");
	goto ERROR;
    }
    initFlags |= INIT_GET_FREQ;

    /* read list all of GPU frequencies */
    if (execGPUFreqScript(CMD_GET_AVAIL_FREQ)) {
	pluginflog("failed getting available GPU frequencies\n");
	goto ERROR;
    }
    initFlags |= INIT_GET_AVAIL_FREQ;

    Script_destroy(script);
    return;

ERROR:
    initFailure = true;
    retInitResult();
    Script_destroy(script);
}

/**
 * @brief qsort compare function
 */
static int compareFreq(const void *entry1, const void *entry2)
{
    uint32_t x = *(uint32_t *) entry1;
    uint32_t y = *(uint32_t *) entry2;
    return x - y;
}

/**
 * @brief Callback for CMD_GET_AVAIL_FREQ
 */
static void cbGetAvailFreq(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_GET_AVAIL_FREQ;

    if (status) {
	pluginflog("cannot determine available frequencies (status %d)\n",
		   status);
	initFailure = true;
    }

    /* sort red frequencies */
    for (int i = 0; i < numGPUs; i++) {
	qsort(gpus[i].gra.availFreq, gpus[i].gra.availFreqNum,
	      sizeof(gpus[i].gra.availFreq[0]), compareFreq);

	qsort(gpus[i].mem.availFreq, gpus[i].mem.availFreqNum,
	      sizeof(gpus[i].mem.availFreq[0]), compareFreq);
    }

    /* test if all GPUs have the same available frequencies */
    commonGraFreq = true;
    for (int c = 1; c < numGPUs; c++) {
	/* graphics frequencies */
	if (gpus[c].gra.availFreqNum != gpus[0].gra.availFreqNum) {
	    commonGraFreq = false;
	    break;
	}
	for (uint32_t i = 0; i < gpus[0].gra.availFreqNum; i++) {
	    if (gpus[0].gra.availFreq[i] != gpus[c].gra.availFreq[i]) {
		commonGraFreq = false;
		break;
	    }
	}
    }

    commonMemFreq = true;
    for (int c = 1; c < numGPUs; c++) {
	/* memory frequencies */
	if (gpus[c].mem.availFreqNum != gpus[0].mem.availFreqNum) {
	    commonMemFreq = false;
	    break;
	}
	for (uint32_t i = 0; i < gpus[0].mem.availFreqNum; i++) {
	    if (gpus[0].mem.availFreq[i] != gpus[c].mem.availFreq[i]) {
		commonMemFreq = false;
		break;
	    }
	}
    }

    testInitComplete();
    Script_destroy(script);
}

/**
 * @brief Save default frequencies (if any)
 */
static void saveDefFreq()
{
    /* test if all GPUs have the same frequencies */
    for (int i = 0; i < numGPUs; i++) {
	if (!defaultGraFreq) {
	    defaultGraFreq = gpus[i].gra.defFreq;
	    continue;
	}
	if (defaultGraFreq != gpus[i].gra.defFreq) {
	    defaultGraFreq = 0;
	    break;
	}
    }
    for (int i = 0; i < numGPUs; i++) {
	if (!defaultMemFreq) {
	    defaultMemFreq = gpus[i].mem.defFreq;
	    continue;
	}
	if (defaultMemFreq != gpus[i].mem.defFreq) {
	    defaultMemFreq = 0;
	    break;
	}
    }
}

/**
 * @brief Callback for CMD_GET_FREQ
 */
static void cbGetFreq(int32_t status, Script_Data_t *script)
{
    initFlags &= ~INIT_GET_FREQ;
    if (status) {
	pluginflog("cannot determine frequency (status %d)\n", status);
	initFailure = true;
    }

    /* test if all GPUs have the same default frequencies */
    saveDefFreq();

    testInitComplete();
    Script_destroy(script);
}

/**
 * @brief Cleanup script structure
 */
static void cbCleanup(int32_t status, Script_Data_t *script)
{
    if (status) {
	char *name = script->info;
	pluginflog("command %s failed (status %d)\n", name, status);
    }
    Script_destroy(script);
}

/* map containing description, argument and function for a command */
static Command_Map_t Command_Map[] = {
    { "LIST_GPUS",	"--list-gpus",      cmdListGPUs,     cbListGPUs },
    { "GET_AVAIL_FREQ",	"--get-avail-freq", cmdGetAvailFreq, cbGetAvailFreq },
    { "GET_CUR_FREQ",	"--get-freq",	    cmdGetFreq,      cbGetFreq },
    { "SET_GRA_FREQ",	"--set-gra-freq",   cmdPrintOutput,  cbCleanup },
    { "SET_MEM_FREQ",	"--set-mem-freq",   cmdPrintOutput,  cbCleanup },
    { "RST_GRA_FREQ",	"--reset-gra-freq", cmdPrintOutput,  cbCleanup },
    { "RST_MEM_FREQ",	"--reset-mem-freq", cmdPrintOutput,  cbCleanup },
};

static ssize_t cmdSize = sizeof(Command_Map) / sizeof(Command_Map[0]);

/**
 * @brief Execute the GPU frequency script doing all the modifications
 *
 * @param cmd Command passed to the script to execute the change
 *
 * @param addArgV Additional script arguments appended at the end
 *
 * @return Returns true on success otherwise false is returned
 */
static bool execGPUFreqScriptEx(Script_CMDs_t cmd, strv_t addArgV)
{
    if (cmd < 0 || cmd >= cmdSize) {
	pluginflog("invalid command %i\n", cmd);
	return false;
    }

    char *fPath = fScriptPath ? fScriptPath : DEFAULT_FREQ_SCRIPT;
    Script_Data_t *script = ScriptData_new(fPath);
    script->runtime = 30;
    script->cbOutput = Command_Map[cmd].fp;
    script->info = Command_Map[cmd].name;
    script->cbResult = Command_Map[cmd].cb;
    strvAdd(script->argV, Command_Map[cmd].arg);
    strvAppend(script->argV, addArgV);

    int ret = Script_exec(script);
    if (!script->cbResult) Script_destroy(script);

    return ret;
}

void GPUfreq_init(GPUfreq_initCB_t *cb)
{
    initCB = cb;
    initFailure = false;

    const char *fPath = freqScript ? freqScript : DEFAULT_FREQ_SCRIPT;
    if (access(fPath, R_OK | X_OK) < 0) {
	pluginflog("invalid GPU-frequency script path %s\n", fPath);
	goto ERROR;
    }

    fScriptPath = freqScript ? ustrdup(freqScript) : NULL;

    /* get basic GPU list and spawn additional gather scripts in
     * callback */
    if (execGPUFreqScript(CMD_LIST_GPUS)) {
	pluginflog("unable to initialize GPUs\n");
	goto ERROR;
    }
    initFlags |= INIT_LIST_GPUS;

    return;

ERROR:
    initFailure = true;
    retInitResult();
}

void GPUfreq_finalize(void)
{
    for (int i = 0; i < numGPUs; i++) {
	ufree(gpus[i].gra.availFreq);
	ufree(gpus[i].mem.availFreq);
    }

    ufree(gpus);
    gpus = NULL;

    ufree(fScriptPath);
    fScriptPath = NULL;
}

/**
 * @brief Map frequency range to an actual frequency
 *
 * A frequency range must have the GPU_FREQ_FLAG flag set so it can
 * be differentiated from a common GPU frequency. See @ref GPUfreq_range_t
 * for currently supported ranges. With this approach no knowledge about
 * the specific GPU capabilities is needed, since the range is mapped to
 * frequencies the underlying hardware supports.
 *
 * @param freq Frequency scaling definition
 *
 * @param range Frequency range to map
 *
 * @return Returns the mapped frequency on success otherwise
 * 0 is returned
 */
static uint32_t mapFreqRange(Freq_Def_t *freq, uint32_t range)
{
    if (!(range & GPU_FREQ_FLAG)) {
	pluginflog("error: no range but normal frequency given\n");
	return 0;
    }

    /* calculate node dependent frequency */
    int numFreq = freq->availFreqNum;

    switch (range) {
	case GPU_FREQ_LOW: /* lowest available frequency */
	    pluginfdbg(PLUGIN_LOG_GPU, "low frequency\n");
	    return freq->availFreq[0];
	case GPU_FREQ_MEDIUM: /* middle of available range */
	    pluginfdbg(PLUGIN_LOG_GPU, "medium frequency\n");
	    return freq->availFreq[(numFreq - 1) / 2];
	case GPU_FREQ_SEC_HIGH: /* second highest available frequency */
	    pluginfdbg(PLUGIN_LOG_GPU, "second high frequency\n");
	    if (numFreq > 1) {
		return freq->availFreq[numFreq-2];
	    }
	    return freq->availFreq[numFreq-1];
	case GPU_FREQ_HIGH: /* highest available frequency */
	    pluginfdbg(PLUGIN_LOG_GPU, "high frequency\n");
	    return freq->availFreq[numFreq-1];
	default:
	    pluginflog("unknown frequency range %u\n", range);
    }

    return 0;
}

/**
 * @brief Map given graphics frequency to supported frequencies of a GPU
 *
 * @param freq Frequency scaling definition
 *
 * @param newFreq New frequency to map
 *
 * @return Returns a valid frequency for the selected GPU on success
 * otherwise 0 is returned
 */
static uint32_t mapValidFreq(Freq_Def_t *freq, uint32_t newFreq)
{
    if (newFreq < freq->minFreq) return freq->minFreq;
    if (newFreq > freq->maxFreq) return freq->maxFreq;

    for (uint32_t i = 0; i < freq->availFreqNum; i++) {
	if (newFreq == freq->availFreq[i]) {
	    return newFreq;
	}
	if (newFreq < freq->availFreq[i]) {
	    return freq->availFreq[i];
	}
    }

    return 0;
}

/**
 * @brief Call a script to change the frequency of one or more GPUs
 *
 * @param set Set containing all GPUs where the frequency should be modified
 *
 * @param newFreq Target frequency to set
 *
 * @param cmd The script command to execute
 *
 * @return Returns true on success otherwise false is returned
 */
static bool doSetFreq(PSCPU_set_t set, uint32_t newFreq, int cmd)
{
    strv_t argV = strvNew(NULL);
    if (!argV) {
	pluginflog("strNew() failed\n");
	return false;
    }

    /* add frequencies as first additional argument */
    char buf[64];
    snprintf(buf, sizeof(buf), "%u", newFreq);
    strvAdd(argV, buf);

    /* add list of GPUs to change */
    strvAdd(argV, "--gpus");
    strvAdd(argV, PSCPU_print_part(set, numGPUs));

    int ret = execGPUFreqScriptEx(cmd, argV);
    strvDestroy(argV);

    if (ret) {
	pluginflog("unable to set new GPU frequencies to %u\n", newFreq);
	return false;
    }

    for (uint16_t i = 0; i < numGPUs; i++) {
	if (!PSCPU_isSet(set, i)) continue;
	plugindbg(PLUGIN_LOG_GPU, "set GPU %i to graphics freq %u\n", i,
		newFreq);
    }
    return true;
}

bool GPUfreq_setGraFreq(PSCPU_set_t set, uint16_t setSize, uint32_t graFreq)
{
    if (!GPUfreq_isInitialized()) return false;
    if (setSize > numGPUs) setSize = numGPUs;

    pluginfdbg(PLUGIN_LOG_GPU, "new graphics %u frequency\n", graFreq);

    PSCPU_set_t setFreq;
    PSCPU_copy(setFreq, set);

    /* no frequency range */
    if (!(graFreq & GPU_FREQ_FLAG)) {
	/* single frequency for all GPUs */
	int16_t first = PSCPU_first(setFreq, setSize);
	if (first == -1) {
	    pluginflog("no GPU in set\n");
	    return false;
	}

	/* map user given frequency to GPU supported frequency */
	uint32_t mapGraFreq = mapValidFreq(&gpus[first].gra, graFreq);
	if (!mapGraFreq) {
	    pluginflog("invalid GPU graphics frequency %u\n", graFreq);
	    return false;
	}

	plugindbg(PLUGIN_LOG_GPU, "single frequencies %u mapped %u "
		  "for all GPUs\n", graFreq, mapGraFreq);
	return doSetFreq(setFreq, mapGraFreq, CMD_SET_GRA_FREQ);
    }

    /* one frequency range for all GPUs */
    if (commonGraFreq) {
	int16_t first = PSCPU_first(setFreq, setSize);
	if (first == -1) {
	    pluginflog("no GPU in set\n");
	    return false;
	}
	uint32_t mapGraFreq = mapFreqRange(&gpus[first].gra, graFreq);
	if (!mapGraFreq) {
	    pluginflog("invalid GPU graphics frequency %u\n", graFreq);
	    return false;
	}

	plugindbg(PLUGIN_LOG_GPU, "frequency range %u mapped %u for all GPUs\n",
		  graFreq, mapGraFreq);

	return doSetFreq(setFreq, mapGraFreq, CMD_SET_GRA_FREQ);
    }

    /* every GPU might have a different frequency range */
    for (uint16_t i = 0; i < setSize; i++) {
	if (!PSCPU_isSet(setFreq, i)) continue;
	PSCPU_set_t nextSet;
	PSCPU_clrAll(nextSet);
	PSCPU_setCPU(nextSet, i);
	uint32_t nextGraFreq = mapFreqRange(&gpus[i].gra, graFreq);
	if (!nextGraFreq) {
	    pluginflog("invalid GPU frequency %u\n", graFreq);
	    return false;
	}

	/* find all GPUs with the same frequency */
	for (uint16_t x = i + 1; x < setSize; x++) {
	    if (!PSCPU_isSet(setFreq, x)) continue;
	    if (nextGraFreq != mapFreqRange(&gpus[x].gra, graFreq)) continue;
	    PSCPU_setCPU(nextSet, x);
	    PSCPU_clrCPU(setFreq, x);
	}

	plugindbg(PLUGIN_LOG_GPU, "set GPUs %s to frequency %u\n",
		  PSCPU_print_part(nextSet, setSize), nextGraFreq);
	if (!doSetFreq(nextSet, nextGraFreq, CMD_SET_GRA_FREQ)) {
	    return false;
	}
    }

    return true;
}

bool GPUfreq_setMemFreq(PSCPU_set_t set, uint16_t setSize, uint32_t memFreq)
{
    if (!GPUfreq_isInitialized()) return false;
    if (setSize > numGPUs) setSize = numGPUs;

    pluginfdbg(PLUGIN_LOG_GPU, "new memory %u frequencies\n", memFreq);

    PSCPU_set_t setFreq;
    PSCPU_copy(setFreq, set);

    /* no frequency range */
    if (!(memFreq & GPU_FREQ_FLAG)) {
	/* single frequency for all GPUs */
	int16_t first = PSCPU_first(setFreq, setSize);
	if (first == -1) {
	    pluginflog("no GPU in set\n");
	    return false;
	}

	/* map user given frequency to GPU supported frequency */
	uint32_t mapMemFreq = mapValidFreq(&gpus[first].mem, memFreq);
	if (!mapMemFreq) {
	    pluginflog("invalid GPU memory frequency %u\n", memFreq);
	    return false;
	}

	plugindbg(PLUGIN_LOG_GPU, "single frequencies %u mapped %u "
		  "for all GPUs\n", memFreq, mapMemFreq);
	return doSetFreq(setFreq, mapMemFreq, CMD_SET_MEM_FREQ);
    }

    /* one frequency range for all GPUs */
    if (commonMemFreq) {
	int16_t first = PSCPU_first(setFreq, setSize);
	if (first == -1) {
	    pluginflog("no GPU in set\n");
	    return false;
	}
	uint32_t mapMemFreq = mapFreqRange(&gpus[first].mem, memFreq);
	if (!mapMemFreq) {
	    pluginflog("invalid GPU memory frequency %u\n", memFreq);
	    return false;
	}
	plugindbg(PLUGIN_LOG_GPU, "frequency range %u mapped %u for all GPUs\n",
		  memFreq, mapMemFreq);

	return doSetFreq(setFreq, mapMemFreq, CMD_SET_MEM_FREQ);
    }

    /* every GPU might have a different frequency range */
    for (uint16_t i = 0; i < setSize; i++) {
	if (!PSCPU_isSet(setFreq, i)) continue;
	PSCPU_set_t nextSet;
	PSCPU_clrAll(nextSet);
	PSCPU_setCPU(nextSet, i);
	uint32_t nextMemFreq = mapFreqRange(&gpus[i].mem, memFreq);
	if (!nextMemFreq) {
	    pluginflog("invalid GPU frequency %u\n", memFreq);
	    return false;
	}

	/* find all GPUs with the same frequency */
	for (uint16_t x = i + 1; x < setSize; x++) {
	    if (!PSCPU_isSet(setFreq, x)) continue;
	    if (nextMemFreq != mapFreqRange(&gpus[x].mem, memFreq)) continue;
	    PSCPU_setCPU(nextSet, x);
	    PSCPU_clrCPU(setFreq, x);
	}

	plugindbg(PLUGIN_LOG_GPU, "set GPUs %s to frequency %u\n",
		  PSCPU_print_part(nextSet, setSize), nextMemFreq);
	if (!doSetFreq(nextSet, nextMemFreq, CMD_SET_MEM_FREQ)) {
	    return false;
	}
    }

    return true;
}

bool GPUfreq_resetFreq(PSCPU_set_t set, int freqType)
{
    pluginfdbg(PLUGIN_LOG_GPU, "on %s type %i\n",
	       PSCPU_print_part(set, numGPUs), freqType);
    if (!GPUfreq_isInitialized()) return false;

    int cmd = -1;
    switch (freqType) {
	case GPU_FREQ_TYPE_GRA:
	    cmd = CMD_RST_GRA_FREQ;
	    break;
	case GPU_FREQ_TYPE_MEM:
	    cmd = CMD_RST_MEM_FREQ;
	    break;
	default:
	    pluginflog("invalid frequency type %i\n", freqType);
	    return false;
    }

    strv_t argV = strvNew(NULL);
    if (!argV) {
	pluginflog("strNew() failed\n");
	return false;
    }

    /* add list of GPUs to change */
    strvAdd(argV, "--gpus");
    strvAdd(argV, PSCPU_print_part(set, numGPUs));

    int ret = execGPUFreqScriptEx(cmd, argV);
    strvDestroy(argV);

    if (ret) {
	pluginflog("unable to reset GPUs %s type %i",
		   PSCPU_print_part(set, numGPUs), freqType);
    } else {
	for (uint16_t i = 0; i < numGPUs; i++) {
	    if (!PSCPU_isSet(set, i)) continue;
	    if (freqType == GPU_FREQ_TYPE_GRA) {
		plugindbg(PLUGIN_LOG_GPU, "reset gra GPU %i to freq %u\n", i,
			  gpus[i].gra.defFreq);
	    } else {
		plugindbg(PLUGIN_LOG_GPU, "reset mem GPU %i to freq %u\n", i,
			  gpus[i].mem.defFreq);
	    }
	}
    }

    return true;
}
