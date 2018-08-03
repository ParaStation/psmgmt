/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSMUNGE_INTER
#define __PSMUNGE_INTER

#include <stdbool.h>

#include "psmungetypes.h"

psMungeEncode_t psMungeEncode;
psMungeDecode_t psMungeDecode;
psMungeDecodeBuf_t psMungeDecodeBuf;
psMungeMeasure_t psMungeMeasure;

/**
 * @brief Initialize munge facility
 *
 * Initialize the plugin's munge facility. This will try to fetch the
 * default credential for encoding and decoding and double check if
 * munge is actually working.
 *
 * @Return On success true is returned. Or false in case of an error
 */
bool initMunge(void);

/**
 * @brief Finalize munge facility
 *
 * Finalize the plugin's munge facility. This includes freeing all
 * contexts and the related memory.
 *
 * @Return No return value
 */
void finalizeMunge(void);

#endif  /* __PSMUNGE_INTER */
