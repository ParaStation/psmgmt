/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSHOSTLIST_H
#define __PSHOSTLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Create list from range
 *
 * Create a comma-separated list of entries defined by @a range. @a
 * range is a describing string of the form
 * "<first>[-<last>]". <first> must be smaller or equal to <last>. The
 * list is appended to the dynamical character array @a list is
 * pointing to using @ref str2Buf(). @a size points to the initial
 * size of this array. Each entry appended to @a list is preceded by
 * @a prefix. @a count will return the number of entries appended to
 * @a list.
 *
 * If <first> contains leading 0's each entry will contain the same
 * number of digits as <first>.
 *
 * @param prefix Prefix repented to each entry created
 *
 * @param range Range describing entries created "<first>[-<last>]"
 *
 * @param list Character array holding the entries after return
 *
 * @param size Size of @a list
 *
 * @param count Number of entries created
 *
 * @return Upon success true is returned. Or false in case of an
 * error. In the latter case @a list and @a size remain untouched.
 */
bool range2List(char *prefix, char *range, char **list, size_t *size,
		uint32_t *count);

/**
 * @brief Expand hostlist
 *
 * Expand the hostlist given in @a hostlist by replacing ranges by a
 * comma-separated list of simple hosts. In order to expand a range
 * @ref range2List() is used. The expanded hostlist is stored into a
 * new dynamical character array that shall be free()ed after use. @a
 * count will provide the total number of entries of the hostlist
 * after expansion.
 *
 * @param hostlist Hostlist to expand
 *
 * @param count Total number of entries created
 *
 * @return Upon success the expanded, comma-separated hostlist is
 * returned as a dynamical character array. Otherwise NULL is
 * returned.
 */
char *expandHostList(char *hostlist, uint32_t *count);

#endif   /* __PSHOSTLIST_H */
