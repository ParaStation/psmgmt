#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "psicomm.h"

#define CHALLENGE 1
#define RESPONSE  2

void checkResult(int rank, char data[1024], char buf[1024], size_t len)
{
    unsigned int i;

    for (i=0; i<len; i++) {
	if (data[i] != buf[i]) break;
    }

    if (i<len) printf("Wrong data on position %d from rank %d\n", i, rank);

    return;
}

int main(int argc, char *argv[])
{
    int pmiSize = 0, pmiID = 0, i;
    unsigned j;
    char *envStr, data[1024], buf[1024];

    envStr = getenv("PMI_SIZE");
    if (!envStr) exit(1);
    pmiSize = atoi(envStr);

    envStr = getenv("PMI_RANK");
    if (!envStr) exit(2);
    pmiID = atoi(envStr);

    if (!pmiID) {
	printf("We're running on %d nodes.\n", pmiSize);
    }

    if (PSIcomm_init() < 0) {
	printf("Failed to initialize PSIcomm on rank %d\n", pmiID);
	exit(3);
    }

    sleep(1);

    srandom(pmiID);
    for (j=0; j<sizeof(data); j++) data[j] = (char)random();

    for (i=0; i<pmiSize; i++) {
	if (i==pmiID) continue;

	PSIcomm_send(i, CHALLENGE, data, sizeof(data));
    }

    for (i=0; i<2*(pmiSize-1); i++) {
	int rank, type, eno;
	size_t len;

	len = sizeof(buf);
	PSIcomm_recv(&rank, &type, buf, &len);

	switch (type) {
	case -1:
	    eno = *(int *)buf;
	    printf("Received error %d from %d: %s\n",
		   eno, rank, strerror(eno)); 
	    break;
	case CHALLENGE:
	    PSIcomm_send(rank, RESPONSE, buf, len);
	    break;
	case RESPONSE:
	    checkResult(rank, data, buf, sizeof(data));
	    break;
	default:
	    printf("Received unknown type %d from %d\n", type, rank);
	}
    }

    printf("pmiID: %02d done\n", pmiID);

    return 0;
}
