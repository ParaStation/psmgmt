/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2003 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * hardware.h: Hardware handling
 *
 * $Id: hardware.h,v 1.1 2003/04/03 14:49:35 eicker Exp $
 *
 * @author
 *         Norbert Eicker <eicker@par-tec.de>
 *
 * @file
 ***********************************************************/

#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Documentation */


int HW_add(const char *name);

int HW_num(void);

char *HW_name(const int idx);

int HW_index(const char *name);

/* Script types */
#define HW_STARTER	"HW_Starter"
#define HW_STOPPER	"HW_Stopper"
#define HW_SETUP	"HW_Setup"
#define HW_HEADERLINE	"HW_Header"
#define HW_COUNTER	"HW_Counter"

int HW_setScript(const int idx, const char *type, const char *script);

char *HW_getScript(const int idx, const char *type);

int HW_setEnv(const int idx, const char *name, const char *val);

char *HW_getEnv(const int idx, const char *name);

int HW_getEnvSize(const int idx);

char *HW_dumpEnv(const int idx, const int num);

/**
 * @brief Get string describing the hardware-type.
 *
 * Get a string describing the hardware-type @a hwType. The returned
 * pointer leads to a static character array that contains the
 * describtion. Sequent calls of @ref HW_printType() will change the
 * content of this array. Therefor the result is not what you expect
 * if more then one call of this function is done within the
 * argument-list of printf(3) and friends.
 *
 * @param hwType The hardware-type to describe. This is supposed to be
 * a bitwise-or of @ref PSHW_ETHERNET, @ref PSHW_MYRINET and @ref
 * PSHW_GIGAETHERNET or 0.
 *
 * @return A pointer to a static character array containing hwType's
 * description. Do not try to free(2) this array.  */
char *HW_printType(unsigned int hwType);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _HARDWARE_H_ */
