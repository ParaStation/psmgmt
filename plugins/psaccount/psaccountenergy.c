/*
 * ParaStation
 *
 * Copyright (C) 2019-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountenergy.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "psprotocol.h"
#include "psserial.h"
#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"

#include "psaccountconfig.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountscript.h"

#define NO_VAL64   (0xfffffffffffffffe)

/** power unit multiplier */
static float powerMult = 0;

/** energy state data */
static psAccountEnergy_t eData;

/** energy monitor script */
static Collect_Script_t *eScript = NULL;

/** script poll interval in seconds */
static int pollTime = 0;

/** additional script environment */
static env_t scriptEnv;

static uint64_t readEnergyFile(char *path)
{
    FILE *fd = fopen(path, "r");

    if (!fd) {
	mwarn(errno, "%s: fopen(%s) failed : ", __func__, path);
	return NO_VAL64;
    }

    uint64_t val;
    if (fscanf(fd, "%"PRIu64, &val) != 1) {
	mwarn(errno, "%s: fscanf(%s) failed : ", __func__, path);
	val = NO_VAL64;
    }

    fclose(fd);
    return val;
}

static void updateEnergy(uint64_t energy)
{
    eData.energyCur = energy;
    if (!eData.energyBase) eData.energyBase = energy;
    eData.lastUpdate = time(0);
    mdbg(PSACC_LOG_ENERGY, "%s: energy base: %zu consumed: %zu "
	    "(joules)\n", __func__, energy, energy - eData.energyBase);
}

static void updatePower(uint64_t power)
{
    static uint32_t readCount = 0;
    /* convert to watt  */
    eData.powerCur = (uint64_t) floor(power * powerMult);
    if (eData.powerCur < eData.powerMin) {
	eData.powerMin = eData.powerCur;
    }
    if (eData.powerCur > eData.powerMax) {
	eData.powerMax = eData.powerCur;
    }
    eData.powerAvg = ((eData.powerAvg * readCount) + eData.powerCur) /
	(readCount +1);
    readCount++;
    mdbg(PSACC_LOG_ENERGY, "%s: power cur: %u avg: %u min: %u "
	    "max: %u (watt)\n", __func__, eData.powerCur, eData.powerAvg,
	    eData.powerMin, eData.powerMax);
    eData.lastUpdate = time(0);
}

static void parseEnergy(char *data)
{
    unsigned long long power, energy;

    if (sscanf(data, "power:%llu energy:%llu", &power, &energy) != 2) {
	flog("parsing energy data '%s' from script failed\n", data);
	return;
    }

    updateEnergy(energy);
    updatePower(power);
}

static bool initPowerUnit(void)
{
    char *powerUnit = getConfValueC(&config, "POWER_UNIT");
    if (!powerUnit || powerUnit[0] == '\0') {
	flog("empty config parameter POWER_UNIT\n");
	return false;
    }

    if (!strcmp(powerUnit, "mW") || !strcmp(powerUnit, "Milliwatt")) {
	powerMult = 0.001;
    } else if (!strcmp(powerUnit, "W") || !strcmp(powerUnit, "Watt")) {
	powerMult = 1;
    } else if (!strcmp(powerUnit, "kW") || !strcmp(powerUnit, "Kilowatt")) {
	powerMult = 1000;
    } else if (!strcmp(powerUnit, "MW") || !strcmp(powerUnit, "Megawatt")) {
	powerMult = 1000 * 1000;
    } else {
	flog("parsing config parameter POWER_UNIT '%s' failed\n", powerUnit);
	return false;
    }
    return true;
}

bool Energy_startScript(void)
{
    if (eScript) return true;

    if (pollTime < 1) pollTime = 30;

    char *energyScript = getConfValueC(&config, "ENERGY_SCRIPT");
    eScript = Script_start("psaccount-energy", energyScript, parseEnergy,
			   pollTime, &scriptEnv);
    if (!eScript) {
	flog("invalid energy script, cannot continue\n");
	return false;
    }
    fdbg(PSACC_LOG_ENERGY, "energy monitor %s interval %i started\n",
	 energyScript, pollTime);

    return true;
}

bool Energy_init(void)
{
    if (!initPowerUnit()) return false;

    memset(&eData, 0, sizeof(eData));
    envInit(&scriptEnv);

    /* start forwarder to execute energy collect script */
    char *energyScript = getConfValueC(&config, "ENERGY_SCRIPT");
    if (energyScript && energyScript[0] != '\0') {
	char *energyPath = getConfValueC(&config, "ENERGY_PATH");
	if (energyPath && energyPath[0] != '\0') {
	    flog("error: ENERGY_SCRIPT and ENERGY_PATH are mutual exclusive\n");
	    return false;
	}

	char *powerPath = getConfValueC(&config, "POWER_PATH");
	if (powerPath && powerPath[0] != '\0') {
	    flog("error: ENERGY_SCRIPT and POWER_PATH are mutual exclusive\n");
	    return false;
	}

    }

    if (!Energy_update()) return false;
    eData.powerMin = eData.powerMax = eData.powerCur;

    return true;
}

void Energy_finalize(void)
{
    if (eScript) Script_finalize(eScript);
    eScript = NULL;
}

bool Energy_update(void)
{
    bool ret = true;

    /* using energy script to update */
    if (eScript) return true;

    /* update energy */
    char *energyPath = getConfValueC(&config, "ENERGY_PATH");
    if (energyPath && energyPath[0] != '\0') {
	uint64_t energy = energyPath ? readEnergyFile(energyPath) : NO_VAL64;
	if (energy != NO_VAL64) {
	    updateEnergy(energy);
	} else {
	    flog("no energy data from %s\n", energyPath);
	    ret = false;
	}
    }

    /* update power */
    char *powerPath = getConfValueC(&config, "POWER_PATH");
    if (powerPath && powerPath[0] != '\0') {
	uint64_t power = powerPath ? readEnergyFile(powerPath) : NO_VAL64;
	if (power != NO_VAL64) {
	    updatePower(power);
	} else {
	    flog("no power data from %s\n", powerPath);
	    ret = false;
	}
    }

    return ret;
}

psAccountEnergy_t *Energy_getData(void)
{
    return &eData;
}

bool Energy_setPoll(uint32_t poll)
{
    pollTime = poll;
    if (eScript) return Script_setPollTime(eScript, poll);
    return true;
}

uint32_t Energy_getPoll(void)
{
    return pollTime;
}

bool Energy_ctlEnv(psAccountCtl_t action, const char *envStr)
{
    switch (action) {
	case PSACCOUNT_SCRIPT_ENV_SET:
	    envPut(&scriptEnv, envStr);
	    break;
	case PSACCOUNT_SCRIPT_ENV_UNSET:
	    envUnset(&scriptEnv, envStr);
	    break;
	default:
	    flog("invalid action %i\n", action);
	    return false;
    }

    if (eScript) return Script_ctlEnv(eScript, action, envStr);
    return true;
}
