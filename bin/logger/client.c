#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <logger.h>

int PSI_myid = 0;

char * PSI_LookupInstalldir(void)
{
    static char thisdir[] = ".";

    return thisdir;
}

int main(int argc, char *argv[])
{
    int loggerport;
    struct in_addr in_addr;
    struct hostent *hp;

    if (argc != 3){
	printf("Usage: %s myid loggerport\n", argv[0]);
	return -1;
    }

    PSI_myid  = atoi(argv[1]);
    printf("My ID is %d\n", PSI_myid);

    loggerport = atoi(argv[2]);
    printf("Try to connect logger on port %d\n", loggerport);

    /* Get own IP-Address */
    hp = gethostbyname("localhost");
    bcopy((char *)hp->h_addr, (char*)&in_addr, hp->h_length);

    printf("My IP-address is %d[%x]\n", in_addr.s_addr, in_addr.s_addr);

    LOGGERspawnforwarder(in_addr.s_addr, loggerport);

    printf("forwarder spawned1\n");
    printf("forwarder spawned2");

    sleep(5);

    return 0;
}
