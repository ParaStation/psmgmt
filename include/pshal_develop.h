
/* 2001-08-22 Jens Hauke */

#ifndef _PSHAL_DEVELOP_H_
#define _PSHAL_DEVELOP_H_

#ifdef ENABLE_PACKETCOUNTER
extern unsigned PSHAL_SendPIOCount;
extern unsigned PSHAL_SendDMACount;
extern unsigned PSHAL_SendDirectDMACount;
extern unsigned PSHAL_RecvSmallCount;
extern unsigned PSHAL_RecvLargeCount;
extern unsigned PSHAL_RecvDirectCount;

extern unsigned PSHAL_RecvNode[4096][8];
extern unsigned PSHAL_SendNode[4096][8];
extern unsigned PSHAL_LastPort;
#else
static unsigned PSHAL_CntDummy = -1;
#define PSHAL_SendPIOCount PSHAL_CntDummy
#define PSHAL_SendDMACount PSHAL_CntDummy
#define PSHAL_SendDirectDMACount PSHAL_CntDummy
#define PSHAL_RecvSmallCount PSHAL_CntDummy
#define PSHAL_RecvLargeCount PSHAL_CntDummy
#define PSHAL_RecvDirectCount PSHAL_CntDummy

#endif




#endif  /* _PSHAL_DEVELOP_H_ */
