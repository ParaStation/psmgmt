/*
 *               ParaStation3
 * pshwtypes.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pshwtypes.h,v 1.1 2002/07/18 12:56:49 eicker Exp $
 *
 */
/**
 * @file
 * pshwtypes: ParaStation hardware types.
 *
 * $Id: pshwtypes.h,v 1.1 2002/07/18 12:56:49 eicker Exp $
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

/*----------------------------------------------------------------------*/
/* types of communication-hardware supported by ParaStation             */
/*----------------------------------------------------------------------*/
#define PSHW_ETHERNET            0x0001
#define PSHW_MYRINET             0x0002
#define PSHW_GIGAETHERNET        0x0004

/**
 * @todo Docu
 */
char *PSHW_printType(int hwType);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSHWTYPES_H */
