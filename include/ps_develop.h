/*
 * develop.h   Compiler switches for develop/debug
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * Version 1.0:	 Jun 2000: initial implementation
 *
 *		Jens Hauke
 *
 *
 */
/*************************************************************fdb*
 * include for debugging switches 
 *							      
 *************************************************************fde*/

#ifndef _DEVELOP_H_
#define _DEVELOP_H_


/* More debug output */
#define DEVELOP

/* Old Buildin debug Dispatchtable: Now use getinfo*/
//#define DEBUGDT

/* Get Timing of Dispatch: */
//#define DISPATCHTIMER


/* Dispatch backtrace */
//#define ENABLE_DISPATCH_BT

/* Enable Trace: */
//#define ENABLETRACE
/*   sleep after each TRACE */
//#define ENABLESLOWTRACE

/* Additional to ENABELTRACE:
   Set Breakpoints */
//#define ENABLEBREAK

/* Additional to ENABELTRACE:
   Set Breakpoint before each tracering overflow */
//#define ENABLETRACENOLOST

/* Detect shortcut in Timeoutq */
//#define ENABLE_TO_CHECK


/* Enable check of illegal dma_hostqueue0() calls (len==0)*/
//#define ENABLE_DMA_ZERO_CALL_CHECK

/* Dont send packets over network */
//#define DISABLE_NETSEND

/* Dont optimize DMA tramsfer 4 Net 2 host */
//#define DISABLE_N2HDMA_OPT			   


/* Reject Random packets on receive*/
//#define  ENABLE_RANDOM_REJECT

/* Enable extratimer for TIMER_START()/STOP() */
//#define ENABLE_TIMER

/* Enable Timestamper in DataSection */
//#define ENABLE_TIMESTAMP   


/* if definend, send only the 1st ENABLE_N2H_DATACUT data bytes from N2H */
//#define ENABLE_N2H_DATACUT 10

/* if definend, send only the 1st ENABLE_H2N_DATACUT data bytes from N2H */
//#define ENABLE_H2N_DATACUT 10

/* if definend, send only the 1st ENABLE_N2N_DATACUT data bytes from N2N */
//#define ENABLE_N2N_DATACUT 10

/* if definend, copy only the 1st ENABLE_H2H_DATACUT data bytes from H2H (HAL) */
//#define ENABLE_H2H_DATACUT 10

/* Debug pshal */
//#define DEBUG_PSHAL

/* Enable sis MCP */
//#define ENABLE_SIS

/* Enable sis MCP */
//#define ENABLE_JM

/* Enable remote kernel debuging */
//#define ENABLE_DEBUG_MSG	


/* Send data with zero copy */
#define ENABLE_DMASEND
/* Receive data with zero copy */
#define ENABLE_DMARECV

/* Disable local communication */
#define DISABLE_LOCAL_COM

/* Enable Packetcounter in PSHAL */
//#define ENABLE_PACKETCOUNTER

/* PSPORT print debuging on ^Z */
//#define ENABLE_REQUESTDUMP



#define PSM_PRINT_MAP_LEVEL_OFF 1000
#define xMARK PSM_PRINT(0,("PSM: MARK in File "__FILE__" :%d\n",__LINE__))


#ifdef DEVELOP
  #define PSM_PRINT_LEVEL_DEV 10
#else
  #define PSM_PRINT_LEVEL_DEV 1
#endif


//#define _P PSM_PRINT(0,("PSM: %s:%d\n",__FILE__,__LINE__))

#endif





