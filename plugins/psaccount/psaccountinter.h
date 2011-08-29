/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PS_ACCOUNT_INTER
#define __PS_ACCOUNT_INTER

#include "psidcomm.h"
#include "psaccountclient.h"

#define PSP_CC_PLUGIN_ACCOUNT      0x0201  /**< psaccount plugin message */

typedef enum {
    PSP_ACCOUNT_FORWARD_START = 0x00000,    /**< req psmom version information */
    PSP_ACCOUNT_FORWARD_END,
    PSP_ACCOUNT_DATA_UPDATE,
} PSP_PSAccount_t;

extern int globalCollectMode;

/**
 * @brief Global message switch for inter account messages.
 *
 * @msg The message to handle.
 *
 * @return No return value.
 */
void handleInterAccount(DDTypedBufferMsg_t *msg);

/**
 * @brief Forward an accounting data record to another node.
 *
 * This function will forward received accounting messages
 * to the node were the logger is executed.
 *
 * @param msg The message to forward.
 *
 * @param type The type of the message to forward.
 *
 * @param logger The logger TaskID to forward this message to.
 *
 * @return No return value.
 */
void forwardAccountMsg(DDTypedBufferMsg_t *msg, int type, PStask_ID_t logger);

/**
 * @brief Experimental function.
 *
 * @return No return value.
 */
void sendAccountUpdate(Client_t *client);

/**
 * @brief Register a torque jobscript via its pid.
 *
 * This function is called by the psmom, because only
 * the psmom knows the pid of the jobscript. The psaccount
 * plugin is then able to identify all processes associated
 * with this jobscript.
 *
 * This enables the psmom the get valid accounting data for
 * all processes in the job, although it only knows the jobscript.
 *
 * @param jsPid The pid of the jobscript to register.
 *
 * @return No return value.
 */
void psAccountRegisterJobscript(pid_t jsPid);

/**
 * @brief Unregister a torque jobscript.
 *
 * The job has finished and the psmom is telling us to stop
 * accounting for this jobscript.
 *
 * @param jsPid The pid of the jobscript to un-register.
 *
 * @return No return value.
 */
void psAccountUnregisterJobscript(pid_t jsPid);

/**
 * @brief Enable the global collection of accounting data.
 *
 * This function is called by the psmom to enable
 * the global collection of accounting data. This way all
 * psaccount plugins will automatic forward all information
 * to the node were the logger is executed.
 *
 * @param active If flag is 1 the global collect mode is switched
 * on. If the flag is 0 it is swichted off.
 *
 * @return No return value.
 */
void psAccountSetGlobalCollect(int active);

/**
 * @brief Get account info for a jobscript.
 * */
int psAccountGetJobInfo(pid_t jobscript, psaccAccountInfo_t *accData);

/**
 * @brief Wrapper for the getSessionInformation() function.
 */
void psAccountGetSessionInfos(int *count, char *buf, size_t bufsize, int *userCount);

/**
 * @brief Wrapper for the sendSignal2Session() function.
 */
int psAccountsendSignal2Session(pid_t session, int sig);

/**
 * @brief Wrapper for the isChildofParent() function.
 */
void psAccountisChildofParent(pid_t parent, pid_t child);

/**
 * @brief Wrapper for the findDaemonProcesses() function.
 */
void psAccountFindDaemonProcs(uid_t uid, int kill, int warn);

#endif
