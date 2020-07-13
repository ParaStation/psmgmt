/*
 * ParaStation
 *
 * Copyright (C) 2019-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "psaccountenergy.h"
#include "psaccountproc.h"
#include "psaccountlog.h"
#include "psaccountconfig.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "pslog.h"
#include "psserial.h"
#include "pscommon.h"

#define NO_VAL64   (0xfffffffffffffffe)

static float powerMult = 0;

static psAccountEnergy_t eData;

/** forwarder data of the energy monitor script */
static Forwarder_Data_t *eScriptFw = NULL;

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

static void parseEnergy(char *ptr)
{
    unsigned long long power, energy;

    /* read message */
    char *data = getStringM(&ptr);

    if (sscanf(data, "power:%llu energy:%llu", &power, &energy) != 2) {
	mlog("%s: parsing energy data '%s' from script failed\n",
	     __func__, data);
	ufree(data);
	return;
    }
    ufree(data);

    updateEnergy(energy);
    updatePower(power);
}

static int handleFwMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    switch (msg->type) {
	case STDOUT:
	    parseEnergy(msg->buf);
	    break;
	case STDERR:
	    mlog("%s: error from energy script: %s\n", __func__, msg->buf);
	    break;
	default:
	    mlog("%s: unhandled msg type %d\n", __func__, msg->type);
	    return 0;
    }

    return 1;
}

static void execEnergyScript(Forwarder_Data_t *fwdata, int rerun)
{
    /* start forwarder to execute energy collect script */
    char *energyScript = getConfValueC(&config, "ENERGY_SCRIPT");
    int poll = getConfValueU(&config, "ENERGY_SCRIPT_POLL");

    while(true) {
	pid_t child = fork();
	if (child < 0) {
	    mlog("%s: fork() failed\n", __func__);
	    exit(1);
	}

	if (!child) {
	    /* This is the child */
	    char *argv[2] = { energyScript, NULL };
	    execvp(argv[0], argv);
	    /* never be here */
	    exit(1);
	}

	while (true) {
	    int status;
	    if (waitpid(child, &status, 0) < 0) {
		if (errno == EINTR) continue;
		mlog("%s: parent kill() errno: %i\n", __func__, errno);
		killpg(child, SIGKILL);
		exit(1);
	    }
	    break;
	}

	sleep(poll);
    }
}

static bool execEnergyFw(void)
{
    char jobid[] = "energy", fname[] = "psacc-energy";

    if (eScriptFw) {
	mlog("%s: error forwarder already running?\n", __func__);
    }

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(jobid);
    fwdata->graceTime = 1;
    fwdata->killSession = signalSession;
    fwdata->handleFwMsg = handleFwMsg;
    fwdata->childFunc = execEnergyScript;
    fwdata->fwChildOE = true;

    if (!startForwarder(fwdata)) {
	mlog("%s: starting energy script forwarder failed\n", __func__);
	return false;
    }
    eScriptFw = fwdata;
    return true;
}

static bool testEnergyScript(char *energyScript)
{
    if (energyScript) {
	struct stat sbuf;
	if (stat(energyScript, &sbuf) == -1) {
	    mwarn(errno, "%s: energy script %s not found:",
		  __func__, energyScript);
	    return false;
	}
	if (!(sbuf.st_mode & S_IFREG) || !(sbuf.st_mode & S_IXUSR)) {
	    mlog("%s: energy script %s is not a valid executable script\n",
		 __func__, energyScript);
	    return false;
	}
	return true;
    }
    return false;
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

	if (testEnergyScript(energyScript)) {
	    if (!execEnergyFw()) return false;
	} else {
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
    if (eScriptFw) {
	shutdownForwarder(eScriptFw);
    }
}

bool energyUpdate(void)
{
    bool ret = true;

    /* using energy script to update */
    if (eScriptFw) return true;

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
