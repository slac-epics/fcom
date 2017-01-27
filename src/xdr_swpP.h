/* $Id: xdr_swpP.h,v 1.3 2010/04/22 02:08:27 strauman Exp $ */
#ifndef XDR_BYTESWAP_PRIVATE_H
#define XDR_BYTESWAP_PRIVATE_H

/* Low-level, inline byte-swapping operations for XDR encoder/decoder.
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */

#if defined(__PPC__)
#include <ppu_intrinsics.h>
#endif

#if ! defined(__i386__) && !defined(__x86_64__) \
    && !defined(__m68k__) && !defined(__arm__) \
    && !defined(__PPC__) && !defined(__ppc__)
#error "Unknown CPU; add to test if it uses IEEE floating-point format"
/* Only CPUs using IEEE floating point format supported so far;
 * Also, we assume floats and doubles are stored with native
 * endianness, i.e. we may byteswap them just as any other
 * objects.
 */
#endif

#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
    #if defined(__i386__) || defined(__x86_64__) || defined(__arm__)
        #define __LITTLE_ENDIAN__
    #elif defined(__m68k__)
        #define __BIG_ENDIAN__
    #else
        #error "neither __LITTLE_ENDIAN__ nor __BIG_ENDIAN__ defined and CPU unknown"
    #endif
#endif


#if defined(__BIG_ENDIAN__)
#define SWAPU32(x) (x)
#elif defined(__LITTLE_ENDIAN__)
  #if defined(__GNUC__)
    #if 4 < __GNUC__ || ( 4 == __GNUC__ && 3 <= __GNUC_MINOR__ )
      /* don't know when __builtin_swap32 appeared -- seems with gcc 4.3;
       * NOTE: for gcc to use the 'bswap' instruction you need
       * to say "-march=pentium"
       */
      #if defined(__i386__) && !defined(__pentium__)
        #warning "x86 < pentium does NOT use bswap; must use -march=pentium to enable"
      #endif
      #define SWAPU32(x) __builtin_bswap32(x)
    #else
      static __inline__ uint32_t swapu32(uint32_t x)
      {
      __asm__ volatile("bswap %0":"=r"(x):"0"(x));
      return x;
      }
      #define SWAPU32(x) swapu32(x)
    #endif
  #endif

  #ifndef SWAPU32
    #warning "Should implement efficient byte-swapping for this CPU"
	static __inline__ uint32_t SWAPU32(uint32_t x)
	{
		x = ( (x<<16) & 0xffff0000 ) | ( (x>>16) & 0x0000ffff );
		x = ( (x<< 8) & 0xff00ff00 ) | ( (x>> 8) & 0x00ff00ff );
		return x;
	}
  #endif
#else
#error "Unknown CPU endianness"
#endif

#endif
