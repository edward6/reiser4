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

#define get_byte(x, n)			( ((x) >> (8 * (n))) & 0xff )

#define __swap16(x)			( (get_byte(x, 0) << 8)		\
					+ (get_byte(x, 1) << 0) )
	
#define __swap32(x)			( (get_byte(x, 0) << 24)	\
					+ (get_byte(x, 1) << 16)	\
					+ (get_byte(x, 2) << 8)		\
					+ (get_byte(x, 3) << 0) )
	
#define __swap64(x)			( (get_byte(x, 0) << 56)	\
					+ (get_byte(x, 1) << 48)	\
					+ (get_byte(x, 2) << 40)	\
					+ (get_byte(x, 3) << 32)	\
					+ (get_byte(x, 4) << 24)	\
					+ (get_byte(x, 5) << 16)	\
					+ (get_byte(x, 6) << 8)		\
					+ (get_byte(x, 7) << 0) )

#define swap16(x)			((uint16_t) __swap16( (uint16_t) x ))
#define swap32(x)			((uint32_t) __swap32( (uint32_t) x ))
#define swap64(x)			((uint64_t) __swap64( (uint64_t) x ))

/*
    Endianess is determined by configure script in the configuring time, that is 
    before compiling the package.
*/
#ifdef WORDS_BIGENDIAN

#  define CPU_TO_LE16(x)		swap16(x)
#  define CPU_TO_BE16(x)		(x)
#  define CPU_TO_LE32(x)		swap32(x)
#  define CPU_TO_BE32(x)		(x)
#  define CPU_TO_LE64(x)		swap64(x)
#  define CPU_TO_BE64(x)		(x)

#  define LE16_TO_CPU(x)		swap16(x)
#  define BE16_TO_CPU(x)		(x)
#  define LE32_TO_CPU(x)		swap32(x)
#  define BE32_TO_CPU(x)		(x)
#  define LE64_TO_CPU(x)		swap64(x)
#  define BE64_TO_CPU(x)		(x)

inline int aal_be_set_bit (int nr, void *addr);
inline int aal_be_clear_bit (int nr, void *addr);
inline int aal_be_test_bit(int nr, const void *addr);

# define set_bit(nr, addr)		aal_be_set_bit(nr, addr)
# define clear_bit(nr, addr)		aal_be_clear_bit(nr, addr)
# define test_bit(nr, addr)		aal_be_test_bit(nr, addr)

#else

#  define CPU_TO_LE16(x)		(x)
#  define CPU_TO_BE16(x)		swap16(x)
#  define CPU_TO_LE32(x)		(x)
#  define CPU_TO_BE32(x)		swap32(x)
#  define CPU_TO_LE64(x)		(x)
#  define CPU_TO_BE64(x)		swap64(x)

#  define LE16_TO_CPU(x)		(x)
#  define BE16_TO_CPU(x)		swap16(x)
#  define LE32_TO_CPU(x)		(x)
#  define BE32_TO_CPU(x)		swap32(x)
#  define LE64_TO_CPU(x)		(x)
#  define BE64_TO_CPU(x)		swap64(x)

inline int le_set_bit (int nr, void *addr);
inline int le_clear_bit (int nr, void *addr);
inline int le_test_bit(int nr, const void *addr);

# define set_bit(nr, addr)		aal_le_set_bit(nr, addr)
# define clear_bit(nr, addr)		aal_le_clear_bit(nr, addr)
# define test_bit(nr, addr)		aal_le_test_bit(nr, addr)

#endif

#define get_leXX(xx, p, field)		(LE##xx##_TO_CPU ((p)->field))
#define set_leXX(xx, p, field, val)	do { (p)->field = CPU_TO_LE##xx(val); } while (0)

#define get_le16(p, field) 		get_leXX(16, p, field)
#define set_le16(p, field, val) 	set_leXX(16, p, field, val)

#define get_le32(p, field) 		get_leXX(32, p, field)
#define set_le32(p, field, val)		set_leXX(32, p, field, val)

#define get_le64(p, field) 		get_leXX(64, p, field)
#define set_le64(p, field, val) 	set_leXX(64, p, field, val)

#endif

