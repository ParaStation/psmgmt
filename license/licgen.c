



#include <ps_types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include "license_priv.h"
#include <netinet/in.h>

int main(int argc, char **argv)
{
#define BLEN (pslic_blen2alen(sizeof(pslic_bin_t)))
    struct timeval tv;
    pslic_bin_t lic;
    char licstr[ BLEN + 1];
    int bla;
    
    printf("No of Nodes  :");fflush(stdout);
    scanf("%d",&bla);
    lic.Nodes	= bla;
    printf("expire (days):");fflush(stdout);
    scanf("%d",&bla);
    printf("Nodes :%d\n",lic.Nodes);
    printf("Days  :%d\n",bla);
    lic.ValidFrom = time(0);
    lic.ValidTo   = lic.ValidFrom + 3600*24*bla;

    
    lic.Magic	  = LIC_KEYMAGIC;

    lic.Nodes     = htonl(lic.Nodes);
    lic.ValidFrom = htonl(lic.ValidFrom);
    lic.ValidTo   = htonl(lic.ValidTo);
    lic.Magic     = htonl(lic.Magic);

    pslic_encode(&lic,sizeof(lic),LIC_KEYMAGIC);
    licstr[BLEN]=0;
    pslic_bin2ascii((char*)&lic,sizeof(lic),licstr);
    printf("Key: %s\n",licstr);
    return 0;
}


/*
 * Local Variables:
 *  compile-command: "gcc -I../include/. licgen.c -o licgen"
 * End:
 *
 */
