/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_INTERCONN
#define __PS_ACCOUNT_INTERCONN

/**
 * @brief Initialize interconnect monitoring
 *
 * @return Returns true on success or false otherwise
 */
bool IC_Init(void);

/**
 * @brief Finalize interconnect monitoring
 */
void IC_Finalize(void);

/**
 * @brief Get current interconnect data
 *
 * @return Returns a pointer to the current interconnect data
 */
psAccountIC_t *IC_getData(void);

/**
 * @brief Set interconnect poll time
 *
 * @param poll The new poll time in seconds
 *
 * @return Returns true on success otherwise false is returned
 */
bool IC_setPoll(uint32_t poll);

/**
 * @brief Get interconnect poll time
 *
 * @param Returns the current poll time in seconds
 */
uint32_t IC_getPoll(void);

#endif  /* __PS_ACCOUNT_INTERCONN */
