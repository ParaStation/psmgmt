/*
 *      @(#)license.c    1.00 (Karlsruhe) 10/10/98
 *
 *      written by Joachim Blum
 *
 * license evaluation and creation functions
 */
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include "psp.h"

/* ACHTUNG: ROTATE ist auf INTEGER ausgelegt !! (keine LONG) */
#define   ROTATE(a,n) ( (((unsigned int)a)<<(n)) | (((unsigned int)a)>>(32-(n))) )
#define DEROTATE(a,n) ( (((unsigned int)a)>>(n)) | (((unsigned int)a)<<(32-(n))) )

/*--------------------------------------------------------------------
  Pruefen des Passworts:
  1) Uberpruefen, ob die IP des Konten 0 mit der Zeit, die in den ersten Bytes
  des Lizenzfiles steht mit dem restlichen Lizenzcode uebereinstimmt.
  2) Anzahl der Konten ueberpruefen.
  */

/*--------------------------------------------------------------------
   Erstellen des Lizenzfiles:
   1) Eingabe der IP des ersten Knotens
   2) Eingabe der Knotenanzahl
   3) Eingabe des Endedatums
   */

/* Format des Lizenzfiles:
   Alle Werte sind in der 6 Bit Codierung mit ausgesuchten Werten kodiert.

   Bytes   Inhalt
   0-5     Lizenzstart
   6-11    IP Nummer
   12-17   Version
   18-23   Anzahl an Knoten
   24-29   Lizenzende
   30-35   Checksum
   */
/*--------------------------------------------------------------------*/
/* code the value in the following way:
 * use only characters [48-58], [64-90], [97-122]
 */
void Code6Bit(char* licensekey,long value,int len)
{
    char test;
    while(len){
	test = (value&0x3F)+48;
	if(test>58)test+=5;
	if(test>90)test+=6;

	licensekey[0]=test;
	value >>= 6;
	licensekey++;
	len--;
    }
}
/*--------------------------------------------------------------------*/
void Decode6Bit(char* licensekey,unsigned long* value,int len)
{
    long offset;
    char test;
    *value=0;
    offset = 1;
    while(len){
	test = licensekey[0];
	if(test>90)test -=6;
	if(test>58)test -=5;
	test -= 48;
	*value = test*offset + (*value);
	offset <<= 6;
	licensekey++;
	len--;
    }
}
/*--------------------------------------------------------------------*/
void IpNodesEndFromLicense(char* licensekey, unsigned int* IP, long* nodes,
			   time_t* start, time_t* end, long* version)
{
    int i,ii;
    unsigned long val;
    long checksum=0;
    char key[60];

    for(i=0,ii=0;i<18;i++,ii+=2){ /* De-Scramble lic string */
	key[i] = licensekey[ii];
	key[18+i] = licensekey[ii+1];
/*     key[35-i] = licensekey[ii+1]; */
    }

/* printf("IpNodesEndFromLicense---------------------------------\n"); */
    Decode6Bit(&key[30],&val,6); /* DeCode checksum */
    checksum=val;
/* printf("CScode=%lx\n",val); */
    Decode6Bit(&key[24],&val,6); /* DeCode end */
    checksum^=val;
    *end=DEROTATE(val,2);
/* printf("end code=%lx [%lx]\n",val,*end); */
    Decode6Bit(&key[18],&val,6); /* DeCode nodes */
    checksum^=val;
    *nodes=DEROTATE(val,2);
/* printf("nodes code=%lx [%lx]\n",val,*nodes); */
    Decode6Bit(&key[12],&val,6); /* DeCode version */
    checksum^=val;
    *version=DEROTATE(val,2);
/* printf("version code=%lx [%lx]\n",val,*version); */
    Decode6Bit(&key[6],&val,6); /* DeCode IP */
    checksum^=val;
    *IP=DEROTATE(val,2);
/* printf("IP code=%lx [%x]\n",val,*IP); */
    Decode6Bit(&key[0],&val,6); /* DeCode Start */
    checksum^=val;
    *start=DEROTATE(val,2);
/* printf("start code=%lx [%lx]\n",val,*start); */

    if(checksum){
	*end = 0; 	/* key was modified !! */
	*start = 0;     /* key was modified !! */
    }
    return;
}

/*--------------------------------------------------------------------*/
void makeLicense(char* licensekey, unsigned int IP, long nodes, time_t start,
		 time_t end,long version)
{
    int i,ii;
    unsigned long val;
    long checksum=0;
    char key[60];

    printf("makeLicense()-------------------------------------------\n");
/* printf("makelic: IP=%x, start=%lx, end=%lx, version=%lx, nodes=%lx\n", */
/* IP,start,end,version,nodes); */
    for(i=0;i<60;i++)key[i]='0';
    val = ROTATE(start,2);
    checksum^=val;
    Code6Bit(&key[0],val,6); /* Code the start date */
/* printf("Startcode=%lx [%lx]\n",val,start); */
    val = ROTATE(IP,2);
    checksum^=val;
    Code6Bit(&key[6],val,6); /* Code the IP */
/* printf("IPcode=%lx [%x]\n",val,IP); */
    val=ROTATE(version,2);
    checksum^=val;
    Code6Bit(&key[12],val,6); /* Code the version */
/* printf("Versioncode=%lx [%lx]\n",val,version); */
    val=ROTATE(nodes,2);
    checksum^=val;
    Code6Bit(&key[18],val,6); /* Code nr of nodes */
/* printf("Nodecode=%lx [%lx]\n",val,nodes); */
    val=ROTATE(end,2);
    checksum^=val;
    Code6Bit(&key[24],val,6); /* Code end date */
/* printf("EndCode=%lx [%lx]\n",val,end); */
    val=checksum;
    Code6Bit(&key[30],val,6); /* Code checksum */
/* printf("CScode=%lx\n",val); */
    for(i=0,ii=0;i<18;i++,ii+=2){ /* Scramble lic string */
	licensekey[ii]=key[i];
	licensekey[ii+1]=key[18+i];
/*     licensekey[ii+1]=key[35-i];   */
    }
    licensekey[36]=0;
    return;
}
/*--------------------------------------------------------------------*/
void IpAndNodesFromRequest(char* licenserequest,unsigned int* IP, long* nodes,
			   long *version)
{
    unsigned long mytest;
    Decode6Bit(&licenserequest[0],&mytest,6);
    *IP = mytest;
    Decode6Bit(&licenserequest[6],&mytest,4);
    *nodes = mytest;
    Decode6Bit(&licenserequest[12],&mytest,6);
    *version = mytest;
}
/*--------------------------------------------------------------------*/
void makeLicenseRequest( char* licenserequest)
{
    char host[255];
    struct hostent	*hp;	/* host pointer */
    int IP;
    int nodes;

    printf("Please insert No of nodes: ");scanf("%d",&nodes);
    printf("Please insert name of your server: ");scanf("%s",host);

    printf("creating License Request for host %s\n",host);
    if ((hp = gethostbyname(host)) == NULL) {
	fprintf(stderr, "can't get \"%s\" host entry\n", host);
	exit(1);
    }
    memcpy(&IP, hp->h_addr, hp->h_length);
    Code6Bit(&licenserequest[0],IP,6); /* Code the IP */
    Code6Bit(&licenserequest[6],nodes,6); /* Code the number of nodes */
    Code6Bit(&licenserequest[12],PSPprotocolversion,6); /* Code the actual protocolnumber */
    licenserequest[18]=0;
    printf("OK, we are half done. \n"
	   "Please send an email to licenserequest@par-tec.com to request a license key.\n"
	   "Please indicate the request number: %s\n",licenserequest);
}

/*--------------------------------------------------------------------*/
void makeLicenseResponse( char* licenseresponse)
{
    unsigned int IP;
    long nodes;
    long version;
    char licenserequest[40];
    unsigned int days;
    time_t now;
    time_t end;

    printf("Please insert the license request: ");scanf("%s",licenserequest);
    IpAndNodesFromRequest(licenserequest,&IP,&nodes,&version);

    printf("The License is requested from %x (%d.%d.%d.%d) for %ld nodes for version %ld\n",
	   IP,IP&0xFF,(IP>>8)&0xFF,(IP>>16)&0xFF,(IP>>24)&0xFF,nodes,version);
    if(((IP&0xFF) ==10)
       ||((IP&0xFFFF) >=(172|(16<<8))&& (IP&0xFFFF) <=(172|(31<<8)))
       ||((IP&0xFFFF) ==(192|(168<<8))))
	printf("!!!!!!!!!!! PRIVATE NETWORK !!!!!!!!!!!!\n");
    printf("Please insert the license duration in days(0 for no limit): ");
    scanf("%d",&days);

    now = time(NULL);

    if(days)
	end = now+days*24*60*60;
    else
	end = 0xFFFFFFFF;

    makeLicense(licenseresponse,IP,nodes,now,end,version);

    printf("The license key is: %s\n",licenseresponse);

    {
	time_t start;
	now = time(NULL);
	end = -1;
	IpNodesEndFromLicense( licenseresponse, &IP, &nodes, &start, &end,&version);
	printf("License key for %x, %ld nodes for %ld days(start= %lx end = %lx)\n",
	       IP, nodes, ((unsigned int)end-now)/24/60/60,start, end);
    }
}
/*--------------------------------------------------------------------*/
/* int checkLicense(char*licensekey,unsigned int Node0, int NrOfNodes)
 *
 * checks if the license key is valid for the passed node0 and NrOfNodes
 * RETURN 0 on failure
 *        version on success
 */
int checkLicense(char*licensekey,unsigned int Node0, int NrOfNodes)
{
    unsigned int IP;
    long nodes;
    time_t end;
    time_t start;
    long version;
    time_t now = time(NULL);
    IpNodesEndFromLicense( licensekey, &IP, &nodes, &start, &end,&version);
    printf("License key for %x, %ld nodes for %ld days\n",IP, nodes,
	   (end-now)/24/60/60);
    if((Node0 != IP) ||(NrOfNodes > nodes) || (end<now)){
	printf("%d[%x!=%x] %d %d\n",(Node0 != IP), Node0, IP,
	       (NrOfNodes > nodes), (end<now));

	return 0;
    }else
	return version;
}
/*--------------------------------------------------------------------*/
/*--------------------------------------------------------------------*/

