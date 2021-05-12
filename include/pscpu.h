/*
 * ParaStation
 *
 * Copyright (C) 2007-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Small toolbox to handle CPU sets.
 */
#ifndef __PSCPU_H
#define __PSCPU_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/** Maximum number of CPUs per node. This has to be a multipe of 8 */
#define PSCPU_MAX 1024

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
 * Set the bit representing CPU @a cpu within the CPU-set @a
 * set.
 *
 * @attention Setting the corresponding bit might fail silently if @a
 * cpu is out of range, i.e. larger than PSCPU_MAX.
 *
 * @param set The CPU-set to manipulate
 *
 * @param cpu Number of the bit within @a set to set
 *
 * @return No return value
 */
static inline void PSCPU_setCPU(PSCPU_set_t set, uint16_t cpu)
{
    if (cpu < PSCPU_MAX) set[cpu/CPUmask_s] |= 1 << cpu%CPUmask_s;
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
static inline void PSCPU_addCPUs(PSCPU_set_t set, const PSCPU_set_t add)
{
    unsigned i;
    for (i=0; i < sizeof(PSCPU_set_t)/sizeof(PSCPU_mask_t); i++)
	set[i] |= add[i];
}

/**
 * @brief Clear bit in CPU-set
 *
 * Clear the bit representing CPU @a cpu within the CPU-set @a
 * set.
 *
 * @attention Clearing the corresponding bit might fail silently if @a
 * cpu is out of range, i.e. larger than PSCPU_MAX.
 *
 * @param set The CPU-set to manipulate
 *
 * @param cpu Number of the bit within @a set to clear
 *
 * @return No return value
 */
static inline void PSCPU_clrCPU(PSCPU_set_t set, uint16_t cpu)
{
    if (cpu < PSCPU_MAX) set[cpu/CPUmask_s] &= ~(1 << cpu%CPUmask_s);
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
static inline void PSCPU_remCPUs(PSCPU_set_t set, const PSCPU_set_t rem)
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
 * @return If the tested bit is set, true is returned. Or false otherwise.
 */
static inline bool PSCPU_isSet(const PSCPU_set_t set, uint16_t cpu)
{
    return (cpu < PSCPU_MAX) && (set[cpu/CPUmask_s] & 1<<cpu%CPUmask_s);
}

/**
 * @brief Test for CPU
 *
 * Test, if any CPU is defined within the CPU-set @a set. Only the
 * first @a numBits CPUs are actually investigated, all further ones
 * will be ignored.
 *
 * @param set The CPU-set to investigate
 *
 * @param numBits Number of bits to actualy investigate
 *
 * @return If any CPU is set, true is returned. Otherwise false is returned
 */
bool PSCPU_any(const PSCPU_set_t set, uint16_t numBits);

/**
 * @brief Test for CPUs
 *
 * Test, if any CPU is *not* defined within the CPU-set @a set.
 *
 * Only the first @a numBits CPUs are actually investigated, all
 * further ones will be ignored.
 *
 * @param set The CPU-set to investigate
 *
 * @param numBits Number of bits to actualy investigate
 *
 * @return If all CPU are set, true is returned. Otherwise false is returned
 */
bool PSCPU_all(const PSCPU_set_t set, uint16_t numBits);

/**
 * @brief Check for overlap between CPU-sets
 *
 * Check if the CPU-sets @a s1 and @a s2 have any common bit set in
 * the first @a numBits bits.
 *
 * @param s1 CPU-set to compare
 *
 * @param s2 CPU-set to compare
 *
 * @param numBits Number of bits to actualy investigate
 *
 * @return If any bit is set in both sets amongst the first @a
 * numBits bits, true is returned; or false otherwise
 */
bool PSCPU_overlap(const PSCPU_set_t set1, const PSCPU_set_t set2,
	           uint16_t numBits);

/**
 * @brief Check for disjointedness between CPU-sets
 *
 * Check if the CPU-sets @a s1 and @a s2 are disjoint within the first
 * @a numBits bits.
 *
 * @param s1 CPU-set to compare
 *
 * @param s2 CPU-set to compare
 *
 * @param numBits Number of bits to actualy investigate
 *
 * @return If both sets are disjoint amongst the first @a numBits
 * bits, true is returned; or false otherwise
 */
static inline bool PSCPU_disjoint(const PSCPU_set_t set1,
				  const PSCPU_set_t set2, uint16_t numBits)
{
    return ! PSCPU_overlap(set1, set2, numBits);
}

/**
 * @brief Get first CPU
 *
 * Get first CPU defined in the CPU-set @a set. This function is
 * mainly used to create messages needed for backward-compatibility.
 *
 * Actually not the whole CPU-set is investigated, but only the first
 * @a numBits slots.
 *
 * @param set The CPU-set to investigate
 *
 * @param numBits Number of bits to actualy investigate
 *
 * @return If a bit is set within @a set, the corresponding number is
 * returned. If no bit is set within the first @a numBits, -1 is
 * returned.
 */
int16_t PSCPU_first(const PSCPU_set_t set, uint16_t numBits);

/**
 * @brief Get CPUs from CPU-set
 *
 * Get up to @a num CPUs from the CPU-set @a origSet and store them to
 * the CPU-set @a newSet. While doing so @a origSet is left untouched.
 *
 * @param origSet The CPU-set to get CPUs from. Left untouched.
 *
 * @param newSet The CPU-set used to store the CPUs selected from @a
 * origSet
 *
 * @param num The number of CPUs to get.
 *
 * @return The number of CPUs got from @a origSet. This might be less
 * than what was requested via @a num.
 */
int PSCPU_getCPUs(const PSCPU_set_t origSet, PSCPU_set_t newSet, int16_t num);

/**
 * @brief Get CPU-set with unset CPUs
 *
 * Create a CPU-set from all the unset CPUs in @a set and store it in
 * @a free. Only the first @a numBits CPUs are actually investigated,
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
 * @param numBits Number of bits in @a set to actualy investigate
 *
 * @param free The CPU-set to store the results in
 *
 * @param tpp Possible 'threads per process'. The number of CPUs set
 * within @a free will be a multiple of this value.
 *
 * @return The number of CPUs set within @a free.
 */
int PSCPU_getUnset(const PSCPU_set_t set, uint16_t numBits,
		   PSCPU_set_t free, uint16_t tpp);


/**
 * @brief Print CPU-set
 *
 * Get a string describing the CPU-set @a set. The returned pointer
 * leads to a static character array that contains the
 * description. Sequent calls to @ref PSCPU_print() or @ref
 * PSCPU_print_part() will change the content of this array. Therefore
 * the result is not what you expect if more than one call of this
 * function is made within a single argument-list of printf(3) and
 * friends.
 *
 * @param set The CPU-set to describe
 *
 * @return A pointer to a static character array containing task ID's
 * description. Do not try to free(2) this array.
 */
char *PSCPU_print(const PSCPU_set_t set);

/**
 * @brief Print parts of CPU-set
 *
 * Get a string describing the last @a num bytes of CPU-set @a set. The
 * returned pointer leads to a static character array that contains
 * the description. Sequent calls to @ref PSCPU_print() or @ref
 * PSCPU_print_part() will change the content of this array. Therefore
 * the result is not what you expect if more than one call of this
 * function is made within a single argument-list of printf(3) and
 * friends.
 *
 * @attention The granularity of @a num is limited to multiples of
 * PSCPU_mask_t and therefore to two bytes.
 *
 * @param set CPU-set to describe
 *
 * @param num Number of bytes to describe
 *
 * @return A pointer to a static character array containing task ID's
 * description. Do not try to free(2) this array.
 */
char *PSCPU_print_part(const PSCPU_set_t set, size_t num);

/**
 * @brief Copy CPU-set
 *
 * Copy the content of CPU-set @a src into the CPU-set @a dest.
 *
 * @param dest The destination CPU-set
 *
 * @param src The CPU-set to copy
 *
 * @return No return value
 */
static inline void PSCPU_copy(PSCPU_set_t dest, const PSCPU_set_t src)
{
    if (src && dest) memcpy(dest, src, sizeof(PSCPU_set_t));
}

/**
 * @brief Determine number of bytes to be copied for part of CPU-set
 *
 * Determine the number of bytes to be copied out of a CPU-set in
 * order to get to information for @a num CPUs.
 *
 * If more than @ref PSCPU_MAX CPUs are requested, 0 is returned and
 * thus, the request is marked to be invalid.
 *
 * This function might be used to determine the last parameter of @ref
 * PSCPU_extract() and PSCPU_inject().
 *
 * @param num Number of CPUs to be copied
 *
 * @return The number of bytes required unless too many CPUs are
 * requested. In this case, 0 is returned.
 *
 * @see PSCPU_extract(), PSCPU_inject()
 */
static inline size_t PSCPU_bytesForCPUs(unsigned short num)
{
    size_t bytes = num ? (num-1)/8 + 1 : 0;

    if (num > PSCPU_MAX) bytes = 0;

    return bytes;
}

/**
 * @brief Extract parts of CPU-set
 *
 * Extract @a num bytes of CPU-set @a src into the buffer @a
 * dest. Extraction is done for full bytes only, i.e. always chunks of
 * 8 CPUs are copied.
 *
 * @ref PSCPU_bytesForCPUs() might be used in order to determine the
 * number of bytes required to extract a given number of CPUs.
 *
 * @param dest The destination buffer
 *
 * @param src The CPU-set to extract from
 *
 * @param num The number bytes to extract
 *
 * @return No return value
 *
 * @see PSCPU_bytesForCPUs()
 */
static inline void PSCPU_extract(void *dest, const PSCPU_set_t src, size_t num)
{
    if (num && src && dest) memcpy(dest, src, num);
}

/**
 * @brief Inject parts of CPU-set
 *
 * Inject @a num bytes stored in buffer @a src into the CPU-set @a
 * dest. Injection is done for full bytes only, i.e. always chunks of
 * 8 CPUs are copied.
 *
 * @ref PSCPU_bytesForCPUs() might be used in order to determine the
 * number of bytes required to inject a given number of CPUs.
 *
 * @param dest The destination CPU-set
 *
 * @param src The source buffer to inject
 *
 * @param num The number of bytes to inject
 *
 * @return No return value
 *
 * @see PSCPU_bytesForCPUs()
 */
static inline void PSCPU_inject(PSCPU_set_t dest, const void *src, size_t num)
{
    if (num && src && dest) memcpy(dest, src,
				   (num > PSCPU_MAX/8) ? PSCPU_MAX/8 : num);
}

/**
 * @brief Compare CPU-sets
 *
 * Compare the CPU-sets @a s1 and @a s2.
 *
 * @param s1 CPU-set to compare
 *
 * @param s2 CPU-set to compare
 *
 * @return This functions returns an integer less than, equal to, or
 * greater than zero depending on in which set the first CPU is found
 * to be set. I.e. 0 is returned if both CPU-sets found to be equal.
 */
static inline int PSCPU_cmp(const PSCPU_set_t s1, const PSCPU_set_t s2)
{
    if (s1 && s2) return memcmp(s1, s2, sizeof(PSCPU_set_t));

    return 0;
}

#undef CPUmask_s

#endif  /* __PSCPU_H */
