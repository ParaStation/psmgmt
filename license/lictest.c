

#include <string.h>
#include <unistd.h>
#include "stdio.h"
#include <netinet/in.h>
#include "ps_types.h"
#include "license_priv.h"

void check_mcpkey(char *Key)
{
    static pslic_bin_t LicKey;
    pslic_binpub_t LicPub;

    char *from;
    char *to;
    unsigned int i;
    
    if (!Key) return;
    
    pslic_ascii2bin((unsigned char *) Key,
		    pslic_blen2alen(sizeof(LicPub))
		    ,(unsigned char *)&LicPub);

    from=(char*)&(LicPub);
    to=(char*)(&LicKey);

    for (i=0; i < sizeof(pslic_bin_t); i++) *to++=*from++;
    
    pslic_decode(&LicKey,sizeof(LicKey),LIC_KEYMAGIC);

    LicKey.Nodes     = htonl(LicKey.Nodes);
    LicKey.ValidFrom = htonl(LicKey.ValidFrom);
    LicKey.ValidTo   = htonl(LicKey.ValidTo);
    LicKey.Magic     = htonl(LicKey.Magic);

    fprintf(stdout, "MCPKey: LicMagic : %s\n",
	    LicKey.Magic == LIC_KEYMAGIC ? "OK" : "FALSE");
    fprintf(stdout, "MCPKey: ValidFrom: %d\n", LicKey.ValidFrom);
    fprintf(stdout, "MCPKey: ValidTo  : %d\n", LicKey.ValidTo);
    fprintf(stdout, "MCPKey: Nodes    : %d\n", LicKey.Nodes);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
	fprintf(stderr, "Expect MCPKey as parameter\n");
    } else {
	check_mcpkey(argv[1]);
    }
	   
    return 0;
}


/*
 * Local Variables:
 *  compile-command: "gcc lictest.c -Wall -W -Wno-unused -g -O2 -I../include -o lictest"
 * End:
 *
 */
