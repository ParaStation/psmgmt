/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Hardware handling
 */
#ifndef _HARDWARE_H_
#define _HARDWARE_H_

/**
 * @brief Create new hardware type.
 *
 * Create the new hardware type @a name. If a hardware type with name
 * @a name already exists, nothing is done. The hardware is registered
 * with an unique positiv index which is returned. It might be looked
 * up later using the HW_index() function.
 *
 * @param name Name of the new hardware type.
 *
 * @return If a hardware type named @a name already exists or an error
 * occured, -1 is returned. Otherwise the index of the new hardware
 * type is given.
 *
 * @see HW_index()
 */
int HW_add(const char *name);

/**
 * @brief Get the number of registered hardware types.
 *
 * Get the number of hardware types already registered using call to HW_add().
 *
 * @return The number of registered hardware types is returned.
 *
 * @see HW_add()
 */
int HW_num(void);

/**
 * @brief Get hardware's name.
 *
 * Get the unique name of the hardware type with index @a idx. The
 * hardware type has to be registered using HW_add() before. The index
 * @a idx used to identfy the hardware type is the one returned by the
 * HW_add() function while registering the type or might be looked up
 * via HW_index() later.
 *
 * @param idx The unique index of the hardware type.
 *
 * @return If a hardware type with index @a idx is registered, a
 * pointer to the corresponding name is returned. Otherwise NULL is
 * returned.
 *
 * @see HW_add(), HW_index()
 */
char *HW_name(const int idx);

/**
 * @brief Get hardware's index.
 *
 * Get the unique index of the hardwaretype with name @a name.
 *
 * @param name The name of the hardware type to lookup.
 *
 * @return If the hardware type was found, the corresponding index is
 * returned. Otherwise -1 is returned.
 */
int HW_index(const char *name);

/** Various script types associated with the hardware */
#define HW_STARTER	"HW_Starter" /**< Script used to start the hardware */
#define HW_STOPPER	"HW_Stopper" /**< Script used to stop the hardware */
#define HW_SETUP	"HW_Setup"   /**< Script used to setup the hardware */
#define HW_HEADERLINE	"HW_Header"  /**< Script used to get counter-names */
#define HW_COUNTER	"HW_Counter" /**< Script used to read counters */

/**
 * @brief Register a hardware's script.
 *
 * Register the script @a script to the hardware type with index @a
 * idx to be used as type @a type. @a type has to be one of @ref
 * HW_STARTER, @ref HW_STOPPER, @ref HW_SETUP, @ref HW_HEADERLINE or
 * @ref HW_COUNTER.
 *
 * @param idx The unique index of the hardware type the script should
 * be registered to.
 *
 * @param type The role the script should play for the hardware type.
 *
 * @param script The actual script to register.
 *
 * @return On succes, 1 is returned, or 0 if an error occured.
 */
int HW_setScript(const int idx, const char *type, const char *script);

/**
 * @brief Get a hardware's script.
 *
 * Get the script with type @a type from the hardware type with index
 * @a idx. @a type has to be one of @ref HW_STARTER, @ref HW_STOPPER,
 * @ref HW_SETUP, @ref HW_HEADERLINE or @ref HW_COUNTER.
 *
 * @param idx The unique index of the hardware type to lookup.
 *
 * @param type The script role within the hardware type.
 *
 * @return On success, i.e. if a hardware type with index @a idx is
 * registered and has a script with role @a type attached, the
 * corresponding script is returned. Otherwise NULL is returned.
 */
char *HW_getScript(const int idx, const char *type);

/**
 * @brief Register a hardware's environment.
 *
 * Register the environment variable @a name to the hardware type with
 * index @a idx to have the value @a val.
 *
 * @param idx The unique index of the hardware type the environment
 * variable should be registered to.
 *
 * @param name The name of the environment variable.
 *
 * @param val The actual value to register.
 *
 * @return On succes, 1 is returned, or 0 if an error occured.
 */
int HW_setEnv(const int idx, const char *name, const char *val);

/**
 * @brief Get a hardware's environment.
 *
 * Get the environment with name @a name from the hardware type with index
 * @a idx.
 *
 * @param idx The unique index of the hardware type to lookup.
 *
 * @param name The environments name within the hardware type.
 *
 * @return On success, i.e. if a hardware type with index @a idx is
 * registered and has an environment variable with name @a name
 * attached, the corresponding value is returned. Otherwise NULL is
 * returned.
 */
char *HW_getEnv(const int idx, const char *name);

/**
 * @brief Get the environments size.
 *
 * Get the number of environment variables registered to the hardware
 * type with index @a idx.
 *
 * @param idx The index of the hardware type to lookup.
 *
 * @return If a hardware type with index @a idx is registered, the
 * number of attached environment variables is returned, or 0
 * otherwise.
 *
 */
 int HW_getEnvSize(const int idx);

/**
 * @brief Dump an environment.
 *
 * Dump the @a num'th environment variable associated with the
 * hardware type with index @a idx. This function is mainly used for
 * putting whole hardware environments into a processes real
 * environment via successiv calls of putenv().
 *
 * @param idx The index of the hardware type to lookup
 *
 * @param num The running number of the environment variable to dump.
 *
 * @return If @a idx is valid and an environment variable is stored
 * under the running number @a num, a string of the format
 * 'name=value' is returned. Otherwise NULL is returned.
 */
char *HW_dumpEnv(const int idx, const int num);

/**
 * @brief Get string describing the hardware-type.
 *
 * Get a string describing the hardware-type @a hwType. The returned
 * pointer leads to a static character array that contains the
 * description. Sequent calls to @ref HW_printType() will change the
 * content of this array. Therefore the result is not what you expect
 * if more then one call of this function is made within a single
 * argument-list of printf(3) and friends.
 *
 * @param hwType The hardware-type to describe. This is supposed to be
 * a bitwise-or of the (1<<idx), where @a idx are the indeces of the
 * various hardware types registered.
 *
 * @return A pointer to a static character array containing hwType's
 * description. Do not try to free(2) this array.
 */
char *HW_printType(unsigned int hwType);

#endif /* _HARDWARE_H_ */
