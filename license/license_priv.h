
#ifndef _LICENSE_PRIV_H_
#define _LICENSE_PRIV_H_

#include "license_pub.h"


typedef struct pslic_ascii_T{
    char	Cpt[80];	// Probably "COPYRIGHT (c) ParTec 2001"
    char	Product[80];	// Probably "ParaStation 3.0"
    char	Name[80];	// Customer name
    char	LicKey[16];	// LicKey
    char	Date[20];	// Creation time
    char	Expire[20];     // Days
    char	Nodes[10];	// No of nodes
    char	Arch[10];	// Architecture of host
    char	Features[20];	// Bitcoded features (dec)
    char	Hash[40];	// Overall hash
}pslic_ascii_t;


typedef struct pslic_bin_T{
    int32_t	Nodes;
    uint32_t	ValidFrom;
    uint32_t	ValidTo;
    uint32_t	Magic;
}pslic_bin_t;


//                         0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
#define LIC16SHUFFELEnc "\xf\x2\x1\x7\x9\xa\x6\x3\xb\xc\xd\xe\x4\x5\x8\x0"
#define LIC16SHUFFELDec "\xf\x2\x1\x7\xc\xd\x6\x3\xe\x4\x5\x8\x9\xa\xb\x0"

#define LIC_KEYMAGIC 0x4A656E73

#define pslic_encode(buf,buflen,magic)					\
{									\
    uint8_t *buf_=(uint8_t *)(buf);					\
    int buflen_ = buflen;						\
    uint32_t magic_ = magic;						\
    while (buflen_--){							\
	(*buf_) =							\
	    (LIC16SHUFFELEnc[ (*buf_) & 0xf ]) |			\
	    (LIC16SHUFFELEnc[ (((*buf_) ^ magic_) >> 4) & 0xf ]<<4);	\
	magic_+= *buf_;							\
	buf_++;								\
    }									\
}


#define pslic_decode(buf,buflen,magic)						\
{										\
    uint8_t *buf_=(uint8_t *)(buf);						\
    int buflen_ = buflen;							\
    uint32_t magic_ = magic;							\
    while (buflen_--){								\
	uint8_t ch;								\
	ch = *buf_;								\
	(*buf_) =								\
	    (LIC16SHUFFELDec[ (*buf_) & 0xf ]) |				\
	    (((LIC16SHUFFELDec[ ((*buf_) >> 4) & 0xf ]<<4)^ magic_)&0xf0);	\
	magic_+= ch;								\
	buf_++;									\
    }										\
}

#endif /* _LICENSE_PRIV_H_ */


















