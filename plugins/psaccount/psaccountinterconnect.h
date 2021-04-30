/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
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
bool InterconnInit(void);

/**
 * @brief Finalize interconnect monitoring
 */
void InterconnFinalize(void);

#endif  /* __PS_ACCOUNT_INTERCONN */
