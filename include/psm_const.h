
/*
 * psm_const.h	 ParaStation3
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * Version 1.0:	 Jun 2000: initial implementation
 *
 *		Jens Hauke
 *
 *
 *
 */
/*************************************************************fdb*
 * consts for kernel module 
 *  ( Major, Devicename, Version ...)			      
 *************************************************************fde*/


#ifndef __PSM_CONST_H__
#define __PSM_CONST_H__

#include "ps_develop.h"

#define PARASTATION_DEVICE_NAME "psm"
#define PSM_DEV "/dev/"PARASTATION_DEVICE_NAME
#define PSM_DESCRIPTION "ParaStation 3 ("PSM_BUILD" "PSM_COMPILE_DATE ")"
/*Debug:*/
//#ifdef PARASTATION_MYRINET_MAJOR
//#undef PARASTATION_MYRINET_MAJOR
//#endif

#ifndef PARASTATION_MYRINET_MAJOR
#  define PARASTATION_MYRINET_MAJOR 40
#endif
#endif /*__PSM_CONST_H__*/

#define PSM_PRINT_LEVEL PSM_PRINT_LEVEL_DEV


#define PSM_NCONTEXT 200  /* max nr of Context from module (not from mcp!)*/
#define PSM_DEFAULT_MCP "mcp"















