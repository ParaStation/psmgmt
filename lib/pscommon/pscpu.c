/*
 * ParaStation
 *
 * Copyright (C) 2007-2021 ParTec Cluster Competence Center GmbH, Munich
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

bool PSCPU_any(const PSCPU_set_t set, uint16_t numBits)
{
    unsigned int i;
    int16_t pCPUs = (numBits > PSCPU_MAX) ? PSCPU_MAX : numBits;
    for (i = 0; i < PSCPU_MAX/CPUmask_s && pCPUs > 0; i++, pCPUs -= CPUmask_s) {
	PSCPU_mask_t m = set[i];
	if (m && (pCPUs >= CPUmask_s || (m << (CPUmask_s - pCPUs)) != 0)) {
	    return true;
	}
    }

    return false;
}

bool PSCPU_all(const PSCPU_set_t set, uint16_t numBits)
{
    unsigned int i;
    int16_t pCPUs = (numBits > PSCPU_MAX) ? PSCPU_MAX : numBits;
    for (i = 0; i < PSCPU_MAX/CPUmask_s && pCPUs > 0; i++, pCPUs -= CPUmask_s) {
	PSCPU_mask_t m = ~set[i];
	if (m && (pCPUs >= CPUmask_s || (m << (CPUmask_s-pCPUs)) != 0)) {
	    return false;
	}
    }

    return true;
}

bool PSCPU_overlap(const PSCPU_set_t set1, const PSCPU_set_t set2,
		   uint16_t numBits)
{
    int16_t pCPUs = (numBits > PSCPU_MAX) ? PSCPU_MAX : numBits;
    for (uint16_t i = 0;
	 i < PSCPU_MAX/CPUmask_s && pCPUs > 0; i++, pCPUs -= CPUmask_s) {
	PSCPU_mask_t m1 = set1[i], m2 = set2[i];
	if ((m1 & m2)
	    && (pCPUs >= CPUmask_s
		|| ((m1 << (CPUmask_s-pCPUs)) & (m2 << (CPUmask_s-pCPUs))
		    & (PSCPU_mask_t)-1))) {
	    return true;
	}
    }
    return false;
}

int16_t PSCPU_first(const PSCPU_set_t set, uint16_t numBits)
{
    PSCPU_mask_t m=0;
    unsigned int i;
    int cpu=0;

    if (numBits > PSCPU_MAX) numBits = PSCPU_MAX;
    for (i = 0; i < PSCPU_MAX/CPUmask_s && cpu < numBits; i++) {
	m = set[i];
	if (m) break;
	cpu += CPUmask_s;
    }

    while (m && !(m & 1) && cpu < numBits) {
	cpu++;
	m >>= 1;
    }

    if (cpu >= numBits) cpu=-1;

    return cpu;
}

int PSCPU_getCPUs(const PSCPU_set_t origSet, PSCPU_set_t newSet, int16_t num)
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


int PSCPU_getUnset(const PSCPU_set_t set, uint16_t numBits,
		   PSCPU_set_t free, uint16_t tpp)
{
    int found=0;

    if (free) PSCPU_clrAll(free);
    /* Shortcut if numBits is unreasonably large */
    if (numBits > PSCPU_MAX) numBits = PSCPU_MAX;
    for (int16_t cpu = 0; cpu < numBits; cpu++) {
	if (!PSCPU_isSet(set, cpu)) {
	    if (free) PSCPU_setCPU(free, cpu);
	    found++;
	}
    }

    for (int16_t cpu = numBits - 1; found%tpp; cpu--) {
	if (free ? PSCPU_isSet(free, cpu) : true) {
	    found--;
	    if (free) PSCPU_clrCPU(free, cpu);
	}
    }

    return found;
}

char *PSCPU_print_part(const PSCPU_set_t set, size_t num)
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

char *PSCPU_print(const PSCPU_set_t set)
{
    return PSCPU_print_part(set, PSCPU_MAX/CPUmask_s);
}
