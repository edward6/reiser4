/*
    stat40.h -- reiser4 default stat data structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef STAT40_H
#define STAT40_H

/* 
    This is even not minimal stat data. Object can live without 
    stat data at all, just do not allow to link to it. Or size
    could be stored in the container if there are objects of 
    the same size only. 
*/
struct stat40_base {
    uint16_t mode;
    uint16_t extmask;
    uint32_t nlink;
    uint64_t size;
};

typedef struct stat40_base stat40_base_t;  

#define stat40_get_mode(stat)		get_le16(stat, mode)
#define stat40_set_mode(stat, val)	set_le16(stat, mode, val)

#define stat40_get_extmask(stat)	get_le16(stat, extmask)
#define stat40_set_extmask(stat, val)	set_le16(stat, extmask, val)

#define stat40_get_nlink(stat)		get_le32(stat, nlink)
#define stat40_set_nlink(stat, val)	set_le32(stat, nlink, val)

#define stat40_get_size(stat)		get_le64(stat, size)
#define stat40_set_size(stat, val)	set_le64(stat, size, val)


/* 
    Stat-data extension. Please order this by presumed 
    frequency of use.
*/
typedef enum {
    /* 
	Data required to implement unix stat(2) call. Layout is in
	reiserfs_unix_stat. If this is not present, file is light-weight 
    */
    UNIX_STAT,
    /* 
	If this is present, file is controlled by non-standard
        plugin (that is, plugin that cannot be deduced from file
        mode bits), for example, aggregation, interpolation etc. 
    */
    PLUGIN_STAT,
    /* 
	This extension contains inode generation and persistent inode
        flags. Layout is in reiserfs_gen_and_flags_stat 
    */
    GEN_AND_FLAGS_STAT,
    /* 
	This extension contains capabilities sets, associated with this
        file. Layout is in reiserfs_capabilities_stat
    */
    CAPABILITIES_STAT,
    /* 
	This contains additional set of 32bit [anc]time fields to
        implement 64bit times a la BSD. Layout is in reiserfs_large_times_stat 
    */
    LARGE_TIMES_STAT,
    LAST_SD_EXTENSION,
    LAST_IMPORTANT_SD_EXTENSION = PLUGIN_STAT,
} reiserfs_stat_extentions;

struct reiserfs_unix_stat {
    uint32_t uid;       /* owner id */
    uint32_t gid;       /* group id */
    uint32_t atime;     /* access time */
    uint32_t mtime;     /* modification time */
    uint32_t ctime;     /* change time */
    uint32_t rdev;      /* minor:major for device files */
    uint64_t bytes;     /* bytes used by file */
};

typedef struct reiserfs_unix_stat reiserfs_unix_stat_t;

struct reiserfs_plugin_slot {
    uint16_t type_id;
    uint16_t id;
};

typedef struct reiserfs_plugin_slot reiserfs_plugin_slot_t;

/* 
    Stat-data extension for files with non-standard 
    plugin. 
*/
struct reiserfs_plugin_stat {
    /* 
	Number of additional plugins, associated with 
	this object.
    */
    uint16_t plugins_no;
    reiserfs_plugin_slot_t slot[0];
};

typedef struct reiserfs_plugin_stat reiserfs_plugin_stat_t;

struct reiserfs_gen_and_flags_stat {
    uint32_t generation;
    uint32_t flags;
};

typedef struct reiserfs_gen_and_flags_stat reiserfs_gen_and_flags_stat_t;

struct reiserfs_capabilities_stat {
    uint32_t effective;
    uint32_t permitted;
};

typedef struct reiserfs_capabilities_stat reiserfs_capabilities_stat_t;

#endif

