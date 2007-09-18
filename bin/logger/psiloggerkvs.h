/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * psiloggerkvs.h: ParaStation global key value space header 
 *
 * $Id$ 
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PSILOGGERKEYVALUESPACE
#define __PSILOGGERKEYVALUESPACE


/**
 * @brief Parse and handle a pmi kvs message.
 *
 * @param msg The received kvs msg to handle. 
 *
 * @return No return value.
 */
void handleKvsMsg(PSLog_Msg_t msg);


/**
 * @brief Init the global kvs. This function must be called bevor
 * calling @ref handleKvsMsg().
 *
 * @param verbose Set verbose mode of pmi.
 *
 * @return No return value.
 */
void initLoggerKvs(int verbose);

#endif
