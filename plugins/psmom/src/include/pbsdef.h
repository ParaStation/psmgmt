/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/

/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_MOM_PBS_DEFINITIONS
#define __PS_MOM_PBS_DEFINITIONS

#include "pbserror.h"

/* type to use in place of socklen_t if not defined */
#define torque_socklen_t socklen_t

/* various data buffer sizes */
#define JOB_NAME_LEN	    256
#define USER_NAME_LEN	    100
#define NODE_NAME_LEN	    100
#define HOST_NAME_LEN	    1024
#define QUEUE_NAME_LEN	    100
#define REQUEST_NAME_LEN    1024

/* the data structure size which qsub will encode its data in */
#define QSUB_DATA_SIZE	    80
#define QSUB_DATA_CONTROL   6

/* protocol numbers and versions for PBS communications */
#define RM_PROTOCOL	1   /* resource monitor protocol number */
#define	TM_PROTOCOL	2   /* task manager protocol number */
#define	IM_PROTOCOL	3   /* inter-manager protocol number (pbs_mom only) */
#define	IS_PROTOCOL	4   /* inter-server protocol number */

/* types of Inter Server messages */
#define	IS_NULL		    0
#define	IS_HELLO	    1
#define	IS_CLUSTER_ADDRS    2
#define IS_UPDATE	    3
#define IS_STATUS           4

/* types of RM messages */
#define RM_NPARM        32      /* max number of parameters for child */
#define RM_CMD_CLOSE    1
#define RM_CMD_REQUEST  2
#define RM_CMD_CONFIG   3
#define RM_CMD_SHUTDOWN 4
#define RM_RSP_OK       100
#define RM_RSP_ERROR    999

/* types of batch reply messages */
#define BATCH_REPLY_CHOICE_NULL         1       /* no reply choice, just code */
#define BATCH_REPLY_CHOICE_Queue        2       /* Job ID */
#define BATCH_REPLY_CHOICE_RdytoCom     3       /* select */
#define BATCH_REPLY_CHOICE_Commit       4       /* commit */
#define BATCH_REPLY_CHOICE_Select       5       /* select */
#define BATCH_REPLY_CHOICE_Status       6       /* status */
#define BATCH_REPLY_CHOICE_Text         7       /* text */
#define BATCH_REPLY_CHOICE_Locate       8       /* locate */
#define BATCH_REPLY_CHOICE_RescQuery    9       /* Resource Query */

/* TM protocol message types */
#define TM_INIT		100	/* tm_init request */
#define TM_TASKS	101     /* tm_taskinfo request */
#define TM_SPAWN	102     /* tm_spawn request */
#define TM_SIGNAL	103	/* tm_signal request */
#define TM_OBIT		104	/* tm_obit request */
#define TM_RESOURCES	105    	/* tm_rescinfo request */
#define TM_POSTINFO	106	/* tm_publish request */
#define TM_GETINFO	107	/* tm_subscribe request */
#define TM_GETTID	108	/* tm_gettasks request */
#define TM_REGISTER	109	/* tm_register request */
#define TM_RECONFIG	110	/* tm_register deferred reply */
#define TM_ACK		111	/* tm_register event acknowledge */
#define TM_FINALIZE	112	/* tm_finalize request, there is no reply */
#define TM_OKAY		0
#define TM_ERROR	999

/* PBS batch requests */
enum PBatchReqTypeEnum {
  PBS_BATCH_Connect     = 0,
  PBS_BATCH_QueueJob    = 1,
  PBS_BATCH_JobCred     = 2,
  PBS_BATCH_Jobscript   = 3,
  PBS_BATCH_RdytoCommit = 4,
  PBS_BATCH_Commit      = 5,
  PBS_BATCH_DeleteJob   = 6,
  PBS_BATCH_HoldJob     = 7,
  PBS_BATCH_LocateJob   = 8,  /* for pbs_server only */
  PBS_BATCH_Manager     = 9,  /* for pbs_server only */
  PBS_BATCH_MessJob     = 10,
  PBS_BATCH_ModifyJob   = 11,
  PBS_BATCH_MoveJob     = 12, /* for pbs_server only */
  PBS_BATCH_ReleaseJob  = 13, /* for pbs_server only */
  PBS_BATCH_Rerun       = 14,
  PBS_BATCH_RunJob      = 15, /* for pbs_server only */
  PBS_BATCH_SelectJobs  = 16, /* for pbs_server only */
  PBS_BATCH_Shutdown    = 17,
  PBS_BATCH_SignalJob   = 18,
  PBS_BATCH_StatusJob   = 19,
  PBS_BATCH_StatusQue   = 20, /* for pbs_server only */
  PBS_BATCH_StatusSvr   = 21, /* for pbs_server only */
  PBS_BATCH_TrackJob    = 22, /* for pbs_server only */
  PBS_BATCH_AsyrunJob   = 23, /* for pbs_server only */
  PBS_BATCH_Rescq       = 24, /* for pbs_server only */
  PBS_BATCH_ReserveResc = 25, /* for pbs_server only */
  PBS_BATCH_ReleaseResc = 26, /* for pbs_server only */
  PBS_BATCH_StageIn     = 48, /* for pbs_server only */
  PBS_BATCH_AuthenUser  = 49, /* for pbs_server only */
  PBS_BATCH_OrderJob    = 50, /* for pbs_server only */
  PBS_BATCH_SelStat     = 51, /* for pbs_server only */
  PBS_BATCH_RegistDep   = 52, /* for pbs_server only */
  PBS_BATCH_CopyFiles   = 54,
  PBS_BATCH_DelFiles    = 55,
  PBS_BATCH_JobObit     = 56, /* for pbs_server only */
  PBS_BATCH_MvJobFile   = 57,
  PBS_BATCH_StatusNode  = 58, /* for pbs_server only */
  PBS_BATCH_Disconnect  = 59
};

#endif
