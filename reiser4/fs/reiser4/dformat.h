/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Formats of on-disk data and conversion functions.
 */

/* put all item formats in the files describing the particular items,
   our model is, everything you need to do to add an item to reiser4,
   (excepting the changes to the plugin that uses the item which go
   into the file defining that plugin), you put into one file. */
/*
 * Data on disk are stored in little-endian format.
 * To declare fields of on-disk structures, use d8, d16, d32 and d64.
 * d??tocpu() and cputod??() to convert.
 */

#if !defined( __FS_REISER4_DFORMAT_H__ )
#define __FS_REISER4_DFORMAT_H__

/* our default disk byteorder is little endian */

#if defined( __LITTLE_ENDIAN )
#define CPU_IN_DISK_ORDER  (1)
#else
#define CPU_IN_DISK_ORDER  (0)
#endif

/* code on-disk data-types as structs with a single field
   to rely on compiler type-checking. Like include/asm-i386/page.h */
typedef struct d8 { __u8 datum; } d8;
typedef struct d16 { __u16 datum; } d16;
typedef struct d32 { __u32 datum; } d32;
typedef struct d64 { __u64 datum; } d64;

static inline __u8 d8tocpu( const d8 *ondisk ) 
{ 
	return ondisk -> datum; 
}

static inline __u16 d16tocpu( const d16 *ondisk ) 
{ 
	return __le16_to_cpu( ondisk -> datum ); 
}

static inline __u32 d32tocpu( const d32 *ondisk ) 
{ 
	return __le32_to_cpu( ondisk -> datum ); 
}

static inline __u64 d64tocpu( const d64 *ondisk ) 
{ 
	return __le64_to_cpu( ondisk -> datum ); 
}

static inline d8 *cputod8( unsigned int oncpu, d8 *ondisk )
{
	assert( "nikita-1264", oncpu < 0x100 );
	ondisk -> datum = oncpu;
	return ondisk;
}

static inline d16 *cputod16( unsigned int oncpu, d16 *ondisk )
{
	assert( "nikita-1265", oncpu < 0x10000 );
	ondisk -> datum = __cpu_to_le16( oncpu );
	return ondisk;
}

static inline d32 *cputod32( __u32 oncpu, d32 *ondisk )
{
	ondisk -> datum = __cpu_to_le32( oncpu );
	return ondisk;
}

static inline d64 *cputod64( __u64 oncpu, d64 *ondisk )
{
	ondisk -> datum = __cpu_to_le64( oncpu );
	return ondisk;
}

/** data-type for block number on disk */
typedef d64 dblock_nr;
typedef __u64 block_nr;

union reiser4_disk_addr {
	block_nr     blk;
};

static inline int disk_addr_eq( const reiser4_disk_addr *b1, 
				const reiser4_disk_addr *b2 )
{
	assert( "nikita-1033", b1 != NULL );
	assert( "nikita-1266", b2 != NULL );
	
	return !memcmp( b1, b2, sizeof *b1 );
}

/* __FS_REISER4_DFORMAT_H__ */
#endif

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
