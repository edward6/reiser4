/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Perm (short for "permissions") plugins common stuff.
 */

#if !defined( __REISER4_PERM_H__ )
#define __REISER4_PERM_H__


typedef struct perm_plugin {
	/** check permissions for read/write */
	int ( *rw_ok )( struct file *file, const char *buf, 
			size_t size, loff_t *off, rw_op op );

	/** check permissions for lookup */
	int ( *lookup_ok )( struct inode *parent, struct dentry *dentry );

	/** check permissions for create */
	int ( *create_ok )( struct inode *parent, struct dentry *dentry, 
			    reiser4_object_create_data *data );

	/** check permissions for linking @where to @existing */
	int ( *link_ok )( struct dentry *existing, struct inode *parent, 
			  struct dentry *where );

	/** check permissions for unlinking @victim from @parent */
	int ( *unlink_ok )( struct inode *parent, struct dentry *victim );

	/** check permissions for deletion of @object whose last reference is by @parent */
	int ( *delete_ok )( struct inode *parent, struct dentry *victim );

	/** check UNIX access bits. This is ->permission() check called by VFS.  This needs to be eliminated if we are
	 * to have real security plugins.  */
	int ( *mask_ok )( struct inode *inode, int mask );
} perm_plugin;

/** call ->check_ok method of perm plugin for inode */
/* you have made assumptions about how a security plugin will work.  This is incompatible with such things as asking a
   user space program to determine if permission is not granted.  */

/* one of us needs to look at lsm, and analyze if what we are doing is consistent with that.... -Hans */
#if 0
#define perm_chk( inode, check, args... )			\
({								\
	perm_plugin *perm;					\
								\
	perm = reiser4_get_object_state( inode ) -> perm;	\
	( ( perm != NULL ) &&					\
	  ( perm -> ## check ## _ok != NULL ) &&	\
	    perm -> ## check ## _ok( ##args ) );	\
})
#endif /* 0 */

typedef enum { RWX_PERM_ID, LAST_PERM_ID } reiser4_perm_id;

/* __REISER4_PERM_H__ */
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
