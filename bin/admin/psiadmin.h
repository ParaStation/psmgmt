#ifndef ___PSIADMIN_H___
#define ___PSIADMIN_H___


#define ALLNODES	-1
#define NODEERR		-2

int PSIADM_Debug(char* protocol,long portno);
int PSIADM_LookUpNodeName(char* hostname);
void PSIADM_AddNode(int node);

void PSIADM_NodeStat(int node);
void PSIADM_RDPStat(int node);
void PSIADM_CountStat(int node);
void PSIADM_ProcStat(int node);
void PSIADM_LoadStat(int node);

void PSIADM_SetMaxProc(int count);
void PSIADM_SetUser(int uid);
void PSIADM_SetDebugmask(long newmask);
void PSIADM_SetPsidDebug(int val, int node);
void PSIADM_SetRdpDebug(int val, int node);

void PSIADM_Version(void);
void PSIADM_ShowParameter(void);

void PSIADM_SetHalInterface(int val);
void PSIADM_SetSmallPacketSize(int smallpacketsize);
void PSIADM_SetResendTimeout(int time);
void PSIADM_SetConfigNo(int no);

void PSIADM_Reset(int,int,int);
void PSIADM_ShutdownCluster(int,int);
void PSIADM_TestNetwork(int mode);
void PSIADM_KillProc(int id);
void PSIADM_Exit(void);

#endif
