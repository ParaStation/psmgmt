#ifndef __info_h_
#define __info_h_

int INFO_request_rdpstatus(int nodeno,void* buffer, int size);

int INFO_request_hoststatus(void* buffer, int size);

int INFO_request_host(unsigned int addr);

int INFO_request_countstatus(int nodeno, int header);

int INFO_request_tasklist(int nodeno);

enum{
    INFO_UID = 0x01,        /* get the uid of the task */
    INFO_ISALIVE = 0x02,    /* check if the tid is alive */
    INFO_PRINTINFO = 0x03,  /* print the infos of this task */
    INFO_PTID = 0x04        /* get the parents TID */
};

long INFO_request_taskinfo(long tid,long what);

#endif /* __info_h_ */
