/*
    plugin.h -- reiserfs plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifndef PLUGIN_H
#define PLUGIN_H

#include <aal/aal.h>

typedef uint64_t oid_t;

typedef uint32_t reiserfs_id_t;
typedef void reiserfs_entity_t;
typedef union reiserfs_plugin reiserfs_plugin_t;

enum reiserfs_plugin_type {
    FILE_PLUGIN_TYPE,
    DIR_PLUGIN_TYPE,
    ITEM_PLUGIN_TYPE,
    NODE_PLUGIN_TYPE,
    HASH_PLUGIN_TYPE,
    DROP_POLICY_PLUGIN_TYPE,
    PERM_PLUGIN_TYPE,
    SDEXT_PLUGIN_TYPE,
    FORMAT_PLUGIN_TYPE,
    OID_PLUGIN_TYPE,
    ALLOC_PLUGIN_TYPE,
    JOURNAL_PLUGIN_TYPE,
    KEY_PLUGIN_TYPE
};

typedef enum reiserfs_plugin_type reiserfs_plugin_type_t;

enum reiserfs_item_type {
    STATDATA_ITEM,
    DIRENTRY_ITEM,
    INTERNAL_ITEM,
    ACL_ITEM,
    FILEBODY_ITEM
};

typedef enum reiserfs_item_type reiserfs_item_type_t;

enum reiserfs_dir_plugin_id {
    DIR_DIR40_ID		= 0x0
};

enum reiserfs_file_plugin_id {
    FILE_REG40_ID		= 0x0,
    FILE_SYMLINK40_ID		= 0x1,
    FILE_SPECIAL40_ID		= 0x2
};

enum reiserfs_item_plugin_id {
    ITEM_STATDATA40_ID		= 0x0,
    ITEM_SDE40_ID		= 0x1,
    ITEM_CDE40_ID		= 0x2,
    ITEM_INTERNAL40_ID		= 0x3,
    ITEM_ACL40_ID		= 0x4,
    ITEM_EXTENT40_ID		= 0x5,
    ITEM_DROP40_ID		= 0x6
};

enum reiserfs_node_plugin_id {
    NODE_REISER40_ID		= 0x0,
};

enum reiserfs_hash_plugin_id {
    HASH_RUPASOV_ID		= 0x0,
    HASH_R5_ID			= 0x1,
    HASH_TEA_ID			= 0x2,
    HASH_FNV1_ID		= 0x3,
    HASH_DEGENERATE_ID		= 0x4
};

typedef enum reiserfs_hash reiserfs_hash_t;

enum reiserfs_drop_policy_plugin_id {
    DROP_NEVER_ID		= 0x0,
    DROP_SUPPRESS_ID		= 0x1,
    DROP_FOURK_ID		= 0x2,
    DROP_ALWAYS_ID		= 0x3,
    DROP_SMART_ID		= 0x4
};

enum reiserfs_perm_plugin_id {
    PERM_RWX_ID			= 0x0
};

enum reiserfs_sdext_plugin_id {
    SDEXT_UNIX_ID		= 0x0,
    SDEXT_SYMLINK_ID		= 0x1,
    SDEXT_PLUGIN_ID		= 0x2,
    SDEXT_GEN_AND_FLAGS_ID	= 0x3,
    SDEXT_CAPABILITIES_ID	= 0x4,
    SDEXT_LARGE_TIMES_ID	= 0x5,
    SDEXT_LAST_ID
};

enum reiserfs_format_plugin_id {
    FORMAT_REISER40_ID		= 0x0,
    FORMAT_REISER36_ID		= 0x1
};

enum reiserfs_oid_plugin_id {
    OID_REISER40_ID		= 0x0,
    OID_REISER36_ID		= 0x1
};

enum reiserfs_alloc_plugin_id {
    ALLOC_REISER40_ID		= 0x0,
    ALLOC_REISER36_ID		= 0x1
};

enum reiserfs_journal_plugin_id {
    JOURNAL_REISER40_ID		= 0x0,
    JOURNAL_REISER36_ID		= 0x1
};

enum reiserfs_key_plugin_id {
    KEY_REISER40_ID		= 0x0,
    KEY_REISER36_ID		= 0x1
};

#define INVALID_PLUGIN_ID	(0xffff)

/* 
    Maximal possible key size. It is used for creating temporary keys by declaring 
    array of uint8_t elements REISERFS_KEY_SIZE long.
*/
#define REISERFS_KEY_SIZE 24

struct reiserfs_key {
    reiserfs_plugin_t *plugin;
    uint8_t body[REISERFS_KEY_SIZE];
};

typedef struct reiserfs_key reiserfs_key_t;

/*
    FIXME-VITALY: We should change these names to plugin independent style. Pass 
    these names to key plugin where they will be converted to plugin-specific names.
*/
typedef enum {
    /* File name key type */
    KEY40_FILENAME_MINOR = 0,
    /* Stat-data key type */
    KEY40_STATDATA_MINOR = 1,
    /* File attribute name */
    KEY40_ATTRNAME_MINOR = 2,
    /* File attribute value */
    KEY40_ATTRBODY_MINOR = 3,
    /* File body (drop or extent) */
    KEY40_BODY_MINOR	 = 4
} reiserfs_key40_minor_t;

/* 
    To create a new item or to insert into the item we need to perform the 
    following operations:
    
    (1) Create the description of the data being inserted.
    (2) Ask item plugin how much space is needed for the 
	data, described in 1.
    
    (3) Free needed space for data being inserted.
    (4) Ask item plugin to create an item (to paste into 
	the item) on the base of description from 1.

    For such purposes we have:
    
    (1) Fixed description structures for all item types (stat, 
	diritem, internal, etc).
    
    (2) Estimate common item method which gets coord of where 
	to insert into (NULL or unit == -1 for insertion, 
	otherwise it is pasting) and data description from 1.
    
    (3) Insert node methods prepare needed space and call 
	Create/Paste item methods if data description is specified.
    
    (4) Create/Paste item methods if data description has not 
	beed specified on 3. 
*/

struct reiserfs_internal_hint {    
    blk_t pointer;
};

typedef struct reiserfs_internal_hint reiserfs_internal_hint_t;

struct reiserfs_sdext_unix_hint {
    uint32_t uid;
    uint32_t gid;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t rdev;
    uint64_t bytes;
};

typedef struct reiserfs_sdext_unix_hint reiserfs_sdext_unix_hint_t;

/* These fields should be changed to what proper description of needed extentions */
struct reiserfs_stat_hint {
    uint16_t mode;
    uint16_t extmask;
    uint32_t nlink;
    uint64_t size;
    
    /* Stat data extention hints */
    struct {
	uint32_t count;
	void *hint[16];
    } ext;
};

typedef struct reiserfs_stat_hint reiserfs_stat_hint_t;

struct reiserfs_entry_hint {
    /* Locality and objectid of object pointed by entry */
    struct {
	uint64_t locality;
	uint64_t objectid;
    } objid;

    /* Offset of entry */
    struct {
	uint64_t objectid;
	uint64_t offset;
    } entryid;

    /* Name of entry */
    char *name;
};

typedef struct reiserfs_entry_hint reiserfs_entry_hint_t;

struct reiserfs_direntry_hint {
    uint16_t count;
    reiserfs_entry_hint_t *entry;
};

typedef struct reiserfs_direntry_hint reiserfs_direntry_hint_t;

struct reiserfs_object_hint {
    reiserfs_id_t statdata_pid;
    reiserfs_id_t direntry_pid;
    reiserfs_id_t drop_pid;
    reiserfs_id_t extent_pid;
    reiserfs_id_t hash_pid;
    uint64_t sdext;
};

typedef struct reiserfs_object_hint reiserfs_object_hint_t;

/* 
    Create item or paste into item on the base of this structure. Here "data" is 
    a pointer to data to be copied. 
*/ 
struct reiserfs_item_hint {
    reiserfs_item_type_t type;
    /*
	This is pointer to already formated item body. It is useful for item copying, 
	replacing, etc. This will be used by fsck probably.
    */
    void *data;

    /*
	This is pointer to hint which describes item. It is widely used for creating 
	an item.
    */
    void *hint;

    /* The key of item */
    reiserfs_key_t key;
    
    uint16_t len;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_item_hint reiserfs_item_hint_t;

struct reiserfs_pos {
    uint32_t item;
    uint32_t unit;
};

typedef struct reiserfs_pos reiserfs_pos_t;

#define REISERFS_PLUGIN_MAX_LABEL	16
#define REISERFS_PLUGIN_MAX_DESC	256

/* Common plugin header */
struct reiserfs_plugin_header {
    void *handle;
    reiserfs_id_t id;
    reiserfs_plugin_type_t type;
    const char label[REISERFS_PLUGIN_MAX_LABEL];
    const char desc[REISERFS_PLUGIN_MAX_DESC];
};

typedef struct reiserfs_plugin_header reiserfs_plugin_header_t;

struct reiserfs_key_ops {
    reiserfs_plugin_header_t h;

    /* Smart check of key structure */
    errno_t (*check) (const void *, int);
    
    /* Confirms key format */
    int (*confirm) (const void *);

    /* Returns minimal key for this key-format */
    const void *(*minimal) (void);
    
    /* Returns maximal key for this key-format */
    const void *(*maximal) (void);
    
    /* Get size of the key */
    uint8_t (*size) (void);

    /* Compares two keys by comparing its all components */
    int (*compare_full) (const void *, const void *);

    /* Compares two keys by comparing only objectid and locality */
    int (*compare_short) (const void *, const void *);

    /* 
	Cleans key up. Actually it just memsets it by zeros, but more smart behavior may 
	be implemented.
    */
    void (*clean) (const void *);

    /* Gets/sets key type (minor in reiser4 notation) */	
    void (*set_type) (const void *, uint32_t);
    uint32_t (*get_type) (const void *);

    /* Gets/sets key locality */
    void (*set_locality) (const void *, uint64_t);
    uint64_t (*get_locality) (const void *);
    
    /* Gets/sets key objectid */
    void (*set_objectid) (const void *, uint64_t);
    uint64_t (*get_objectid) (const void *);

    /* Gets/sets key offset */
    void (*set_offset) (const void *, uint64_t);
    uint64_t (*get_offset) (const void *);

    /* Gets/sets directory key hash */
    void (*set_hash) (const void *, uint64_t);
    uint64_t (*get_hash) (const void *);

    /* Gets/sets directory key generation counter */
    void (*set_counter) (const void *, uint8_t);
    uint8_t (*get_counter) (const void *);
    
    errno_t (*build_generic_full) (const void *, uint32_t, uint64_t, uint64_t, uint64_t);
    errno_t (*build_entry_full) (const void *, void *, uint64_t, uint64_t, const char *);
    
    errno_t (*build_generic_short) (const void *, uint32_t, uint64_t, uint64_t);
    errno_t (*build_entry_short) (const void *, void *, const char *);

    errno_t (*build_by_entry) (const void *, void *);
};

typedef struct reiserfs_key_ops reiserfs_key_ops_t;

struct reiserfs_file_ops {
    reiserfs_plugin_header_t h;

};

typedef struct reiserfs_file_ops reiserfs_file_ops_t;

struct reiserfs_dir_ops {
    reiserfs_plugin_header_t h;

    /* Creates new directory with passed parent and object keys */
    reiserfs_entity_t *(*create) (const void *, reiserfs_key_t *, 
	reiserfs_key_t *, reiserfs_object_hint_t *); 
    
    /* Opens directory with specified key */
    reiserfs_entity_t *(*open) (const void *, reiserfs_key_t *);
    
    /* Closes previously opened or created directory */
    void (*close) (reiserfs_entity_t *);

    /* 
	Resets internal position so that next read from the directory will return
	first entry.
    */
    errno_t (*rewind) (reiserfs_entity_t *);
   
    /* Reads next entry from the directory */
    errno_t (*read) (reiserfs_entity_t *, reiserfs_entry_hint_t *);
    
    /* Adds new entry into directory */
    errno_t (*add) (reiserfs_entity_t *, reiserfs_entry_hint_t *);

    /* Makes check of directory */
    errno_t (*check) (reiserfs_entity_t *, int);

    /* 
	Simple check for validness (for instance, is statdata exists for dir40 
	directory). 
    */
    int (*confirm) (reiserfs_entity_t *);

    /* Returns current position in directory */
    uint32_t (*tell) (reiserfs_entity_t *);

    /* Makes lookup inside dir */
    errno_t (*lookup) (reiserfs_entity_t *, reiserfs_entry_hint_t *);
};

typedef struct reiserfs_dir_ops reiserfs_dir_ops_t;

struct reiserfs_item_common_ops {
    
    /* Forms item structures based on passed hint in passed memory area */
    errno_t (*create) (const void *, reiserfs_item_hint_t *);

    /* Makes lookup for passed key */
    int (*lookup) (const void *, reiserfs_key_t *, uint32_t *);

    /* Confirms item type */
    int (*confirm) (const void *);

    /* Checks item for validness */
    errno_t (*check) (const void *, int);

    /* Prints item into specified buffer */
    errno_t (*print) (const void *, char *, uint32_t);

    /* Get the max key which could be stored in the item of this type */
    errno_t (*maxkey) (const void *);
    
    /* Returns unit count */
    uint32_t (*count) (const void *);

    /* Removes specified unit from the item */
    errno_t (*remove) (const void *, uint32_t);

    /* Inserts unit described by passed hint into the item */
    errno_t (*insert) (const void *, uint32_t, reiserfs_item_hint_t *);
    
    /* Estimatess item */
    errno_t (*estimate) (uint32_t, reiserfs_item_hint_t *);
    
    /* Retunrs min size the item may occupy */
    uint32_t (*minsize) (void);
};

typedef struct reiserfs_item_common_ops reiserfs_item_common_ops_t;

struct reiserfs_direntry_ops {
    errno_t (*get_entry) (const void *, uint32_t, reiserfs_entry_hint_t *);
};

typedef struct reiserfs_direntry_ops reiserfs_direntry_ops_t;

struct reiserfs_stat_ops {
    uint16_t (*get_mode) (const void *);
    void (*set_mode) (const void *, uint16_t);
};

typedef struct reiserfs_stat_ops reiserfs_stat_ops_t;

struct reiserfs_internal_ops {
    errno_t (*set_pointer) (const void *, blk_t);
    blk_t (*get_pointer) (const void *);
    int (*has_pointer) (const void *, blk_t);
};

typedef struct reiserfs_internal_ops reiserfs_internal_ops_t;

struct reiserfs_item_ops {
    reiserfs_plugin_header_t h;

    /* Methods common for all item types */
    reiserfs_item_common_ops_t common;

    /* Methods specific to particular type of item */
    union {
	reiserfs_direntry_ops_t direntry;
	reiserfs_stat_ops_t statdata;
	reiserfs_internal_ops_t internal;
    } specific;
};

typedef struct reiserfs_item_ops reiserfs_item_ops_t;

/* Unix stat data fileds extention plugin */
struct reiserfs_sdext_ops {
    reiserfs_plugin_header_t h;

    errno_t (*create) (void *, void *);
    errno_t (*open) (void *, void *);

    uint32_t (*length) (void);
    int (*confirm) (void *);
};

typedef struct reiserfs_sdext_ops reiserfs_sdext_ops_t;

/*
    Node plugin operates on passed block. It doesn't any initialization, so it 
    hasn't close method and all its methods accepts first argument aal_block_t, 
    not initialized previously hypothetic instance of node.
*/
struct reiserfs_node_ops {
    reiserfs_plugin_header_t h;

    /* 
	Forms empty node incorresponding to given level in specified block.
	Initializes instance of node and returns it to caller.
    */
    reiserfs_entity_t *(*create) (aal_block_t *, uint8_t);

    /* 
	Opens node (parses data in orser to check whether it is valid for this
	node type), initializes instance and returns it to caller.
     */
    reiserfs_entity_t *(*open) (aal_block_t *);

    /* 
	Finalizes work with node (compresses data back) and frees all memory.
	Returns the error code to caller.
    */
    errno_t (*close) (reiserfs_entity_t *);
    
    /* Confirms that given block contains valid node of requested format */
    int (*confirm) (reiserfs_entity_t *);

    /* 
	Make more smart node check and return result to caller. Thsi method is 
	used for fsck purposes.
    */
    errno_t (*check) (reiserfs_entity_t *, int);
    
    /* Prints node into given buffer */
    errno_t (*print) (reiserfs_entity_t *, char *, uint32_t);
    
    /* 
	Makes lookup inside node by specified key. Returns TRUE in the case exact
	match was found and FALSE otherwise.
    */
    int (*lookup) (reiserfs_entity_t *, reiserfs_key_t *, 
	reiserfs_pos_t *);
    
    /* Inserts item at specified pos */
    errno_t (*insert) (reiserfs_entity_t *, reiserfs_pos_t *, 
	reiserfs_item_hint_t *);
    
    /* Pastes units at specified pos */
    errno_t (*paste) (reiserfs_entity_t *, reiserfs_pos_t *, 
	reiserfs_item_hint_t *);
    
    /* Removes item at specified pos */
    errno_t (*remove) (reiserfs_entity_t *, reiserfs_pos_t *);
    
    /* Returns max item count */
    uint32_t (*maxnum) (reiserfs_entity_t *);

    /* Returns item count */
    uint32_t (*count) (reiserfs_entity_t *);
    
    /* Gets/sets node's free space */
    uint32_t (*get_space) (reiserfs_entity_t *);
    errno_t (*set_space) (reiserfs_entity_t *, uint32_t);
   
    /* Gets/sets node's plugin id */
    uint32_t (*get_pid) (reiserfs_entity_t *);
    errno_t (*set_pid) (reiserfs_entity_t *, uint32_t);
    
    /*
	This is optional method. That means that there could be node formats which 
	do not keep level.
    */
    uint8_t (*get_level) (reiserfs_entity_t *);
    errno_t (*set_level) (reiserfs_entity_t *, uint8_t);
    
    /* Gets/sets key at pos */
    errno_t (*get_key) (reiserfs_entity_t *, uint32_t, reiserfs_key_t *);
    errno_t (*set_key) (reiserfs_entity_t *, uint32_t, reiserfs_key_t *);

    /* Returns item's overhead */
    uint32_t (*item_overhead) (void);

    /* Returns item's length by pos */
    uint32_t (*item_len) (reiserfs_entity_t *, uint32_t);
    
    /* Returns item's max size */
    uint32_t (*item_maxsize) (reiserfs_entity_t *);
    
    /* Gets item at passed pos */
    void *(*item_body) (reiserfs_entity_t *, uint32_t);

    /* Gets/sets node's plugin ID */
    uint32_t (*item_get_pid) (reiserfs_entity_t *, uint32_t);
    errno_t (*item_set_pid) (reiserfs_entity_t *, uint32_t, uint32_t);
};

typedef struct reiserfs_node_ops reiserfs_node_ops_t;

struct reiserfs_hash_ops {
    reiserfs_plugin_header_t h;
    uint64_t (*build) (const unsigned char *, uint32_t);
};

typedef struct reiserfs_hash_ops reiserfs_hash_ops_t;

struct reiserfs_drop_ops {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_drop_ops reiserfs_drop_ops_t;

struct reiserfs_hook_ops {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_hook_ops reiserfs_hook_ops_t;

struct reiserfs_perm_ops {
    reiserfs_plugin_header_t h;
};

typedef struct reiserfs_perm_ops reiserfs_perm_ops_t;

/* Disk-format plugin */
struct reiserfs_format_ops {
    reiserfs_plugin_header_t h;
    
    /* 
	Called during filesystem opening (mounting).
	It reads format-specific super block and initializes
	plugins suitable for this format.
    */
    reiserfs_entity_t *(*open) (aal_device_t *);
    
    /* 
	Called during filesystem creating. It forms format-specific
	super block, initializes plugins and calls their create 
	method.
    */
    reiserfs_entity_t *(*create) (aal_device_t *, count_t, uint16_t);
    
    /*
	Called during filesystem syncing. It calls method sync
	for every "child" plugin (block allocator, journal, etc).
    */
    errno_t (*sync) (reiserfs_entity_t *);

    /*
	Checks format-specific super block for validness. Also checks
	whether filesystem objects lie in valid places. For example,
	format-specific super block for format40 must lie in 17-th
	block for 4096 byte long blocks.
    */
    errno_t (*check) (reiserfs_entity_t *, int);

    /*
	Probes whether filesystem on given device has this format.
	Returns "true" if so and "false" otherwise.
    */
    int (*confirm) (aal_device_t *device);

    /*
	Closes opened or created previously filesystem. Frees
	all assosiated memory.
    */
    void (*close) (reiserfs_entity_t *);
    
    /*
	Returns format string for this format. For example
	"reiserfs 4.0".
    */
    const char *(*format) (reiserfs_entity_t *);

    /* 
	Returns offset in blocks where format-specific super block 
	lies.
    */
    blk_t (*offset) (reiserfs_entity_t *);
    
    /* Gets/sets root block */
    blk_t (*get_root) (reiserfs_entity_t *);
    void (*set_root) (reiserfs_entity_t *, blk_t);
    
    /* Gets/sets block count */
    count_t (*get_blocks) (reiserfs_entity_t *);
    void (*set_blocks) (reiserfs_entity_t *, count_t);
    
    /* Gets/sets height field */
    uint16_t (*get_height) (reiserfs_entity_t *);
    void (*set_height) (reiserfs_entity_t *, uint16_t);
    
    /* Gets/sets free blocks number for this format */
    count_t (*get_free) (reiserfs_entity_t *);
    void (*set_free) (reiserfs_entity_t *, count_t);
    
    /* Returns children objects plugins */
    reiserfs_id_t (*journal_pid) (reiserfs_entity_t *);
    reiserfs_id_t (*alloc_pid) (reiserfs_entity_t *);
    reiserfs_id_t (*oid_pid) (reiserfs_entity_t *);

    /* Returns area where oid data lies */
    void (*oid)(reiserfs_entity_t *, void **, void **);
};

typedef struct reiserfs_format_ops reiserfs_format_ops_t;

struct reiserfs_oid_ops {
    reiserfs_plugin_header_t h;

    reiserfs_entity_t *(*open) (void *, void *);
    reiserfs_entity_t *(*create) (void *, void *);

    errno_t (*sync) (reiserfs_entity_t *);

    int (*confirm) (reiserfs_entity_t *);
    errno_t (*check) (reiserfs_entity_t *);

    void (*close) (reiserfs_entity_t *);
    
    uint64_t (*alloc) (reiserfs_entity_t *);
    void (*dealloc) (reiserfs_entity_t *, uint64_t);
    
    uint64_t (*next) (reiserfs_entity_t *);
    uint64_t (*used) (reiserfs_entity_t *);

    uint64_t (*root_parent_locality) (void);
    
    uint64_t (*root_locality) (void);
    uint64_t (*root_objectid) (void);
};

typedef struct reiserfs_oid_ops reiserfs_oid_ops_t;

struct reiserfs_alloc_ops {
    reiserfs_plugin_header_t h;
    
    reiserfs_entity_t *(*open) (aal_device_t *, count_t);
    reiserfs_entity_t *(*create) (aal_device_t *, count_t);
    void (*close) (reiserfs_entity_t *);
    errno_t (*sync) (reiserfs_entity_t *);

    void (*mark) (reiserfs_entity_t *, blk_t);
    int (*test) (reiserfs_entity_t *, blk_t);
    
    blk_t (*alloc) (reiserfs_entity_t *);
    void (*dealloc) (reiserfs_entity_t *, blk_t);

    count_t (*free) (reiserfs_entity_t *);
    count_t (*used) (reiserfs_entity_t *);

    int (*confirm) (reiserfs_entity_t *);
    errno_t (*check) (reiserfs_entity_t *, int);
};

typedef struct reiserfs_alloc_ops reiserfs_alloc_ops_t;

struct reiserfs_journal_ops {
    reiserfs_plugin_header_t h;
    
    reiserfs_entity_t *(*open) (aal_device_t *);
    reiserfs_entity_t *(*create) (aal_device_t *, void *);
    void (*close) (reiserfs_entity_t *);

    int (*confirm) (reiserfs_entity_t *);
    errno_t (*check) (reiserfs_entity_t *, int);
    
    errno_t (*sync) (reiserfs_entity_t *);
    errno_t (*replay) (reiserfs_entity_t *);
    
    void (*area) (reiserfs_entity_t *, blk_t *, blk_t *);
};

typedef struct reiserfs_journal_ops reiserfs_journal_ops_t;

union reiserfs_plugin {
    reiserfs_plugin_header_t h;
	
    reiserfs_file_ops_t file_ops;
    reiserfs_dir_ops_t dir_ops;
    reiserfs_item_ops_t item_ops;
    reiserfs_node_ops_t node_ops;
    reiserfs_hash_ops_t hash_ops;
    reiserfs_drop_ops_t drop_ops;
    reiserfs_hook_ops_t hook_ops;
    reiserfs_perm_ops_t perm_ops;
    reiserfs_format_ops_t format_ops;
    reiserfs_oid_ops_t oid_ops;
    reiserfs_alloc_ops_t alloc_ops;
    reiserfs_journal_ops_t journal_ops;
    reiserfs_key_ops_t key_ops;
    reiserfs_sdext_ops_t sdext_ops;
};

/* 
    Replica of coord for using in plugins. Field "cache" is void * because we 
    should keep libreiser4 structures unknown for plugins.
*/
struct reiserfs_place {
   void *cache;
   reiserfs_pos_t pos;
};

typedef struct reiserfs_place reiserfs_place_t;

/* 
    This structure is passed to all plugins in initialization time and used
    for access libreiser4 factories.
*/
struct reiserfs_core {
    
    struct {
	
	/* Finds plugin by its attribues (type and id) */
	reiserfs_plugin_t *(*plugin_find)(reiserfs_plugin_type_t, reiserfs_id_t);
	
    } factory_ops;
    
    struct {
	/*
	    Makes lookup in the tree in order to know where say stat data item of a
	    file realy lies. It is used in all object plugins.
	*/
	int (*lookup) (const void *, reiserfs_key_t *, reiserfs_place_t *);

	/* 
	    Inserts item/unit into the tree by calling reiserfs_tree_insert function,
	    used by all object plugins (dir, file, etc)
	*/
	errno_t (*item_insert)(const void *, reiserfs_item_hint_t *);
    
	/*
	    Removes item/unit from the tree. It is used in all object plugins for
	    modification purposes.
	*/
	errno_t (*item_remove)(const void *, reiserfs_key_t *);

	/*
	    Returns pointer on the item and its size by passed coord from the tree.
	    It is used all object plugins in order to access data stored in items.
	*/
	errno_t (*item_body) (const void *, reiserfs_place_t *, void **, uint32_t *);

	/* Returns key by specified coord */
	errno_t (*item_key) (const void *, reiserfs_place_t *, reiserfs_key_t *);
    
	errno_t (*item_right) (const void *, reiserfs_place_t *);
	errno_t (*item_left) (const void *, reiserfs_place_t *);

	/* Returs plugin id by coord */
	reiserfs_id_t (*item_pid) (const void *, reiserfs_place_t *, 
	    reiserfs_plugin_type_t type);
	
    } tree_ops;
};

typedef struct reiserfs_core reiserfs_core_t;

typedef reiserfs_plugin_t *(*reiserfs_plugin_entry_t) (reiserfs_core_t *);
typedef errno_t (*reiserfs_plugin_func_t) (reiserfs_plugin_t *, void *);

/* Plugin functions and macros */
#ifndef ENABLE_COMPACT

#define libreiser4_plugin_call(action, ops, method, args...)	    \
    ({								    \
	if (!ops.method) {					    \
	    aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,	    \
		"Method \"" #method "\" isn't implemented in %s.",  \
		#ops);						    \
	    action;						    \
	}							    \
	ops.method(args);					    \
    })

#else

#define libreiser4_plugin_call(action, ops, method, args...)	    \
    ({ops.method(args);})					    \
    
#endif

#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)

extern reiserfs_plugin_t *libreiser4_plugin_load_name(const char *name);

#endif

extern reiserfs_plugin_t *libreiser4_plugin_load_entry(reiserfs_plugin_entry_t entry);

extern void libreiser4_plugin_unload(reiserfs_plugin_t *plugin);

/* Factory functions */
extern errno_t libreiser4_factory_init(void);
extern void libreiser4_factory_done(void);

#if defined(ENABLE_COMPACT) || defined(ENABLE_MONOLITHIC)
    
#define libreiser4_factory_register(entry)			    \
    static reiserfs_plugin_entry_t __plugin_entry		    \
	__attribute__((__section__(".plugins"))) = entry
#else

#define libreiser4_factory_register(entry)			    \
    reiserfs_plugin_entry_t __plugin_entry = entry

#endif

#define libreiser4_factory_failed(action, oper, type, id)	    \
    do {							    \
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,	    \
	    "Can't " #oper " " #type " plugin by its id %x.", id);  \
	action;							    \
    } while (0)

extern errno_t libreiser4_factory_foreach(reiserfs_plugin_func_t func, 
    void *data);

extern reiserfs_plugin_t *libreiser4_factory_suitable(reiserfs_plugin_func_t func,
    void *data);

extern reiserfs_plugin_t *libreiser4_factory_find_by_id(reiserfs_plugin_type_t type,
    reiserfs_id_t id);

extern reiserfs_plugin_t *libreiser4_factory_find_by_name(reiserfs_plugin_type_t type,
    const char *name);

#endif

