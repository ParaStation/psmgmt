/*
 *               ParaStation
 *
 * Copyright (C) 2007-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * pspmi.h: ParaStation pspmi protocol header
 *
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PSIDPMIPROTOCOL
#define __PSIDPMIPROTOCOL

#define PMI_VERSION 1
#define PMI_SUBVERSION 1
#define PMI_FINALIZED 55

#include "pslog.h"

/**
 * @brief Parse a pmi msg and call the appropriate protocol handler
 * function
 *
 * @param msg The pmi message to parse.
 *
 * @return Returns 0 for success, 1 on errors.
 */
int pmi_parse_msg(char *msg);

/**
 * @brief Init the PMI interface, this must be the first call before
 * everything else.
 *
 * @param pmisocket The socket witch is connect to the pmi client.
 *
 * @param loggertaskid The task id of the logger.
 *
 * @param pRank The rank of the pmi client.
 *
 * @return Returns 0 on success and 1 on errors.
 */
int pmi_init(int pmisocket, PStask_ID_t loggertaskid, int pRank);

/**
 * @brief Set predecessor task.
 *
 * Set the task ID of the current task's predecessor to @a pred. This
 * implicitely enables daisy-chain broadcasts within PMI, if the
 * successor process is disclosed at the same time.
 *
 * @param pred The task ID of the predecessor to be set.
 *
 * @return No return value.
 *
 * @see pmi_set_succ()
 */
void pmi_set_pred(PStask_ID_t pred);

/**
 * @brief Set successor task.
 *
 * Set the task ID of the current task's successor to @a succ. This
 * implicitely enables daisy-chain broadcasts within PMI, if the
 * predecessor process is disclosed at the same time.
 *
 * @param pred The task ID of the successor to be set.
 *
 * @return No return value.
 *
 * @see pmi_set_pred()
 */
void pmi_set_succ(PStask_ID_t succ);

/**
 * @brief Forward or handle a kvs msg from logger.
 *
 * @return No return value.
 */
void pmi_handleKvsRet(PSLog_Msg_t *msg);

/**
 * @brief Send finalize ack to the pmi client, this is called from the
 * forwarder if the deamon has released the pmi client. This message
 * allows the pmi client to exit.
 *
 * @return No return value.
 */
void pmi_finalize(void);

#endif
