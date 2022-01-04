/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __ps_mom_interactive
#define __ps_mom_interactive

#include <stddef.h>

#include "psmomcomm.h"
#include "psmomforwarder.h"
#include "psmomjob.h"

/**
 * @brief Initialize a connection to waiting qsub.
 *
 * Connect to waiting qsub and do the first steps to initialize the
 * conneciton.
 *
 * @param job The job structure for the interactive job.
 *
 * @param data Interactive data structure where all important connection
 * information will be saved in.
 *
 * @param term The file descriptor which will be connected to the local
 * terminal.
 *
 * @return On success a communication handle connected to qsub is returned,
 * on error NULL is returned.
 */
ComHandle_t *initQsubConnection(Job_t *job, Inter_Data_t *data, int term);

/**
 * @brief Start forwarding data between qsub and the local terminal.
 *
 * @return No return value.
 */
void enableQsubConnection(void);

/**
 * @brief Close all connections between qsub and the local terminal.
 *
 * @return No return value.
 */
void closeQsubConnection(void);

/**
 * @brief Write a message to qsub.
 *
 * @param msg The buffer which holds the message to write.
 *
 * @param len The size of the message to write.
 *
 * @param return Returns the number of bytes written or -1 on error.
 */
int writeQsubMessage(char *msg, size_t len);

void handle_Local_Qsub_Out(ComHandle_t *com);

int setWindowSize(char *windowSize, int pty);
int setTermOptions(char *termOpt, int pty);
int setSocketNonBlocking(int socket);
int initX11Forwarding1(Job_t *job, int *xport);
int initX11Forwarding2(Job_t *job, char *x11Cookie, int port,
    char *display, size_t displaySize);
ComHandle_t *getQsubConnection(Job_t *job);

#endif
