/*
 *               ParaStation
 *
 * Copyright (C) 2006 Cluster Competence Center GmbH, Munich
 *
 * $Id: psidutil.h 3892 2006-01-09 18:29:54Z eicker $
 *
 */
/**
 * \file
 * Functions handling the communication hardware
 *
 * $Id: psidutil.h 3892 2006-01-09 18:29:54Z eicker $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDHW_H
#define __PSIDHW_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Init all communication hardware.
 *
 * Initialize all the configured communication hardware. Various
 * parameters have to be set before. This is usually done by reading
 * and parsing the configuration file within @ref
 * PSID_readConfigFile(). For further details take a look on the
 * source code.
 *
 * The actual initialization of the various hardware types defined is
 * done via calls to the PSID_startHW() function.
 *
 * @return No return value.
 *
 * @see PSID_readConfigFile(), PSID_startHW()
 */
void PSID_startAllHW(void);

/**
 * @brief Init distinct communciation hardware.
 *
 * Initialize the distinct communication hardware @a hw. @a hw is a
 * unique number describing the hardware and is defined from the
 * configuration file.
 *
 * @param hw A unique number of the communication hardware to start.
 *
 * @return No return value.
 */
void PSID_startHW(int hw);

/**
 * @brief Stop all communication hardware.
 *
 * Stop and bring down all the configured and initialized
 * communication hardware. Various parameters have to be set
 * before. This is usually done by reading and parsing the
 * configuration file within @ref PSID_readConfigFile(). For further
 * details take a look on the source code.
 *
 * The actual stopping of the various hardware types defined is done
 * via calls to the PSID_stopHW() function.
 *
 * @return No return value.
 *
 * @see PSID_readConfigFile(), PSID_stopHW()
 */
void PSID_stopAllHW(void);

/**
 * @brief Stop distinct communciation hardware.
 *
 * Stop and bring down the distinct communication hardware @a hw. @a
 * hw is a unique number describing the hardware and is defined from
 * the configuration file.
 *
 * @param hw A unique number of the communication hardware to start.
 *
 * @return No return value.
 */
void PSID_stopHW(int hw);

/**
 * @brief Get hardware counters.
 *
 * Read out the hardware counters of the hardware @a hw. The value of
 * the counter is determined via calling the script registered to this
 * hardware. The output of this script is stored to the buffer @a buf
 * with size @a size.
 *
 * Depending on the value of @a header, either a header line
 * describing the different values of the counter line is created (@a
 * header = 1) or the actual counter line is generated.
 *
 * @param hw The hardware type of the counters to read out.
 *
 * @param buf The buffer to store the headerline to.
 *
 * @param size The actual size of @a buf.
 *
 * @param header Flag marking, if a headerline or a counterline should
 * be generated.
 *
 * @return No return value.
 */
void PSID_getCounter(int hw, char *buf, size_t size, int header);

/**
 * @brief Set hardware parameter.
 *
 * Set parameter described by @a option of the hardware @a hw to @a value. 
 *
 * @param hw The hardware type the parameter is connected to.
 *
 * @param option The hardware parameter to be set.
 *
 * @param value The value to be set.
 *
 * @return No return value.
 */
void PSID_setParam(int hw, PSP_Option_t option, PSP_Optval_t value);

/**
 * @brief Get hardware parameter.
 *
 * Read out the parameter described by @a option of the hardware @a hw.
 *
 * @param hw The hardware type the parameter is connected to.
 *
 * @param option The hardware parameter to be read out.
 *
 * @return If no error occurred, the value of the parameter to read
 * out is returned. Otherwise -1 is returned.
 */
PSP_Optval_t PSID_getParam(int hw, PSP_Option_t option);

/**
 * @brief Handle PSP_CD_HWSTART message
 *
 * Handle the message @a msg of type PSP_CD_HWSTART.
 *
 * Start the communication hardware as described within @a msg. If
 * starting succeeded and the corresponding hardware was down before,
 * all other nodes are informed on the change hardware situation on
 * the local node.
 *
 * @param msg Pointer to message to handle.
 *
 * @return No return value.
 */
void msg_HWSTART(DDBufferMsg_t *msg);

/**
 * @brief Handle PSP_CD_HWSTOP message
 *
 * Handle the message @a msg of type PSP_CD_HWSTOP.
 *
 * Stop the communication hardware as described within @a msg. If
 * stopping succeeded and the corresponding hardware was up before,
 * all other nodes are informed on the change hardware situation on
 * the local node.
 *
 * @param msg Pointer to message to handle.
 *
 * @return No return value.
 */
void msg_HWSTOP(DDBufferMsg_t *msg);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDHW_H */
