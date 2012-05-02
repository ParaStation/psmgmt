/*
 * ParaStation
 *
 * Copyright (C) 2007-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "pscpu.h"

/** Number of bits (i.e. CPUs) encoded within @ref PSCPU_mask_t. */
#define CPUmask_s (8*sizeof(PSCPU_mask_t))

int16_t PSCPU_any(PSCPU_set_t set, uint16_t physCPUs)
{
    int any=0;
    unsigned int i;

    for (i=0; i<PSCPU_MAX/CPUmask_s && physCPUs>0; i++, physCPUs-=CPUmask_s) {
	PSCPU_mask_t m = set[i];
	if (m && (physCPUs >= CPUmask_s || m << (CPUmask_s-physCPUs))) {
	    any=1;
	    break;
	}
    }

    return any;
}

int16_t PSCPU_all(PSCPU_set_t set, uint16_t physCPUs)
{
    int all=1;
    unsigned int i;

    for (i=0; i<PSCPU_MAX/CPUmask_s && physCPUs>0; i++, physCPUs-=CPUmask_s) {
	PSCPU_mask_t m = ~set[i];
	if (m && (physCPUs >= CPUmask_s || m << (CPUmask_s-physCPUs))) {
	    all=0;
	    break;
	}
    }

    return all;
}

int16_t PSCPU_first(PSCPU_set_t set, uint16_t physCPUs)
{
    PSCPU_mask_t m=0;
    unsigned int i;
    int cpu=0;

    for (i=0; i<PSCPU_MAX/CPUmask_s && cpu<physCPUs; i++) {
	m = set[i];
	if (m) break;
	cpu += CPUmask_s;
    }


    while (m && !(m & 1) && cpu<physCPUs) {
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
    for (cpu=0; cpu < PSCPU_MAX && found < num; cpu++) {
	if (PSCPU_isSet(origSet, cpu)) {
	    if (newSet) PSCPU_setCPU(newSet, cpu);
	    found++;
	}
    }

    return found;
}


int PSCPU_getUnset(PSCPU_set_t set, int16_t physCPUs,
		   PSCPU_set_t free, int16_t tpp)
{
    int16_t cpu;
    int found=0;

    if (free) PSCPU_clrAll(free);
    for (cpu=0; cpu < physCPUs; cpu++) {
	if (!PSCPU_isSet(set, cpu)) {
	    if (free) PSCPU_setCPU(free, cpu);
	    found++;
	}
    }

    for (cpu=physCPUs-1; found%tpp; cpu--) {
	if (free ? PSCPU_isSet(free, cpu) : 1) {
	    found--;
	    if (free) PSCPU_clrCPU(free, cpu);
	}
    }

    return found;
}

char *PSCPU_print(PSCPU_set_t set)
{
    static char setStr[PSCPU_MAX/4+10];
    unsigned int i;

    snprintf(setStr, sizeof(setStr), "0x");
    for (i=0; i<PSCPU_MAX/CPUmask_s; i++) {
	snprintf(setStr+strlen(setStr), sizeof(setStr)-strlen(setStr),
		 "%hx", set[PSCPU_MAX/CPUmask_s-1-i]);
    }

    return setStr;
}
