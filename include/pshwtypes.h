/*
 *               ParaStation3
 * pshwtypes.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pshwtypes.h,v 1.7 2003/04/11 10:27:04 eicker Exp $
 *
 */
/**
 * @file
 * ParaStation hardware types.
 *
 * $Id: pshwtypes.h,v 1.7 2003/04/11 10:27:04 eicker Exp $
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

#define PSHW_NAME_ETHERNET	"ethernet"
#define PSHW_NAME_MYRINET	"myrinet"
#define PSHW_NAME_INFINIBAND	"infiniband"
#define PSHW_NAME_STARFABRIC	"starfabric"
#define PSHW_NAME_P4SOCK	"p4sock"


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSHWTYPES_H */
