/*
 *               ParaStation
 *
 * Copyright (C) 2008-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * Part of this code is based on examples with:
 * Copyright (c) 2005 Intel Corporation
 * All Rights Reserved
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#define _GNU_SOURCE
#include <unistd.h>
#include <sched.h>
#include <errno.h>

#include <sys/types.h>
#include <unistd.h>

#include "psidutil.h"


int PSID_GenuineIntel(void)
{
#if defined __i386__ || defined __x86_64__
    unsigned int Regebx = 0, Regedx = 0, Regecx = 0;
    char* IntelID = "GenuineIntel";

    asm (
	"xorl %%eax, %%eax\n\t"
	"cpuid\n\t"
	:       "=b" (Regebx),
		"=d" (Regedx),
		"=c" (Regecx)
	:
	: "%eax"
	);

    return (Regebx == *(unsigned int *)&IntelID[0]
	    && Regedx == *(unsigned int *)&IntelID[4]
	    && Regecx == *(unsigned int *)&IntelID[8]);
#else
    return 0;
#endif
}

#if defined __i386__ || defined __x86_64__
/**
 * @brief Get maximum argument of CPUID instruction
 *
 * Call CPUID instruction with argument 0. Upon return the eax
 * register will contain the maximum supported standard function.
 *
 * @return Content of eax register is returned. If CPUID instruction
 * is unavailable, 0 is returned.
 */
static unsigned int CpuIDSupported(void)
{
    unsigned int maxInputValue = 0;

    asm (
	"xorl %%eax,%%eax\n\t"
	"cpuid\n\t"
	: "=a" (maxInputValue)
	:
	: "%ebx", "%ecx", "%edx"
	);

    return maxInputValue;
}

/** EDX[28]  Bit 28 is set if HT or multi-core is supported */
#define HWD_MT_BIT         0x10000000

/**
 * @brief Test for hardware multi-threaded support.
 *
 * Determine, if the current CPU supports multi-threading. Therefore
 * the CPUID instructions is called with argument 1 and the relevant
 * bit is masked out from the result.
 *
 * The function returns 0 when the hardware multi-threaded bit is
 * *not* set.
 *
 * @return Returns 1 if the processor supports hardware
 * multi-threading, or otherwise 0.
 */
static unsigned int HWD_MTSupported(void)
{
    unsigned int Regedx = 0;

    if (!CpuIDSupported() || ! PSID_GenuineIntel()) return 0;

    asm (
	"movl $1,%%eax\n\t"
	"cpuid"
	: "=d" (Regedx)
	:
	: "%eax","%ebx","%ecx"
	);

    return (Regedx & HWD_MT_BIT);
}

/**
 * EAX[31:26] Bit 26-31 in eax contains the number of cores minus one
 * per physical processor when execute cpuid with eax set to 4.
 */
#define NUM_CORE_BITS 0xFC000000

/**
 * @brief Determine number of cores
 *
 * Determine the number of cores per physical package. Therefore the
 * CPUID instructions is called with argument 4 and the relevant bits
 * are extracted from the result.
 *
 * If the current CPU does not support multi-threadding, it is assumed
 * to have just one core.
 *
 * @return The number of cores per physical package is returned.
 */
static unsigned int corePerPhysical(void)
{
    unsigned int Regeax = 0;

    if (!HWD_MTSupported()) return 1;

    asm (
	"xorl %eax, %eax\n\t"
	"cpuid\n\t"
	"cmpl $4, %eax\n\t"	/* check if cpuid supports leaf 4 */
	"jl .single_core\n\t"	/* Single core */
	"movl $4, %eax\n\t"
	"movl $0, %ecx\n\t"	/* start with index = 0; Leaf 4 reports */
	);			/* at least one valid cache level */
    asm (
	"cpuid"
	: "=a" (Regeax)
	:
	: "%ebx", "%ecx", "%edx"
	);
    asm (
	"jmp .multi_core\n"
	".single_core:\n\t"
	"xor %eax, %eax\n"
	".multi_core:"
	);

    return ((Regeax & NUM_CORE_BITS) >> 26)+1;
}

/**
 * EBX[23:16] Bit 16-23 in ebx contains the number of logical
 * processors per physical processor when execute cpuid with eax set
 * to 1
 */
#define NUM_LOGICAL_BITS   0x00FF0000

/**
 * @brief Determine number of logical processors
 *
 * Determine the number of logical processors per physical
 * package. Therefore the CPUID instructions is called with argument 0
 * and the relevant bits are extracted from the result.
 *
 * If the current CPU does not support multi-threadding, it is assumed
 * to have just one logical processors.
 *
 * @return The number of cores per physical package is returned.
 */
static unsigned int logicalPerPhysical(void)
{
    unsigned int Regebx = 0;

    if (!HWD_MTSupported()) return 1;

    asm (
	"movl $1,%%eax\n\t"
	"cpuid"
	: "=b" (Regebx)
	:
	: "%eax","%ecx","%edx"
	);

    return ((Regebx & NUM_LOGICAL_BITS) >> 16);
}

/**
 * EBX[31:24] Bits 24-31 (8 bits) return the 8-bit unique initial APIC
 * ID for the processor this code is running on.
 */
#define INITIAL_APIC_ID_BITS  0xFF000000

/**
 * @brief Get APIC ID
 *
 * Get the APIC ID of the current CPU. Therefore the CPUID
 * instructions is called with argument 1 and the relevant bits are
 * extracted from the result.
 *
 * @return The current APIC ID.
 */
static unsigned char getAPIC_ID(void)
{
    unsigned int Regebx = 0;

    asm (
	"movl $1, %%eax\n\t"
	"cpuid"
	: "=b" (Regebx)
	:
	: "%eax","%ecx","%edx"
	);

    return (unsigned char) ((Regebx & INITIAL_APIC_ID_BITS) >> 24);
}

/**
 * @brief Determine bitfield-width
 *
 * Determine the width of the bit field that can represent the value
 * @a CountItem.
 *
 * @param CountItem The maximum value the bitfield is able to store.
 *
 * @return The determined bitfield-width.
 */
static unsigned int findMaskwidth(unsigned int CountItem)
{
    unsigned int MaskWidth = 0;

    if (CountItem) CountItem--;
    while (CountItem) {
	CountItem >>= 1;
	MaskWidth++;
    }

    return MaskWidth;
}


/**
 * @brief Extract subset from bit field
 *
 * Extract a trailing subset of bit field from the 8-bit value @a
 * full. The lenght of the subset is determined by the number of bits
 * needed to store the the value of @a maxVal. It returns a 8-bit
 * sub ID.
 *
 * @param full Bit field to extract a subset from
 *
 * @param maxVal Determines the number of bits to extract.
 *
 * @return The subset of length determined via @a maxVal is returned.
 */
static unsigned char getSubset(unsigned char full, unsigned char maxVal)
{
    unsigned int MaskWidth;
    unsigned char MaskBits;

    MaskWidth = findMaskwidth(maxVal);
    MaskBits  = 0xff ^ ((unsigned char) (0xff << (MaskWidth)));

    return (full & MaskBits);
}
#endif

long PSID_getPhysCPUs_IA32(void)
{
#if defined __i386__ || defined __x86_64__
    long virtCPUs = PSID_getVirtCPUs(), physCPUs = 0;
    cpu_set_t allowedCPUs, currentCPU;
    int i;

    if (!PSID_GenuineIntel()) {
	PSID_log(-1, "%s: Processor not 'GenuineIntel'!\n", __func__);
	return virtCPUs;
    }

    /* Backup affinity settings */
    sched_getaffinity(0, sizeof(allowedCPUs), &allowedCPUs);

    for (i=0; i < virtCPUs; i++) {
	CPU_ZERO(&currentCPU);
	CPU_SET(i, &currentCPU);
	if (!sched_setaffinity(0, sizeof(currentCPU), &currentCPU)) {
	    int logicalPerCore;
	    unsigned char apicID, smtID;
	    sleep(0); /* Ensure system to switch to the right CPU */

	    logicalPerCore = logicalPerPhysical() / corePerPhysical();

	    apicID = getAPIC_ID();
	    smtID = getSubset(apicID, logicalPerCore);

	    if (!smtID) physCPUs++;
	} else {
	    PSID_warn(-1, errno, "%s: sched_setaffinity(%d) failed",
		      __func__, i);
	}
    }

    /* restore the affinity setting to its original state */
    if (sched_setaffinity(0, sizeof(allowedCPUs), &allowedCPUs)) {
	PSID_warn(-1, errno, "%s: sched_setaffinity() failed", __func__);
    }
    sleep(0);

    return physCPUs;
#else
    return PSID_getVirtCPUs();
#endif
}
