/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/* 

This describes the static_stat item, used to hold all information needed by the stat() syscall.

In the case where each file has not less than the fields needed by the
stat() syscall, it is more compact to store those fields in this
struct.

If this item does not exist, then all stats are dynamically resolved.
At the moment, we either resolve all stats dynamically or all of them
statically.  If you think this is not fully optimal, and the rest of
reiser4 is working, then fix it...:-)

 */

#if !defined( __FS_REISER4_PLUGIN_ITEM_STATIC_STAT_H__ )
#define __FS_REISER4_PLUGIN_ITEM_STATIC_STAT_H__

/* Stat data layout: goals and implementation.  

   Reiser4 should support extensible means to store attributes with each
   file. It also should support light-weight files, that is, files that
   are so cheap space-wise, that creation and access to small files
   (<50b) is not prohibitively expensive. This was among main goals of
   reiserfs from its inception.

   There are attributes that will be always read on access to a file if
   present: inode generation, persistent inode attributes, capabilities,
   object plugin etc. It would be too expensive to store such attributes
   as separate items only to launch a bunch of additional tree lookups
   immediately after reading stat data. Our decision is to store all
   such attributes in the stat data item making it of variable size. The
   same idea allows us to implement light-weight files efficiently:
   minimal stat data only consist of fields that are absolutely
   necessary to perform standard set of file operations. This minimal
   stat-data (reiser4_stat_data_base) is 16 bytes in size and only
   contains file mode (that is necessary to install proper object
   plugin), nlink (required for link, unlink and rename) and size (to do
   read/write).
   
   This minimal stat data also contains "extension bitmask": each bit in
   it indicates presence or absence of or particular stat data extension
   (see sd_ext_bits enum). For example if bit 0 is set (as it will be in
   most normal installations), minimal stat data would be immediately
   followed by the rest of standard UNIX inode fields: owner and group
   id, times, bytes actually used by file (used to calculate i_block
   field). If this bit is 0, we have light-weight file whose attributes
   are either inherited from parent directory (as uid, gid) or
   initialised to some sane values.

   To capitalize on existing code infrastructure, extensions are
   implemented as plugins of type REISER4_SD_EXT_PLUGIN_TYPE.
   Each stat-data extension plugin implements four methods:

    ->present() called by sd_load() when this extension is found in stat-data
    ->absent() called by sd_load() when this extension is not found in stat-data
    ->save_len() called by sd_len() to calculate total length of stat-data
    ->save() called by sd_save() to store extension data into stat-data

    Implementation is in fs/reiser4/plugin/item/static_stat.c
*/


/** stat-data extension. Please order this by presumed frequency of use */
typedef enum {
	/** data required to implement unix stat(2) call. Layout is in
	    reiser4_unix_stat. If this is not present, file is light-weight */
	UNIX_STAT, 
	/* stat data has link name included */
	SYMLINK_STAT,
	/** if this is present, file is controlled by non-standard
	    plugin (that is, plugin that cannot be deduced from file
	    mode bits), for example, aggregation, interpolation etc. */
	PLUGIN_STAT, 
	/** this extension contains inode generation and persistent inode
	    flags. Layout is in reiser4_gen_and_flags_stat */
	GEN_AND_FLAGS_STAT, 
	/** this extension contains capabilities sets, associated with this
	    file. Layout is in reiser4_capabilities_stat */
	CAPABILITIES_STAT, 
	/** this contains additional set of 32bit [anc]time fields to
	    implement 64bit times a la BSD. Layout is in
	    reiser4_large_times_stat */
	LARGE_TIMES_STAT,
	LAST_SD_EXTENSION,
	LAST_IMPORTANT_SD_EXTENSION = PLUGIN_STAT,
} sd_ext_bits;

/* this is not minimal.  I used to think that size was the only stat
   data which cannot be eliminated/inherited, but then I realized that
   if you have a directory of files of equal size, you could cause
   them to be somehow specified by the directory.  Give some thought
   to how we can allow for directories specifying characteristics of
   their children if something about the children (a bit flag?) says
   to check the object whose oid equals the packing locality for the
   existence of such specifications.  Note that once lightweight files
   exist, you need compressed item headers to make the solution
   complete.  Not until v4.1, or later.  */
/** minimal stat-data. This allows to support light-weight files. */
typedef struct reiser4_stat_data_base {
	/*  0 */ d16 mode;
	/** bitmask indicating what pieces of data (extensions) follow
	    this minimal stat-data */
	/*  2 */ d16 extmask;
	/*  4 */ d32 nlink;
	/*  8 */ d64 size;	/* size in bytes */
	/* 16 */
} reiser4_stat_data_base;

typedef struct reiser4_unix_stat {
	/*  0 */ d32 uid;	/* owner id */
	/*  4 */ d32 gid;	/* group id */
	/*  8 */ d32 atime;	/* access time */
	/* 12 */ d32 mtime;	/* modification time */
	/* 16 */ d32 ctime;	/* change time */
	/* 20 */ d32 rdev;	/* minor:major for device files */
	/* 24 */ d64 bytes;     /* bytes used by file */
	/* 32 */
} reiser4_unix_stat;

typedef struct reiser4_plugin_slot {
	/*  0 */ d16 type_id;
	/*  2 */ d16 id;
	/*  4 */ /* here plugin stores its persistent state */
} reiser4_plugin_slot;

/** stat-data extension for files with non-standard plugin. */
typedef struct reiser4_plugin_stat {
	/** number of additional plugins, associated with this object */
	/*  0 */ d16 plugins_no;
	/*  2 */ reiser4_plugin_slot slot[ 0 ];
	/*  2 */
} reiser4_plugin_stat;

typedef struct reiser4_gen_and_flags_stat {
	/*  0 */ d32 generation;
	/*  4 */ d32 flags;
	/*  8 */
} reiser4_gen_and_flags_stat;

typedef struct reiser4_capabilities_stat {
	/*  0 */ d32 effective;
	/*  8 */ d32 permitted;
	/* 16 */
} reiser4_capabilities_stat;

typedef struct reiser4_large_times_stat {
	/*  0 */ d32 atime;	/* access time */
	/*  8 */ d32 mtime;	/* modification time */
	/* 16 */ d32 ctime;	/* change time */
	/* 24 */
} reiser4_large_times_stat;

/* plugin->item.common.* */
extern void sd_print( const char *prefix, coord_t *coord );

/* plugin->item.s.sd.* */
extern int sd_load( struct inode *inode, char *sd, int len );
extern int sd_len( struct inode *inode );
extern int sd_save( struct inode *inode, char **area );

/* __FS_REISER4_PLUGIN_ITEM_STATIC_STAT_H__ */
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
