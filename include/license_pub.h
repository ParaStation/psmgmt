/*
 *               ParaStation
 * license_pub.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: license_pub.h,v 1.5 2004/06/15 13:47:30 eicker Exp $
 *
 */
/**
 * @file
 *
 * license_pub.h: Public definitions and functions for licensing.
 *
 * $Id: license_pub.h,v 1.5 2004/06/15 13:47:30 eicker Exp $
 *
 * @author
 * Jens Hauke <hauke@par-tec.com>
 *
 */
#ifndef _LICENSE_PUB_H_
#define _LICENSE_PUB_H_

#include <stdint.h>

typedef struct pslic_binpub_T{
    uint32_t	param[4];
}pslic_binpub_t;

//  0               1               2               3         
//  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
#define LICBITPACK_B2A(bin)  ((unsigned char)				\
  ("abcdefgh%jklmnopqrstuvwxyzABCDEFGH@JKLMN*PQRSTUVWXYZ?=23456789+-"	\
   [(bin) & 0x3f]))

static unsigned char ARRAY_LICBITPACK_A2B[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
    0x3f, 0x0b, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, /* _!"#$%&' */
    0x00, 0x00, 0x28, 0x3e, 0x00, 0x3f, 0x00, 0x00, /* ()*+,-./ */
    0x0e, 0x0b, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, /* 01234567 */
    0x3c, 0x3d, 0x00, 0x00, 0x00, 0x35, 0x00, 0x34, /* 89:;<=>? */
    0x22, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, /* @ABCDEFG */
    0x21, 0x0b, 0x23, 0x24, 0x25, 0x26, 0x27, 0x0e, /* HIJKLMNO */
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, /* PQRSTUVW */
    0x31, 0x32, 0x33, 0x0b, 0x00, 0x0b, 0x00, 0x3f, /* XYZ[\]^_ */
    0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* `abcdefg */
    0x07, 0x0b, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, /* hijklmno */
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, /* pqrstuvw */
    0x17, 0x18, 0x19, 0x00, 0x0b, 0x00, 0x00, 0x00  /* xyz{|}~. */
};

#define LICBITPACK_A2B(asci) (ARRAY_LICBITPACK_A2B[(asci) & 0x7f])

#define pslic_alen2blen(alen) (((alen) * 6) / 8)
#define pslic_blen2alen(blen)    (((blen) * 8 + 5) / 6)

#ifdef __DECC
#define inline
#endif

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

#ifdef __DECC
#undef inline
#endif


#endif /* _LICENSE_PUB_H_ */
