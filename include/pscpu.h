/*
 * ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Small toolbox to handle CPU sets.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSCPU_H
#define __PSCPU_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Maximum number of CPUs per node. This has to be a multipe of 8 */
#define PSCPU_MAX 32

/** Mask used to encode  */
typedef uint16_t PSCPU_mask_t;

/**
 * Number of bits (i.e. CPUs) encoded within @ref
 * PSCPU_mask_t. Internal use only.
 */
#define CPUmask_s (8*sizeof(PSCPU_mask_t))

/**
 * Some masks bound together to store info on @ref PSCPU_MAX
 * CPUs. This is what this module is all about.
 */
typedef PSCPU_mask_t PSCPU_set_t[PSCPU_MAX/CPUmask_s];


/**
 * @brief Set bit in CPU-set
 *
 * Set the bit representing CPU @a cpu within the CPU-set @a set.
 *
 * @param set The CPU-set to manipulate
 *
 * @param cpu Number of the bit within @a set to set.
 *
 * @return No return value
 */
static inline void PSCPU_setCPU(PSCPU_set_t set, int16_t cpu)
{
    set[cpu/CPUmask_s] |= 1 << cpu%CPUmask_s;
}

/**
 * @brief Set all bits in CPU-set
 *
 * Set all bits within the CPU-set @a set.
 *
 * @param set The CPU-set to manipulate
 *
 * @return No return value
 */
static inline void PSCPU_setAll(PSCPU_set_t set)
{
    unsigned i;
    for (i=0; i < sizeof(PSCPU_set_t)/sizeof(PSCPU_mask_t); i++) set[i] = -1;
}

/**
 * @brief Join some bits into CPU-set.
 *
 * Join some bits described by the CPU-set @a add into the CPU-set
 * @a set.
 *
 * @param set The CPU-set to manipulate
 *
 * @param add The CPU-set to join
 *
 * @return No return value
 */
static inline void PSCPU_addCPUs(PSCPU_set_t set, PSCPU_set_t add)
{
    unsigned i;
    for (i=0; i < sizeof(PSCPU_set_t)/sizeof(PSCPU_mask_t); i++)
	set[i] |= add[i];
}

/**
 * @brief Clear bit in CPU-set
 *
 * Clear the bit representing CPU @a cpu within the CPU-set @a set.
 *
 * @param set The CPU-set to manipulate
 *
 * @param cpu Number of the bit within @a set to clear
 *
 * @return No return value
 */
static inline void PSCPU_clrCPU(PSCPU_set_t set, int16_t cpu)
{
    set[cpu/CPUmask_s] &= ~(1 << cpu%CPUmask_s);
}

/**
 * @brief Clear all bits in CPU-set
 *
 * Clear all bits within the CPU-set @a set.
 *
 * @param set The CPU-set to manipulate
 *
 * @return No return value
 */
static inline void PSCPU_clrAll(PSCPU_set_t set)
{
    unsigned i;
    for (i=0; i < sizeof(PSCPU_set_t)/sizeof(PSCPU_mask_t); i++) set[i] = 0;
}

/**
 * @brief Remove some bits from CPU-set
 *
 * Remove some bits described by the CPU-set @a rem from the CPU-set
 * @a set.
 *
 * @param set The CPU-set to manipulate
 *
 * @param add The CPU-set to remove
 *
 * @return No return value
 */
static inline void PSCPU_remCPUs(PSCPU_set_t set, PSCPU_set_t rem)
{
    unsigned i;
    for (i=0; i < sizeof(PSCPU_set_t)/sizeof(PSCPU_mask_t); i++)
	set[i] &= ~rem[i];
}

/**
 * @brief Test for bit set in CPU-set
 *
 * Test if the bit representing CPU @a cpu is set within the CPU-set
 * @a set.
 *
 * @param set The CPU-set to test
 *
 * @param cpu The number of the CPU to test
 *
 * @return If the tested bit is set, 1 is returned. Or 0 otherwise.
 */
static inline int PSCPU_isSet(PSCPU_set_t set, int16_t cpu)
{
    return set[cpu/CPUmask_s] & 1<<cpu%CPUmask_s;
}

/**
 * @brief Test for CPU
 *
 * Test, if any CPU is defined within the CPU-set @a set. Only the
 * first @a physCPUs CPUs are actually investigated, all further ones
 * will be ignored.
 *
 * @param set The CPU-set to investigate
 *
 * @param physCPUs The number of CPUs actualy to investigate.
 *
 * @return If any CPU is set, 1 is returned. Otherwise 0 is returned.
 */
int16_t PSCPU_any(PSCPU_set_t set, uint16_t physCPUs);

/**
 * @brief Test for CPUs
 *
 * Test, if any CPU is *not* defined within the CPU-set @a set.
 *
 * Only the first @a physCPUs CPUs are actually investigated, all
 * further ones will be ignored.
 *
 * @param set The CPU-set to investigate
 *
 * @param physCPUs The number of CPUs actualy to investigate.
 *
 * @return If all CPU are set, 1 is returned. Otherwise 0 is returned.
 */
int16_t PSCPU_all(PSCPU_set_t set, uint16_t physCPUs);

/**
 * @brief Get first CPU
 *
 * Get first CPU defined in the CPU-set @a set. This function is
 * mainly used to create messages needed for backward-compatibility.
 *
 * Actually not the whole CPU-set is investigated, but only the first
 * @a physCPUs slots.
 *
 * @param set The CPU-set to investigate
 *
 * @param physCPUs The number of CPUs actualy to investigate.
 *
 * @return If a CPU defined within @a set is found, the corresponding
 * number is returned. If no CPU is defined within the first @a
 * physCPUs, -1 is returned.
 */
int16_t PSCPU_first(PSCPU_set_t set, uint16_t physCPUs);

/**
 * @brief Get CPUs from CPU-set
 *
 * Get up to @a num CPUs from the CPU-set @a origSet and store them to
 * the CPU-set @a newSet. While doing so @a origSet is left untouched.
 *
 * @param origSet The CPU-set to get CPUs from. Left untouched.
 *
 * @param newSet The CPU-set used to store the CPUs selected from @a
 * origSet.
 *
 * @param num The number of CPUs to get.
 *
 * @return The number of CPUs got from @a origSet. This might be less
 * than what was requested via @a num.
 */
int PSCPU_getCPUs(PSCPU_set_t origSet, PSCPU_set_t newSet, int16_t num);

/**
 * @brief Get CPU-set with unset CPUs
 *
 * Create a CPU-set from all the unset CPUs in @a set and store it in
 * @a free. Only the first @a physCPUs CPUs are actually investigated,
 * all further ones will be ignored.
 *
 * If @a free is NULL, no manipulation is done but the number of free
 * CPUs within @a set is returned as if @a free would be there.
 *
 * The number of free CPUs that will be put into the CPU-set @a free
 * is a multiple of @a tpp. This enables the use of the set for
 * multi-threaded applications. I.e. if @a tpp is set to 1, all free
 * CPUs in @a set will be used. Otherwise some CPUs within @a set
 * might remain free. Nevertheless @a set will not be changed.
 *
 * @param set The CPU-set to take the free CPUs from
 *
 * @param physCPUs The number of CPUs in @a set actualy to
 * investigate.
 *
 * @param free The CPU-set to store the results in.
 *
 * @param tpp Possible 'threads per process'. The number of CPUs set
 * within @a free will be a multiple of this value.
 *
 * @return The number of CPUs set within @a free.
 */
int PSCPU_getUnset(PSCPU_set_t set, int16_t physCPUs,
		   PSCPU_set_t free, int16_t tpp);


/**
 * @brief Print CPU-set
 *
 * Get a string describing the CPU-set @a set. The returned pointer
 * leads to a static character array that contains the
 * description. Sequent calls to @ref PSCPU_print() will change the
 * content of this array. Therefore the result is not what you expect
 * if more than one call of this function is made within a single
 * argument-list of printf(3) and friends.
 *
 * @param tid The task ID to describe.
 *
 * @param set The CPU-set to describe
 *
 * @return A pointer to a static character array containing task ID's
 * description. Do not try to free(2) this array.
 */
char *PSCPU_print(PSCPU_set_t set);

/**
 * @brief Copy CPU-set
 *
 * Copy the content of CPU-set @a src into the CPU-set @a dest.
 *
 * @param dest The destination CPU-set.
 *
 * @param src The CPU-set to copy
 *
 * @return No return value
 */
static inline void PSCPU_copy(PSCPU_set_t dest, PSCPU_set_t src)
{
    if (src && dest) memcpy(dest, src, sizeof(dest));
}


#undef CPUmask_s
				 
#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSCPU_H */
