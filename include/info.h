#ifndef __info_h_
#define __info_h_

typedef struct _INFO_STATUS{
    int myid, nrofnodes, speed;
    int aos_err, da_err, nack_err, crc_err, rsa_err;
    int sbuf, maxsbuf, irbuf, maxirbuf, rbuf, maxrbuf;
} INFO_STATUS;

typedef struct _INFO_COUNT{
    int t[8];
    int c[8];
} INFO_COUNT;

int INFO_request_rdpstatus(int nodeno,void* buffer, int size);

int INFO_request_hoststatus(void* buffer, int size);

int INFO_request_host(unsigned int addr);

int INFO_request_countstatus(int nodeno);

int INFO_request_tasklist(int nodeno);

int INFO_request_psistatus(int nodeno);

enum{
    INFO_UID = 0x01,        /* get the uid of the task */
    INFO_ISALIVE = 0x02,    /* check if the tid is alive */
    INFO_PRINTINFO = 0x03,  /* print the infos of this task */
    INFO_PTID = 0x04        /* get the parents TID */
};

long INFO_request_taskinfo(long tid,long what);

void INFO_printCount(INFO_COUNT info);

void INFO_printStatus(INFO_STATUS info);

#endif /* __info_h_ */
