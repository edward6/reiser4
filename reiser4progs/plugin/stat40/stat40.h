/*
    stat40.h -- reiser4 default stat data structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef STAT40_H
#define STAT40_H

/* 
    This is enen not minimal stat data. Object can live without 
    stat data at all, just do not allow to link to it. Or size
    could be stored in the container if there are objects of 
    the same size only. 
*/
struct reiserfs_stat40_base {
    uint16_t mode;
    uint16_t extmask;
    uint32_t nlink;
    uint64_t size;
};

typedef struct reiserfs_stat40_base reiserfs_stat40_base_t;  

#define stat40_get_mode(stat)		get_le16(stat, mode)
#define stat40_set_mode(stat, val)	set_le16(stat, mode, val)

#define stat40_get_extmask(stat)	get_le16(stat, extmask)
#define stat40_set_extmask(stat, val)	set_le16(stat, extmask, val)

#define stat40_get_nlink(stat)		get_le32(stat, nlink)
#define stat40_set_nlink(stat, val)	set_le32(stat, nlink, val)

#define stat40_get_size(stat)		get_le64(stat, size)
#define stat40_set_size(stat, val)	set_le64(stat, size, val)


/* stat-data extension. Please order this by presumed frequency of use */
typedef enum {
    /* 
	data required to implement unix stat(2) call. Layout is in
	reiserfs_unix_stat. If this is not present, file is light-weight 
    */
    UNIX_STAT,
    /* 
	if this is present, file is controlled by non-standard
        plugin (that is, plugin that cannot be deduced from file
        mode bits), for example, aggregation, interpolation etc. 
    */
    PLUGIN_STAT,
    /* 
	this extension contains inode generation and persistent inode
        flags. Layout is in reiserfs_gen_and_flags_stat 
    */
    GEN_AND_FLAGS_STAT,
    /* 
	this extension contains capabilities sets, associated with this
        file. Layout is in reiserfs_capabilities_stat
    */
    CAPABILITIES_STAT,
    /* 
	this contains additional set of 32bit [anc]time fields to
        implement 64bit times a la BSD. Layout is in reiserfs_large_times_stat 
    */
    LARGE_TIMES_STAT,
    LAST_SD_EXTENSION,
    LAST_IMPORTANT_SD_EXTENSION = PLUGIN_STAT,
} reiserfs_stat_extentions;

struct reiserfs_unix_stat {
    /*  0 */ uint32_t uid;       /* owner id */
    /*  4 */ uint32_t gid;       /* group id */
    /*  8 */ uint32_t atime;     /* access time */
    /* 12 */ uint32_t mtime;     /* modification time */
    /* 16 */ uint32_t ctime;     /* change time */
    /* 20 */ uint32_t rdev;      /* minor:major for device files */
    /* 24 */ uint64_t bytes;     /* bytes used by file */
    /* 32 */
};

typedef struct reiserfs_unix_stat reiserfs_unix_stat_t;

struct reiserfs_plugin_slot {
    /*  0 */ uint16_t type_id;
    /*  2 */ uint16_t id;
    /*  4 here plugin stores its persistent state */
};

typedef struct reiserfs_plugin_slot reiserfs_plugin_slot_t;

/* stat-data extension for files with non-standard plugin. */
struct reiserfs_plugin_stat {
    /* number of additional plugins, associated with this object */
    /*  0 */ uint16_t plugins_no;
    /*  2 */ reiserfs_plugin_slot_t slot[0];
    /*  2 */
};

typedef struct reiserfs_plugin_stat reiserfs_plugin_stat_t;

struct reiserfs_gen_and_flags_stat {
    /*  0 */ uint32_t generation;
    /*  4 */ uint32_t flags;
    /*  8 */
};

typedef struct reiserfs_gen_and_flags_stat reiserfs_gen_and_flags_stat_t;

struct reiserfs_capabilities_stat {
    /*  0 */ uint32_t effective;
    /*  8 */ uint32_t permitted;
    /* 16 */
};

typedef struct reiserfs_capabilities_stat reiserfs_capabilities_stat_t;

#endif

