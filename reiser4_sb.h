/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definition of reiser4-specific part of in-memory VFS struct inode
 */

#if !defined( __REISER4_SB_H__ )
#define __REISER4_SB_H__

/* VFS only needs size of reiser4-specific super-block information.  Our part
   of super-block is composed of data-structures whose definition relies on a
   bunch of header files. We want neither to put everything into one
   file, nor to pollute include/linux. So, we put here stub declaration
   that only reserves space. In our code (fs/reiser4/super.c) there is
   compile time assertion that triggers if not enough space were reserved
   here. Real declaration of reiser4-specific part of super-block is in
   fs/reiser4/super.h:reiser4_super_info_data. */
#if REISER4_STATS
#define REISER4_STUB_SUPER_INFO_LENGTH    (20480)
#else
#define REISER4_STUB_SUPER_INFO_LENGTH    (2048)
#endif

/*
struct reiser4_sb_info {
	char padding[ REISER4_STUB_SUPER_INFO_LENGTH ];
};
*/

/* __REISER4_SB_H__ */
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
