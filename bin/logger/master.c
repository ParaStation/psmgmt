#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <logger.h>

int PSI_myid = 0;

char * PSI_LookupInstalldir(void)
{
    static char thisdir[] = ".";

    return thisdir;
}

int main(void)
{
    int loggerport;
    struct in_addr in_addr;
    struct hostent *hp;

    loggerport = LOGGERspawnlogger();

    printf("logger listening on port %d\n", loggerport);

    /* Get own IP-Address */
    hp = gethostbyname("localhost");
    memcpy(&in_addr, hp->h_addr, hp->h_length);

    printf("My IP-address is %s\n", inet_ntoa(in_addr));

    LOGGERspawnforwarder(in_addr.s_addr, loggerport);

    printf("forwarder spawned1\n");
    printf("forwarder spawned2");

    sleep(600);

    return 0;
}
