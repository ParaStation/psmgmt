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
void PSIADM_SetPsidDebug(int val, int first, int last);
void PSIADM_ShowPsidDebug(int first, int last);
void PSIADM_SetPsidSelectTime(int val, int first, int last);
void PSIADM_ShowPsidSelectTime(int first, int last);
void PSIADM_SetRDPDebug(int val, int first, int last);
void PSIADM_ShowRDPDebug(int first, int last);
void PSIADM_SetRDPPktLoss(int val, int first, int last);
void PSIADM_ShowRDPPktLoss(int first, int last);
void PSIADM_SetRDPMaxRetrans(int val, int first, int last);
void PSIADM_ShowRDPMaxRetrans(int first, int last);
void PSIADM_SetMCastDebug(int val, int first, int last);
void PSIADM_ShowMCastDebug(int first, int last);

void PSIADM_Version(void);
void PSIADM_ShowConfig(void);

void PSIADM_SetSmallPacketSize(int smallpacketsize);
void PSIADM_ShowSmallPacketSize(void);
void PSIADM_SetResendTimeout(int time);
void PSIADM_ShowResendTimeout(void);
void PSIADM_SetHNPend(int val);
void PSIADM_ShowHNPend(void);
void PSIADM_SetAckPend(int val);
void PSIADM_ShowAckPend(void);

void PSIADM_Reset(int reset_hw, int first, int last);
void PSIADM_ShutdownCluster(int first, int last);
void PSIADM_TestNetwork(int mode);
void PSIADM_KillProc(long tid, int sig);
void PSIADM_Exit(void);

#endif
