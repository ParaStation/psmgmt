
#ifndef _LICENSE_PUB_H_
#define _LICENSE_PUB_H_


typedef struct pslic_binpub_T{
    UINT32	param[4];
}pslic_binpub_t;

//  0               1               2               3         
//  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
#define LICBITPACK_B2A(bin)  ((unsigned char)				\
  ("abcdefgh%jklmnopqrstuvwxyzABCDEFGH@JKLMN*PQRSTUVWXYZ?=23456789+-"	\
   [(bin) & 0x3f]))

#define LICBITPACK_A2B(asci) ((unsigned char)(		\
"\x00\x00\x00\x00\x00\x00\x00\x00" /* ........ */	\
"\x00\x00\x00\x00\x00\x00\x00\x00" /* ........ */	\
"\x00\x00\x00\x00\x00\x00\x00\x00" /* ........ */	\
"\x00\x00\x00\x00\x00\x00\x00\x00" /* ........ */	\
"\x3f\x0b\x00\x00\x00\x08\x00\x00" /* _!"#$%&' */	\
"\x00\x00\x28\x3e\x00\x3f\x00\x00" /* ()*+,-./ */	\
"\x0e\x0b\x36\x37\x38\x39\x3a\x3b" /* 01234567 */	\
"\x3c\x3d\x00\x00\x00\x35\x00\x34" /* 89:;<=>? */	\
"\x22\x1a\x1b\x1c\x1d\x1e\x1f\x20" /* @ABCDEFG */	\
"\x21\x0b\x23\x24\x25\x26\x27\x0e" /* HIJKLMNO */	\
"\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30" /* PQRSTUVW */	\
"\x31\x32\x33\x0b\x00\x0b\x00\x3f" /* XYZ[\]^_ */	\
"\x00\x00\x01\x02\x03\x04\x05\x06" /* `abcdefg */	\
"\x07\x0b\x09\x0a\x0b\x0c\x0d\x0e" /* hijklmno */	\
"\x0f\x10\x11\x12\x13\x14\x15\x16" /* pqrstuvw */	\
"\x17\x18\x19\x00\x0b\x00\x00\x00" /* xyz{|}~. */	\
[(asci) & 0x7f]))

#define pslic_alen2blen(alen) (((alen) * 6) / 8)
#define pslic_blen2alen(blen)    (((blen) * 8 + 5) / 6)



static inline void
pslic_ascii2bin(unsigned char *ascii,int alen,unsigned char *bin ){
    unsigned int tmp=0;
    int bits=-8;
    while (alen--){
	tmp= (tmp << 6) | LICBITPACK_A2B(*ascii++);
	bits+=6;
	if (bits >=0){
	    *bin++ = tmp >> bits;
	    bits-=8;
	}
    };
}

static inline void
pslic_bin2ascii(unsigned char *bin,int blen,unsigned char *ascii ){
    unsigned int tmp=0;
    int bits=-6;
    while (blen--){
	tmp = (tmp << 8)| *bin++;
	bits+=8;
	while (bits >=0){
	    *ascii++ = LICBITPACK_B2A(tmp >> bits);
	    bits-=6;
	}
    };
    if (bits >-6){
	tmp = (tmp << 8);
	bits+=8;
	*ascii++ = LICBITPACK_B2A(tmp >> bits);
    }
    
}



#endif /* _LICENSE_PUB_H_ */














