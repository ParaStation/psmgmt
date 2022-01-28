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

static float powerMult = 0;

static psAccountEnergy_t eData;

/** energy monitor script */
static Collect_Script_t *eScript = NULL;

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
	mlog("%s: parsing energy data '%s' from script failed\n",
	     __func__, data);
	return;
    }

    updateEnergy(energy);
    updatePower(power);
}

static bool initPowerUnit(void)
{
    char *powerUnit = getConfValueC(&config, "POWER_UNIT");
    if (!powerUnit || powerUnit[0] == '\0') {
	mlog("%s: empty config parameter POWER_UNIT\n", __func__);
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
	mlog("%s: parsing config parameter POWER_UNIT '%s' failed\n", __func__,
	     powerUnit);
	return false;
    }
    return true;
}

bool energyInit(void)
{
    if (!initPowerUnit()) return false;

    memset(&eData, 0, sizeof(eData));

    /* start forwarder to execute energy collect script */
    char *energyScript = getConfValueC(&config, "ENERGY_SCRIPT");
    if (energyScript && energyScript[0] != '\0') {
	char *energyPath = getConfValueC(&config, "ENERGY_PATH");
	if (energyPath && energyPath[0] != '\0') {
	    mlog("%s: error: ENERGY_SCRIPT and ENERGY_PATH are mutual "
		 "exclusive\n", __func__);
	    return false;
	}

	char *powerPath = getConfValueC(&config, "POWER_PATH");
	if (powerPath && powerPath[0] != '\0') {
	    mlog("%s: error: ENERGY_SCRIPT and POWER_PATH are mutual "
		 "exclusive\n", __func__);
	    return false;
	}

	int poll = getConfValueU(&config, "ENERGY_SCRIPT_POLL");
	eScript = Script_start("energy", energyScript, parseEnergy, poll);
	if (!eScript) {
	    mlog("%s: invalid energy script, cannot continue\n", __func__);
	    return false;
	}
    }

    if (!energyUpdate()) return false;
    eData.powerMin = eData.powerMax = eData.powerCur;

    return true;
}

void energyFinalize(void)
{
    if (eScript) Script_finalize(eScript);
}

bool energyUpdate(void)
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
	    mlog("%s: no energy data from %s\n", __func__, energyPath);
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
	    mlog("%s: no power data from %s\n", __func__, powerPath);
	    ret = false;
	}
    }

    return ret;
}

psAccountEnergy_t *energyGetData(void)
{
    return &eData;
}
