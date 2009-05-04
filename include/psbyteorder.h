/*
 *               ParaStation
 *
 * Copyright (C) 2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file ParaStation network byteorder functions.
 *
 * For ParaStation little-endian byteorder was chosen as the
 * network-byteorder. This was done for compatibility reasons with
 * older version on x86 systems. Be aware of the fact that for the IP
 * protocol suite big-endian byteorder is chosen on the wire.
 *
 * This file defines six helper functions that convert values of size
 * 16 bit, 32 bit and 64 bit from and to this above defined network
 * byteorder on different architectures.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef _PSBYTEORDER_H_
#define _PSBYTEORDER_H_

#include <endian.h>
#include <byteswap.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
/** Convert the 64 bit value @a x to host-byteorder */
#  define psntoh64(x)   __bswap_64 (x)
/** Convert the 32 bit value @a x to host-byteorder */
#  define psntoh32(x)   __bswap_32 (x)
/** Convert the 16 bit value @a x to host-byteorder */
#  define psntoh16(x)   __bswap_16 (x)
/** Convert the 64 bit value @a x to network-byteorder */
#  define pshton64(x)   __bswap_64 (x)
/** Convert the 32 bit value @a x to network-byteorder */
#  define pshton32(x)   __bswap_32 (x)
/** Convert the 16 bit value @a x to network-byteorder */
#  define pshton16(x)   __bswap_16 (x)
#else
#  if __BYTE_ORDER == __LITTLE_ENDIAN
/* The host byte order is the same as network byte order,
   so these functions are all just identity.  */
/** Convert the 64 bit value @a x to host-byteorder */
#    define psntoh64(x) (x)
/** Convert the 32 bit value @a x to host-byteorder */
#    define psntoh32(x) (x)
/** Convert the 16 bit value @a x to host-byteorder */
#    define psntoh16(x) (x)
/** Convert the 64 bit value @a x to network-byteorder */
#    define pshton64(x) (x)
/** Convert the 32 bit value @a x to network-byteorder */
#    define pshton32(x) (x)
/** Convert the 16 bit value @a x to network-byteorder */
#    define pshton16(x) (x)
#  else
#    error "Unknown byteorder"
#  endif
#endif

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _PSBYTEORDER_H_ */
