/*
 *               ParaStation
 * psidtimer.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidtimer.h,v 1.2 2004/01/09 15:58:25 eicker Exp $
 *
 */
/**
 * \file
 * Helper functions for timer handling within the ParaStation daemon.
 *
 * All the stuff within this file is defined inline. Thus no
 * psidtimer.c is necessary.
 *
 * $Id: psidtimer.h,v 1.2 2004/01/09 15:58:25 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDTIMER_H
#define __PSIDTIMER_H

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * ParaStations main timer. Defined within psid.c.
 */
extern struct timeval mainTimer;

/**
 * The standard time spent within the main select. Defined within
 * psid.c. The value is defined from parastation.conf's SelectTime
 * option.
 */
extern struct timeval selectTime;

/**
 * @brief Set a timer.
 *
 * Set the timer pointed by @a tvp to the value pointed by @a fvp.
 *
 * @param tvp Pointer to the timer to set.
 *
 * @param fvp Pointer to the value to use.
 */
#define timerset(tvp,fvp)        {(tvp)->tv_sec  = (fvp)->tv_sec;\
                                  (tvp)->tv_usec = (fvp)->tv_usec;}

/**
 * @brief Operate on a timer.
 *
 * Execute the operation @a op on the timer pointed by @a tvp using
 * the operands @a sec and @a usec on the corresponding members of the
 * timer. For the operation @a op to make sense it has to be one of
 * '+' and '-'.
 *
 * @param tvp Pointer to the timer to operate on.
 *
 * @param sec Operand of the operation on the tv_sec part of the
 * timer.
 *
 * @param usec Operand of the operation on the tv_usec part of the
 * timer.
 *
 * @param op The operation to execute. To make sense, this has to be
 * one of '+' and '-'.
 */
#define timerop(tvp,sec,usec,op) {(tvp)->tv_sec  = (tvp)->tv_sec op sec;\
                                  (tvp)->tv_usec = (tvp)->tv_usec op usec;}

/**
 * @brief Add to a timer
 *
 * Add @a sec seconds and @a usec microseconds to the timer pointed by
 * tvp.
 *
 * @param tvp Pointer to the timer to modify.
 *
 * @param sec The number of seconds to add to the timer.
 *
 * @param usec The number of microseconds to add to the timer.
 */
#define mytimeradd(tvp,sec,usec) timerop(tvp,sec,usec,+)

/**
 * @brief Subtract from a timer
 *
 * Subtract @a sec seconds and @a usec microseconds from the timer
 * pointed by tvp.
 *
 * @param tvp Pointer to the timer to modify.
 *
 * @param sec The number of seconds to add to the timer.
 *
 * @param usec The number of microseconds to add to the timer.
 */
#define mytimersub(tvp,sec,usec) timerop(tvp,sec,usec,-)


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDTIMER_H */
