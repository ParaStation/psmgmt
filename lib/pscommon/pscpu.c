/*
 * ParaStation
 *
 * Copyright (C) 2007-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "pscpu.h"

/** Number of bits (i.e. CPUs) encoded within @ref PSCPU_mask_t. */
#define CPUmask_s (int16_t)(8*sizeof(PSCPU_mask_t))

bool PSCPU_any(PSCPU_set_t set, uint16_t physCPUs)
{
    unsigned int i;
    int16_t pCPUs = (physCPUs > PSCPU_MAX) ? PSCPU_MAX : physCPUs;
    for (i = 0; i < PSCPU_MAX/CPUmask_s && pCPUs > 0; i++, pCPUs -= CPUmask_s) {
	PSCPU_mask_t m = set[i];
	if (m && (pCPUs >= CPUmask_s || (m << (CPUmask_s - pCPUs)) != 0)) {
	    return true;
	}
    }

    return false;
}

bool PSCPU_all(PSCPU_set_t set, uint16_t physCPUs)
{
    unsigned int i;
    int16_t pCPUs = (physCPUs > PSCPU_MAX) ? PSCPU_MAX : physCPUs;
    for (i = 0; i < PSCPU_MAX/CPUmask_s && pCPUs > 0; i++, pCPUs -= CPUmask_s) {
	PSCPU_mask_t m = ~set[i];
	if (m && (pCPUs >= CPUmask_s || (m << (CPUmask_s-pCPUs)) != 0)) {
	    return false;
	}
    }

    return true;
}

int16_t PSCPU_first(PSCPU_set_t set, uint16_t physCPUs)
{
    PSCPU_mask_t m=0;
    unsigned int i;
    int cpu=0;

    if (physCPUs > PSCPU_MAX) physCPUs = PSCPU_MAX;
    for (i = 0; i < PSCPU_MAX/CPUmask_s && cpu < physCPUs; i++) {
	m = set[i];
	if (m) break;
	cpu += CPUmask_s;
    }

    while (m && !(m & 1) && cpu < physCPUs) {
	cpu++;
	m >>= 1;
    }

    if (cpu >= physCPUs) cpu=-1;

    return cpu;
}

int PSCPU_getCPUs(PSCPU_set_t origSet, PSCPU_set_t newSet, int16_t num)
{
    unsigned int cpu;
    int found=0;

    if (newSet) PSCPU_clrAll(newSet);
    for (cpu = 0; cpu < PSCPU_MAX && found < num; cpu++) {
	if (PSCPU_isSet(origSet, cpu)) {
	    if (newSet) PSCPU_setCPU(newSet, cpu);
	    found++;
	}
    }

    return found;
}


int PSCPU_getUnset(PSCPU_set_t set, uint16_t physCPUs,
		   PSCPU_set_t free, uint16_t tpp)
{
    int16_t cpu;
    int found=0;

    if (free) PSCPU_clrAll(free);
    /* Shortcut if physCPUs is unreasonably large */
    if (physCPUs > PSCPU_MAX) physCPUs = PSCPU_MAX;
    for (cpu = 0; cpu < physCPUs; cpu++) {
	if (!PSCPU_isSet(set, cpu)) {
	    if (free) PSCPU_setCPU(free, cpu);
	    found++;
	}
    }

    for (cpu = physCPUs - 1; found%tpp; cpu--) {
	if (free ? PSCPU_isSet(free, cpu) : true) {
	    found--;
	    if (free) PSCPU_clrCPU(free, cpu);
	}
    }

    return found;
}

char *PSCPU_print_part(PSCPU_set_t set, size_t num)
{
    static char setStr[PSCPU_MAX/4+10];
    unsigned int i;

    if (num > PSCPU_MAX/8) num = PSCPU_MAX/8;
    snprintf(setStr, sizeof(setStr), "0x");
    for (i = (num+1)/sizeof(PSCPU_mask_t); i > 0; i--) {
	snprintf(setStr + strlen(setStr), sizeof(setStr) - strlen(setStr),
		 "%0*hx", (int)sizeof(PSCPU_mask_t) * 2, set[i-1]);
    }

    return setStr;
}

char *PSCPU_print(PSCPU_set_t set)
{
    return PSCPU_print_part(set, PSCPU_MAX/CPUmask_s);
}
