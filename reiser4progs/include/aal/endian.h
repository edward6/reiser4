/*
    endian.h -- endianess translation macros. This is a number of macro
    because macro is better for performance than to use functions which are
    determining the translation kind in the run time.
    
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifndef ENDIAN_H
#define ENDIAN_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#if defined(__sparc__) || defined(__sparcv9)
#  include <sys/int_types.h>
#else
#  include <stdint.h>
#endif

#define __aal_get_octave(x, n)		( ((x) >> (8 * (n))) & 0xff )

#define __aal_swap16(x)			( (__aal_get_octave(x, 0) << 8)		\
					+ (__aal_get_octave(x, 1) << 0) )
	
#define __aal_swap32(x)			( (__aal_get_octave(x, 0) << 24)	\
					+ (__aal_get_octave(x, 1) << 16)	\
					+ (__aal_get_octave(x, 2) << 8)		\
					+ (__aal_get_octave(x, 3) << 0) )
	
#define __aal_swap64(x)			( (__aal_get_octave(x, 0) << 56)	\
					+ (__aal_get_octave(x, 1) << 48)	\
					+ (__aal_get_octave(x, 2) << 40)	\
					+ (__aal_get_octave(x, 3) << 32)	\
					+ (__aal_get_octave(x, 4) << 24)	\
					+ (__aal_get_octave(x, 5) << 16)	\
					+ (__aal_get_octave(x, 6) << 8)		\
					+ (__aal_get_octave(x, 7) << 0) )

#define aal_swap16(x)			((uint16_t) __aal_swap16((uint16_t)x))
#define aal_swap32(x)			((uint32_t) __aal_swap32((uint32_t)x))
#define aal_swap64(x)			((uint64_t) __aal_swap64((uint64_t)x))

/*
    Endianess is determined by configure script in the configuring time, that is 
    before compiling the package.
*/
#ifdef WORDS_BIGENDIAN

#  define CPU_TO_LE16(x)		aal_swap16(x)
#  define CPU_TO_LE32(x)		aal_swap32(x)
#  define CPU_TO_LE64(x)		aal_swap64(x)
#  define LE16_TO_CPU(x)		aal_swap16(x)
#  define LE32_TO_CPU(x)		aal_swap32(x)
#  define LE64_TO_CPU(x)		aal_swap64(x)

#else

#  define CPU_TO_LE16(x)		(x)
#  define CPU_TO_LE32(x)		(x)
#  define CPU_TO_LE64(x)		(x)
#  define LE16_TO_CPU(x)		(x)
#  define LE32_TO_CPU(x)		(x)
#  define LE64_TO_CPU(x)		(x)

#endif

extern inline int aal_set_bit (int nr, void *addr);
extern inline int aal_clear_bit (int nr, void *addr);
extern inline int aal_test_bit(int nr, const void *addr);
extern inline int aal_find_first_zero_bit (const void *vaddr, unsigned size);
extern inline int aal_find_next_zero_bit (const void *vaddr, unsigned size, unsigned offset);

#define aal_get_leXX(xx, p, field)	(LE##xx##_TO_CPU ((p)->field))
#define aal_set_leXX(xx, p, field, val)	do { (p)->field = CPU_TO_LE##xx(val); } while (0)

#define aal_get_le16(p, field) 		aal_get_leXX(16, p, field)
#define aal_set_le16(p, field, val) 	aal_set_leXX(16, p, field, val)

#define aal_get_le32(p, field) 		aal_get_leXX(32, p, field)
#define aal_set_le32(p, field, val)	aal_set_leXX(32, p, field, val)

#define aal_get_le64(p, field) 		aal_get_leXX(64, p, field)
#define aal_set_le64(p, field, val) 	aal_set_leXX(64, p, field, val)

#endif

