/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definition of reiser4-specific part of in-memory VFS struct inode
 */

#if !defined( __REISER4_I_H__ )
#define __REISER4_I_H__

/* VFS only needs size of reiser4-specific inode information.  Our part
   of inode is composed of data-structures whose definition relies on a
   bunch of header files. We want neither to put everything into one
   file, nor to pollute include/linux. So, we put here stub declaration
   that only reserves space. In our code (fs/reiser4/inode.c) there is
   compile time assertion that triggers if not enough space were reserved
   here. Real declaration of reiser4-specific part of inode is in
   fs/reiser4/inode.h:reiser4_inode_info_data. */
#define REISER4_STUB_INODE_INFO_LENGTH    (100)

struct reiser4_inode_info {
	char padding[ REISER4_STUB_INODE_INFO_LENGTH ];
};

/* __REISER4_I_H__ */
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
