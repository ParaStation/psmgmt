/*
 *               ParaStation3
 * pshwtypes.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pshwtypes.h,v 1.3 2002/08/01 16:42:40 eicker Exp $
 *
 */
/**
 * @file
 * ParaStation hardware types.
 *
 * $Id: pshwtypes.h,v 1.3 2002/08/01 16:42:40 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSHWTYPES_H
#define __PSHWTYPES_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Supported hardware types.
 *
 * ParaStation's supported communication hardware types.
 */
#define PSHW_ETHERNET            0x0001 /**< TCP/IP over Ethernet.
					   Used within ParaStation FE. */
#define PSHW_MYRINET             0x0002 /**< RDP (Reliable Data Protocol) over
					   Myrinet.
					   Used within ParaStation 3 and 4. */
#define PSHW_GIGAETHERNET        0x0004 /**< RDP (Reliable Data Protocol) over
					   (GigaBit-)Ethernet.
					   Used within ParaStation 4. */

/**
 * @brief Get string describing the hardware-type.
 *
 * Get a string describing the hardware-type @a hwType. The returned
 * pointer leads to a static character array that contains the
 * describtion. Sequent calls of @ref PSHW_printType() will change the
 * content of this array. Therefor the result is not what you expect
 * if more then one call of this function is done within the
 * argument-list of printf(3) and friends.
 *
 * @param hwType The hardware-type to describe. This is supposed to be
 * a bitwise-or of @ref PSWH_ETHERNET, @ref PSHW_MYRINET and @ref
 * PSHW_GIGAETHERNET or 0.
 *
 * @return A pointer to a static character array containing hwType's
 * description. Do not try to free(2) this array.  */
char *PSHW_printType(int hwType);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSHWTYPES_H */
