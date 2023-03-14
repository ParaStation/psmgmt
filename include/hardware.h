/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Attribute management
 */
#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include <stdbool.h>
#include <stdint.h>

/** */
typedef int8_t AttrIdx_t;

/** */
typedef uint32_t AttrMask_t;

/**
 * @brief Register a new node attribute
 *
 * Create the new node attribute @a name. If an attribute with this
 * name already exists, nothing is done. The new node attribute is
 * registered to an unique positive index which is returned. It might
 * be looked up later using the Attr_index() function.
 *
 * @param name Name of the new node attribute
 *
 * @return If an node attribute named @a name was registered before or
 * an error occured, -1 is returned; otherwise the index of the new
 * attribute is given
 *
 * @see Attr_index()
 */
AttrIdx_t Attr_add(const char *name);

/**
 * @brief Get the number of registered attributes
 *
 * Get the number of node attributes already registered via Attr_add().
 *
 * @return The number of registered attribute types is returned
 *
 * @see Attr_add()
 */
AttrIdx_t Attr_num(void);

/**
 * @brief Get node attribute's name
 *
 * Get the unique name of the attribute with index @a idx. The
 * attribute type has to be registered using Attr_add() before. The
 * index @a idx used to identify the attribute type is the one returned
 * by the Attr_add() function while registering the type or might be
 * looked up via Attr_index() later.
 *
 * @param idx Unique index of the node attribute
 *
 * @return If the index is valid, a pointer to the corresponding name
 * is returned; otherwise NULL is returned
 *
 * @see Attr_add(), Attr_index()
 */
char *Attr_name(const AttrIdx_t idx);

/**
 * @brief Get attribute's index
 *
 * Get the unique index of the node attribute with name @a name.
 *
 * @param name Name of the attribute to lookup
 *
 * @return If the node attribute was found, the corresponding index is
 * returned; otherwise -1 is returned
 */
AttrIdx_t Attr_index(const char *name);

/**
 * @brief Provide a string describing the attributes
 *
 * Get a string describing the attrbutes encoded in @a attr. A pointer
 * to a static character array containing the description is
 * returned. Subsequent calls to @ref Attr_print() will change the
 * content of this array. Therefore the result is not what you expect
 * if more then one call of this function is made within a single
 * argument-list of printf(3) and friends.
 *
 * @param attr Attribute mask to describe; this is supposed to be a
 * bitwise-or of the (1<<idx), where @a idx are the indexes of the
 * various node attributes to describe
 *
 * @return A pointer to a static character array containing the
 * attributes description; do not try to free(2) this array
 */
char *Attr_print(AttrMask_t attr);

/** @defgroup Some node attributes represent node-local hardware that
 * might need specific handling like initialization, or setup and can
 * provided additional information like counter, etc.  For this,
 * different scripts and environments might be associated to this
 * attributes. Helper functions to trigger those scripts and analyze
 * their results are provided in psidhw.c. These interfaces are mostly
 * of historical relevance but kept for backward compatibility with
 * their original names */
/*\@{*/

/** Various script types associated with the hardware */
#define HW_STARTER	"HW_Starter" /**< Script used to start the hardware */
#define HW_STOPPER	"HW_Stopper" /**< Script used to stop the hardware */
#define HW_SETUP	"HW_Setup"   /**< Script used to setup the hardware */
#define HW_HEADERLINE	"HW_Header"  /**< Script used to get counter-names */
#define HW_COUNTER	"HW_Counter" /**< Script used to read counters */

/**
 * @brief Register a hardware's script
 *
 * Register the script @a script to the node attribute indexed by @a
 * idx to be used as type @a type. @a type has to be one of @ref
 * HW_STARTER, @ref HW_STOPPER, @ref HW_SETUP, @ref HW_HEADERLINE or
 * @ref HW_COUNTER.
 *
 * @param idx Index of the node attribute the script shall be
 * registered to
 *
 * @param type Role the script shall play for the hardware
 *
 * @param script Actual script to register
 *
 * @return On succes, true is returned; or false if an error occured
 */
bool HW_setScript(const AttrIdx_t idx, const char *type, const char *script);

/**
 * @brief Get a hardware's script
 *
 * Get the script of type @a type from the node attribute indexed by
 * @a idx. @a type has to be one of @ref HW_STARTER, @ref HW_STOPPER,
 * @ref HW_SETUP, @ref HW_HEADERLINE or @ref HW_COUNTER.
 *
 * @param idx Index of the node attribute to lookup
 *
 * @param type Script's role to the node attribute
 *
 * @return On success, i.e. if the index @a idx is valid and has a
 * script with role @a type attached, the corresponding script is
 * returned; otherwise NULL is returned
 */
char *HW_getScript(const AttrIdx_t idx, const char *type);

/**
 * @brief Register a hardware's environment
 *
 * Register the environment variable @a name to the node attribute
 * indexed by @a idx to have the value @a val.
 *
 * @param idx Index of the node attribute the environment variable
 * shall be registered to
 *
 * @param name Environment variable's name
 *
 * @param val Actual value to register
 *
 * @return On succes, true is returned; or false if an error occured
 */
bool HW_setEnv(const AttrIdx_t idx, const char *name, const char *val);

/**
 * @brief Get a hardware's environment
 *
 * Get the environment with name @a name from the node attribute
 * indexed by @a idx.
 *
 * @param idx Index of the node attribute to lookup
 *
 * @param name Environment's name within the node attribute
 *
 * @return On success, i.e. if the index @a idx is valid and has an
 * environment variable with name @a name associated, the
 * corresponding value is returned; otherwise NULL is returned
 */
char *HW_getEnv(const AttrIdx_t idx, const char *name);

/**
 * @brief Get a node attribute's size of environment
 *
 * Get the number of environment variables registered to the node
 * attribute indexed by @a idx.
 *
 * @param idx Index of the node attribute to lookup
 *
 * @return If the index @a idx is valid, the number of attached
 * environment variables is returned; or 0 otherwise
 */
 int HW_getEnvSize(const AttrIdx_t idx);

/**
 * @brief Dump an environment
 *
 * Dump the @a num'th environment variable associated to the node
 * attribute indexed by @a idx. This function is mainly used for
 * putting whole hardware environments into a process' actual
 * environment via successive calls of putenv().
 *
 * @param idx Index of the node attribute to lookup
 *
 * @param num Running number of the environment variable to dump
 *
 * @return If the index @a idx is valid and an environment variable is
 * stored under the running number @a num, a string of the format
 * 'name=value' is returned; otherwise NULL is returned
 */
char *HW_dumpEnv(const AttrIdx_t idx, const int num);

/*\@}*/

#endif /* _HARDWARE_H_ */
