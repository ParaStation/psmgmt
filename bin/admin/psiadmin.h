#ifndef ___PSIADMIN_H___
#define ___PSIADMIN_H___

#define ALLNODES	-1

int PSIADM_Debug(char* protocol,long portno);
int PSIADM_LookUpNodeName(char* hostname);
void PSIADM_AddNode(int first, int last);

void PSIADM_NodeStat(int first, int last);
void PSIADM_RDPStat(int first, int last);
void PSIADM_CountStat(int first, int last);
void PSIADM_ProcStat(int first, int last);
void PSIADM_LoadStat(int first, int last);

void PSIADM_SetMaxProc(int count);
void PSIADM_SetUser(int uid);
void PSIADM_SetDebugmask(long newmask);
void PSIADM_SetPsidDebug(int val, int first, int last);
void PSIADM_SetRdpDebug(int val, int first, int last);

void PSIADM_Version(void);
void PSIADM_ShowConfig(void);

void PSIADM_SetHalInterface(int val);
void PSIADM_SetSmallPacketSize(int smallpacketsize);
void PSIADM_SetResendTimeout(int time);
void PSIADM_SetConfigNo(int no);

void PSIADM_Reset(int reset_hw, int first, int last);
void PSIADM_ShutdownCluster(int first, int last);
void PSIADM_TestNetwork(int mode);
void PSIADM_KillProc(int id);
void PSIADM_Exit(void);

#endif
