#ifndef ___PSIADMIN_H___
#define ___PSIADMIN_H___

#define ALLNODES	-1

int PSIADM_Debug(char* protocol,long portno);
int PSIADM_LookUpNodeName(char* hostname);
void PSIADM_AddNode(int first, int last);

void PSIADM_NodeStat(int first, int last);
void PSIADM_RDPStat(int first, int last);
void PSIADM_MCastStat(int first, int last);
void PSIADM_CountStat(int first, int last);
void PSIADM_ProcStat(int first, int last, int full);
void PSIADM_LoadStat(int first, int last);
void PSIADM_HWStat(int first, int last);

void PSIADM_SetMaxProc(int count, int first, int last);
void PSIADM_ShowMaxProc(int first, int last);
void PSIADM_SetUser(int uid, int first, int last);
void PSIADM_ShowUser(int first, int last);
void PSIADM_SetParam(int type, int value, int first, int last);
void PSIADM_ShowParam(int type, int first, int last);

void PSIADM_Version(void);
/* void PSIADM_ShowConfig(void); */

void PSIADM_Reset(int reset_hw, int first, int last);
void PSIADM_ShutdownCluster(int first, int last);
void PSIADM_TestNetwork(int mode);
void PSIADM_KillProc(long tid, int sig);
void PSIADM_Exit(void);

#endif
