/*
    plugin.h -- reiser4 plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#ifndef PLUGIN_H
#define PLUGIN_H

#include <aal/aal.h>

#define REISER4_DEFAULT_BLOCKSIZE	(4096)
#define REISER4_MASTER_OFFSET		(65536)
#define REISER4_MASTER_MAGIC		("R4Sb")

typedef uint64_t roid_t;
typedef uint16_t rpid_t;

typedef void reiser4_body_t;

enum reiser4_plugin_type {
    FILE_PLUGIN_TYPE,
    DIR_PLUGIN_TYPE,
    ITEM_PLUGIN_TYPE,
    NODE_PLUGIN_TYPE,
    HASH_PLUGIN_TYPE,
    TAIL_PLUGIN_TYPE,
    PERM_PLUGIN_TYPE,
    SDEXT_PLUGIN_TYPE,
    FORMAT_PLUGIN_TYPE,
    OID_PLUGIN_TYPE,
    ALLOC_PLUGIN_TYPE,
    JNODE_PLUGIN_TYPE,
    JOURNAL_PLUGIN_TYPE,
    KEY_PLUGIN_TYPE
};

typedef enum reiser4_plugin_type reiser4_plugin_type_t;

enum reiser4_dir_plugin_id {
    DIR_DIR40_ID		= 0x0
};

enum reiser4_file_plugin_id {
    FILE_REGULAR40_ID		= 0x0,
    FILE_SYMLINK40_ID		= 0x1,
    FILE_SPECIAL40_ID		= 0x2
};

enum reiser4_item_plugin_id {
    ITEM_STATDATA40_ID		= 0x0,
    ITEM_SDE40_ID		= 0x1,
    ITEM_CDE40_ID		= 0x2,
    ITEM_INTERNAL40_ID		= 0x3,
    ITEM_ACL40_ID		= 0x4,
    ITEM_EXTENT40_ID		= 0x5,
    ITEM_TAIL40_ID		= 0x6
};

enum reiser4_item_group {
    STATDATA_ITEM_GROUP		= 0x0,
    INTERNAL_ITEM_GROUP		= 0x1,
    DIRENTRY_ITEM_GROUP		= 0x2,
    FILEBODY_ITEM_GROUP		= 0x3,
    PERMISSN_ITEM_GROUP		= 0x4
};

typedef enum reiser4_item_group reiser4_item_group_t;

enum reiser4_node_plugin_id {
    NODE_REISER40_ID		= 0x0,
};

enum reiser4_hash_plugin_id {
    HASH_RUPASOV_ID		= 0x0,
    HASH_R5_ID			= 0x1,
    HASH_TEA_ID			= 0x2,
    HASH_FNV1_ID		= 0x3,
    HASH_DEGENERATE_ID		= 0x4
};

typedef enum reiser4_hash reiser4_hash_t;

enum reiser4_tail_plugin_id {
    TAIL_NEVER_ID		= 0x0,
    TAIL_SUPPRESS_ID		= 0x1,
    TAIL_FOURK_ID		= 0x2,
    TAIL_ALWAYS_ID		= 0x3,
    TAIL_SMART_ID		= 0x4,
    TAIL_LAST_ID		= 0x5
};

enum reiser4_perm_plugin_id {
    PERM_RWX_ID			= 0x0
};

enum reiser4_sdext_plugin_id {
    SDEXT_UNIX_ID		= 0x0,
    SDEXT_SYMLINK_ID		= 0x1,
    SDEXT_PLUGIN_ID		= 0x2,
    SDEXT_GEN_AND_FLAGS_ID	= 0x3,
    SDEXT_CAPABILITIES_ID	= 0x4,
    SDEXT_LARGE_TIMES_ID	= 0x5
};

enum reiser4_format_plugin_id {
    FORMAT_REISER40_ID		= 0x0,
    FORMAT_REISER36_ID		= 0x1
};

enum reiser4_oid_plugin_id {
    OID_REISER40_ID		= 0x0,
    OID_REISER36_ID		= 0x1
};

enum reiser4_alloc_plugin_id {
    ALLOC_REISER40_ID		= 0x0,
    ALLOC_REISER36_ID		= 0x1
};

enum reiser4_journal_plugin_id {
    JOURNAL_REISER40_ID		= 0x0,
    JOURNAL_REISER36_ID		= 0x1
};

enum reiser4_key_plugin_id {
    KEY_REISER40_ID		= 0x0,
    KEY_REISER36_ID		= 0x1
};

typedef union reiser4_plugin reiser4_plugin_t;

struct reiser4_entity {
    reiser4_plugin_t *plugin;
};

typedef struct reiser4_entity reiser4_entity_t;

#define INVALID_PLUGIN_ID	(0xffff)

/* Types for layout defining */
typedef errno_t (*reiser4_action_func_t) (aal_device_t *, 
    blk_t, void *);

typedef errno_t (*reiser4_layout_func_t) (reiser4_entity_t *, 
    reiser4_action_func_t, void *);

/* 
    Maximal possible key size. It is used for creating temporary keys by declaring 
    array of uint8_t elements reiser4_KEY_SIZE long.
*/
#define REISER4_KEY_SIZE 24

struct reiser4_key {
    reiser4_plugin_t *plugin;
    uint8_t body[REISER4_KEY_SIZE];
};

typedef struct reiser4_key reiser4_key_t;

#define KEY_FILENAME_TYPE   0
#define KEY_STATDATA_TYPE   1
#define KEY_ATTRNAME_TYPE   2
#define KEY_ATTRBODY_TYPE   3
#define KEY_BODY_TYPE	    4
#define KEY_LAST_TYPE	    5

typedef uint32_t reiser4_key_type_t;

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

struct reiser4_internal_hint {    
    blk_t ptr;
};

typedef struct reiser4_internal_hint reiser4_internal_hint_t;

struct reiser4_sdext_unix_hint {
    uint32_t uid;
    uint32_t gid;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t rdev;
    uint64_t bytes;
};

typedef struct reiser4_sdext_unix_hint reiser4_sdext_unix_hint_t;

/* These fields should be changed to what proper description of needed extentions */
struct reiser4_statdata_hint {
    uint16_t mode;
    uint32_t nlink;
    uint64_t size;
    uint64_t extmask;
    
    /* Stat data extention hints */
    struct {
	uint8_t count;
	void *hint[64];
    } extentions;
};

typedef struct reiser4_statdata_hint reiser4_statdata_hint_t;

struct reiser4_entry_hint {

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

typedef struct reiser4_entry_hint reiser4_entry_hint_t;

struct reiser4_direntry_hint {
    uint16_t count;
    reiser4_entry_hint_t *entry;
};

typedef struct reiser4_direntry_hint reiser4_direntry_hint_t;

struct reiser4_object_hint {
    rpid_t statdata_pid;
    rpid_t direntry_pid;
    rpid_t tail_pid;
    rpid_t extent_pid;
    rpid_t hash_pid;
    uint64_t sdext;
};

typedef struct reiser4_object_hint reiser4_object_hint_t;

/* 
    Create item or paste into item on the base of this structure. Here "data" is 
    a pointer to data to be copied. 
*/ 
struct reiser4_item_hint {
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
    reiser4_key_t key;

    /* Length of the item to inserted */
    uint16_t len;
    
    /* Plugin to be used for creating item */
    reiser4_plugin_t *plugin;
};

typedef struct reiser4_item_hint reiser4_item_hint_t;

struct reiser4_pos {
    uint32_t item;
    uint32_t unit;
};

typedef struct reiser4_pos reiser4_pos_t;

#define REISER4_PLUGIN_MAX_LABEL	16
#define REISER4_PLUGIN_MAX_DESC		256

/* Common plugin header */
struct reiser4_plugin_header {
    void *handle;
    rpid_t id;
    reiser4_plugin_type_t type;
    const char label[REISER4_PLUGIN_MAX_LABEL];
    const char desc[REISER4_PLUGIN_MAX_DESC];
};

typedef struct reiser4_plugin_header reiser4_plugin_header_t;

struct reiser4_key_ops {
    reiser4_plugin_header_t h;

    /* Smart check of key structure */
    errno_t (*valid) (reiser4_body_t *);
    
    /* Confirms key format */
    int (*confirm) (reiser4_body_t *);

    /* Returns minimal key for this key-format */
    reiser4_body_t *(*minimal) (void);
    
    /* Returns maximal key for this key-format */
    reiser4_body_t *(*maximal) (void);
    
    /* Get size of the key */
    uint8_t (*size) (void);

    /* Compares two keys by comparing its all components */
    int (*compare) (reiser4_body_t *, reiser4_body_t *);

    /* 
	Cleans key up. Actually it just memsets it by zeros, but more smart behavior 
	may be implemented.
    */
    void (*clean) (reiser4_body_t *);

    errno_t (*build_generic) (reiser4_body_t *, 
	reiser4_key_type_t, uint64_t, uint64_t, uint64_t);
    
    errno_t (*build_direntry) (reiser4_body_t *, 
	reiser4_plugin_t *, uint64_t, uint64_t, 
	const char *);
    
    errno_t (*build_objid) (reiser4_body_t *, 
	reiser4_key_type_t, uint64_t, uint64_t);
    
    errno_t (*build_entryid) (reiser4_body_t *, 
	reiser4_plugin_t *, const char *);

    /* Builds full key by short entry key */
    errno_t (*build_by_entry) (reiser4_body_t *, 
	reiser4_body_t *);

    /* Gets/sets key type (minor in reiser4 notation) */	
    void (*set_type) (reiser4_body_t *, reiser4_key_type_t);
    reiser4_key_type_t (*get_type) (reiser4_body_t *);

    /* Gets/sets key locality */
    void (*set_locality) (reiser4_body_t *, uint64_t);
    uint64_t (*get_locality) (reiser4_body_t *);
    
    /* Gets/sets key objectid */
    void (*set_objectid) (reiser4_body_t *, uint64_t);
    uint64_t (*get_objectid) (reiser4_body_t *);

    /* Gets/sets key offset */
    void (*set_offset) (reiser4_body_t *, uint64_t);
    uint64_t (*get_offset) (reiser4_body_t *);

    /* Gets/sets directory key hash */
    void (*set_hash) (reiser4_body_t *, uint64_t);
    uint64_t (*get_hash) (reiser4_body_t *);

    /* Prints key into specified buffer */
    errno_t (*print) (reiser4_body_t *, char *, 
	uint32_t, uint16_t);
};

typedef struct reiser4_key_ops reiser4_key_ops_t;

struct reiser4_file_ops {
    reiser4_plugin_header_t h;
};

typedef struct reiser4_file_ops reiser4_file_ops_t;

struct reiser4_dir_ops {
    reiser4_plugin_header_t h;

    /* Creates new directory with passed parent and object keys */
    reiser4_entity_t *(*create) (const void *, reiser4_key_t *, 
	reiser4_key_t *, reiser4_object_hint_t *); 
    
    /* Opens directory with specified key */
    reiser4_entity_t *(*open) (const void *, reiser4_key_t *);
    
    /* Closes previously opened or created directory */
    void (*close) (reiser4_entity_t *);

    /* 
	Resets internal position so that next read from the directory will return
	first entry.
    */
    errno_t (*rewind) (reiser4_entity_t *);
   
    /* Reads next entry from the directory */
    errno_t (*read) (reiser4_entity_t *, reiser4_entry_hint_t *);
    
    /* Adds new entry into directory */
    errno_t (*add) (reiser4_entity_t *, reiser4_entry_hint_t *);

    /* Removes entry from directory */
    errno_t (*remove) (reiser4_entity_t *, uint32_t);
    
    /* Makes simple check of directory */
    errno_t (*valid) (reiser4_entity_t *);

    /* Returns current position in directory */
    uint32_t (*tell) (reiser4_entity_t *);

    /* Makes lookup inside dir */
    errno_t (*lookup) (reiser4_entity_t *, reiser4_entry_hint_t *);
};

typedef struct reiser4_dir_ops reiser4_dir_ops_t;

struct reiser4_item_common_ops {
    
    /* Forms item structures based on passed hint in passed memory area */
    errno_t (*init) (reiser4_body_t *, reiser4_item_hint_t *);

    /* Inserts unit described by passed hint into the item */
    errno_t (*insert) (reiser4_body_t *, uint32_t, reiser4_item_hint_t *);
    
    /* Removes specified unit from the item. Returns released space */
    uint32_t (*remove) (reiser4_body_t *, uint32_t);

    /* Estimates item */
    errno_t (*estimate) (uint32_t, reiser4_item_hint_t *);
    
    /* Confirms item type */
    int (*confirm) (reiser4_body_t *);

    /* Makes lookup for passed key */
    int (*lookup) (reiser4_body_t *, reiser4_key_t *, uint32_t *);

    /* Checks item for validness */
    errno_t (*valid) (reiser4_body_t *);

    /* Prints item into specified buffer */
    errno_t (*print) (reiser4_body_t *, char *, uint32_t, uint16_t);

    /* Get the max key which could be stored in the item of this type */
    errno_t (*maxkey) (reiser4_key_t *);
    
    /* Returns unit count */
    uint32_t (*count) (reiser4_body_t *);
};

typedef struct reiser4_item_common_ops reiser4_item_common_ops_t;

struct reiser4_direntry_ops {
    errno_t (*entry) (reiser4_body_t *, uint32_t, 
	reiser4_entry_hint_t *);
};

typedef struct reiser4_direntry_ops reiser4_direntry_ops_t;

struct reiser4_statdata_ops {
    uint16_t (*get_mode) (reiser4_body_t *);
    errno_t (*set_mode) (reiser4_body_t *, uint16_t);
};

typedef struct reiser4_statdata_ops reiser4_statdata_ops_t;

struct reiser4_internal_ops {
    blk_t (*get_ptr) (reiser4_body_t *);
    errno_t (*set_ptr) (reiser4_body_t *, blk_t);
};

typedef struct reiser4_internal_ops reiser4_internal_ops_t;

struct reiser4_item_ops {
    reiser4_plugin_header_t h;

    /* Item group (stat data, internal, file body, etc) */
    reiser4_item_group_t group;

    /* Methods common for all item types */
    reiser4_item_common_ops_t common;

    /* Methods specific to particular type of item */
    union {
	reiser4_statdata_ops_t statdata;
	reiser4_internal_ops_t internal;
	reiser4_direntry_ops_t direntry;
	
	/* Here shall be filebody item (extent, tail) */

    } specific;
};

typedef struct reiser4_item_ops reiser4_item_ops_t;

/* Unix stat data fileds extention plugin */
struct reiser4_sdext_ops {
    reiser4_plugin_header_t h;

    errno_t (*init) (reiser4_body_t *, 
	reiser4_sdext_unix_hint_t *);
    
    errno_t (*open) (reiser4_body_t *, 
	reiser4_sdext_unix_hint_t *);
    
    int (*confirm) (reiser4_body_t *);
    
    uint32_t (*length) (void);
};

typedef struct reiser4_sdext_ops reiser4_sdext_ops_t;

/*
    Node plugin operates on passed block. It doesn't any initialization, so it 
    hasn't close method and all its methods accepts first argument aal_block_t, 
    not initialized previously hypothetic instance of node.
*/
struct reiser4_node_ops {
    reiser4_plugin_header_t h;

    /* 
	Forms empty node incorresponding to given level in specified block.
	Initializes instance of node and returns it to caller.
    */
    reiser4_entity_t *(*create) (aal_block_t *, uint8_t);

    /* 
	Opens node (parses data in orser to check whether it is valid for this
	node type), initializes instance and returns it to caller.
     */
    reiser4_entity_t *(*open) (aal_block_t *);

    /* 
	Finalizes work with node (compresses data back) and frees all memory.
	Returns the error code to caller.
    */
    errno_t (*close) (reiser4_entity_t *);
    
    /* Confirms that given block contains valid node of requested format */
    int (*confirm) (aal_block_t *);

    errno_t (*valid) (reiser4_entity_t *);
    
    /* Prints node into given buffer */
    errno_t (*print) (reiser4_entity_t *, char *, uint32_t, uint16_t);
    
    /* Returns item count */
    uint16_t (*count) (reiser4_entity_t *);
    
    /* Returns item's overhead */
    uint16_t (*overhead) (reiser4_entity_t *);

    /* Returns item's max size */
    uint16_t (*maxspace) (reiser4_entity_t *);
    
    /* Returns free space in the node */
    uint16_t (*space) (reiser4_entity_t *);

    /* Gets node's plugin id */
    uint16_t (*pid) (reiser4_entity_t *);
    
    /* 
	Makes lookup inside node by specified key. Returns TRUE in the case exact
	match was found and FALSE otherwise.
    */
    int (*lookup) (reiser4_entity_t *, reiser4_key_t *, 
	reiser4_pos_t *);
    
    /* Inserts item at specified pos */
    errno_t (*insert) (reiser4_entity_t *, reiser4_pos_t *, 
	reiser4_item_hint_t *);
    
    /* Removes item at specified pos */
    errno_t (*remove) (reiser4_entity_t *, reiser4_pos_t *);
    
    /* Pastes units at specified pos */
    errno_t (*paste) (reiser4_entity_t *, reiser4_pos_t *, 
	reiser4_item_hint_t *);
    
    /* Removes unit at specified pos */
    errno_t (*cut) (reiser4_entity_t *, reiser4_pos_t *);
    
    /* Gets/sets key at pos */
    errno_t (*get_key) (reiser4_entity_t *, reiser4_pos_t *, reiser4_key_t *);
    errno_t (*set_key) (reiser4_entity_t *, reiser4_pos_t *, reiser4_key_t *);

    /* Gets item at passed pos */
    reiser4_body_t *(*item_body) (reiser4_entity_t *, reiser4_pos_t *);

    /* Returns item's length by pos */
    uint16_t (*item_len) (reiser4_entity_t *, reiser4_pos_t *);
    
    /* Gets/sets node's plugin ID */
    uint16_t (*item_pid) (reiser4_entity_t *, reiser4_pos_t *);
};

typedef struct reiser4_node_ops reiser4_node_ops_t;

struct reiser4_hash_ops {
    reiser4_plugin_header_t h;
    uint64_t (*build) (const unsigned char *, uint32_t);
};

typedef struct reiser4_hash_ops reiser4_hash_ops_t;

struct reiser4_tail_ops {
    reiser4_plugin_header_t h;
};

typedef struct reiser4_tail_ops reiser4_tail_ops_t;

struct reiser4_hook_ops {
    reiser4_plugin_header_t h;
};

typedef struct reiser4_hook_ops reiser4_hook_ops_t;

struct reiser4_perm_ops {
    reiser4_plugin_header_t h;
};

typedef struct reiser4_perm_ops reiser4_perm_ops_t;

/* Disk-format plugin */
struct reiser4_format_ops {
    reiser4_plugin_header_t h;
    
    /* 
	Called during filesystem opening (mounting).
	It reads format-specific super block and initializes
	plugins suitable for this format.
    */
    reiser4_entity_t *(*open) (aal_device_t *);
    
    /* 
	Called during filesystem creating. It forms format-specific
	super block, initializes plugins and calls their create 
	method.
    */
    reiser4_entity_t *(*create) (aal_device_t *, count_t, uint16_t);
    
    /*
	Called during filesystem syncing. It calls method sync
	for every "child" plugin (block allocator, journal, etc).
    */
    errno_t (*sync) (reiser4_entity_t *);

    /*
	Checks format-specific super block for validness. Also checks
	whether filesystem objects lie in valid places. For example,
	format-specific super block for format40 must lie in 17-th
	block for 4096 byte long blocks.
    */
    errno_t (*valid) (reiser4_entity_t *);
    
    /*	Checks thoroughly the format structure and fixes what needed. */
    errno_t (*check) (reiser4_entity_t *, uint16_t);

    /* Prints all useful information about the format */
    errno_t (*print) (reiser4_entity_t *, char *, uint32_t, uint16_t);
    
    /*
	Probes whether filesystem on given device has this format.
	Returns "true" if so and "false" otherwise.
    */
    int (*confirm) (aal_device_t *device);

    /*
	Closes opened or created previously filesystem. Frees
	all assosiated memory.
    */
    void (*close) (reiser4_entity_t *);
    
    /*
	Returns format string for this format. For example
	"reiserfs 4.0".
    */
    const char *(*name) (reiser4_entity_t *);

    /* Gets/sets root block */
    blk_t (*get_root) (reiser4_entity_t *);
    void (*set_root) (reiser4_entity_t *, blk_t);
    
    /* Gets/sets block count */
    count_t (*get_len) (reiser4_entity_t *);
    void (*set_len) (reiser4_entity_t *, count_t);
    
    /* Gets/sets height field */
    uint16_t (*get_height) (reiser4_entity_t *);
    void (*set_height) (reiser4_entity_t *, uint16_t);
    
    /* Gets/sets free blocks number for this format */
    count_t (*get_free) (reiser4_entity_t *);
    void (*set_free) (reiser4_entity_t *, count_t);
    
    /* Returns children objects plugins */
    rpid_t (*journal_pid) (reiser4_entity_t *);
    rpid_t (*alloc_pid) (reiser4_entity_t *);
    rpid_t (*oid_pid) (reiser4_entity_t *);

    /* Returns area where oid data lies */
    void (*oid_area)(reiser4_entity_t *, void **, uint32_t *);

    /* The set of methods for going through format blocks */
    reiser4_layout_func_t skipped_layout;
    reiser4_layout_func_t format_layout;
    reiser4_layout_func_t alloc_layout;
    reiser4_layout_func_t journal_layout;
};

typedef struct reiser4_format_ops reiser4_format_ops_t;

struct reiser4_oid_ops {
    reiser4_plugin_header_t h;

    /* Opens oid allocator on passed area */
    reiser4_entity_t *(*open) (const void *, uint32_t);

    /* Creates oid allocator on passed area */
    reiser4_entity_t *(*create) (const void *, uint32_t);

    /* Closes passed instance of oid allocator */
    void (*close) (reiser4_entity_t *);
    
    /* Synchronizes oid allocator */
    errno_t (*sync) (reiser4_entity_t *);

    /* Makes check for validness */
    errno_t (*valid) (reiser4_entity_t *);
    
    /* Gets next object id */
    uint64_t (*alloc) (reiser4_entity_t *);

    /* Releases passed object id */
    void (*dealloc) (reiser4_entity_t *, uint64_t);
    
    /* Returns the number of used object ids */
    uint64_t (*used) (reiser4_entity_t *);
    
    /* Returns the number of free object ids */
    uint64_t (*free) (reiser4_entity_t *);

    /* Object ids of root and root parenr object */
    uint64_t (*root_parent_locality) (void);
    uint64_t (*root_locality) (void);
    uint64_t (*root_objectid) (void);
};

typedef struct reiser4_oid_ops reiser4_oid_ops_t;

struct reiser4_alloc_ops {
    reiser4_plugin_header_t h;
    
    /* Opens block allocator */
    reiser4_entity_t *(*open) (reiser4_entity_t *, 
	count_t);

    /* Creates block allocator */
    reiser4_entity_t *(*create) (reiser4_entity_t *, 
	count_t);
    
    /* Closes blcok allocator */
    void (*close) (reiser4_entity_t *);

    /* Synchronizes block allocator */
    errno_t (*sync) (reiser4_entity_t *);

    /* Marks passed block as used */
    void (*mark) (reiser4_entity_t *, blk_t);

    /* Checks if passed block used */
    int (*test) (reiser4_entity_t *, blk_t);
    
    /* Allocates one block */
    blk_t (*alloc) (reiser4_entity_t *);

    /* Deallocates passed block */
    void (*dealloc) (reiser4_entity_t *, blk_t);

    /* Returns number of unused blocks */
    count_t (*free) (reiser4_entity_t *);

    /* Returns number of used blocks */
    count_t (*used) (reiser4_entity_t *);

    /* Checks blocks allocator on validness */
    errno_t (*valid) (reiser4_entity_t *);
};

typedef struct reiser4_alloc_ops reiser4_alloc_ops_t;

struct reiser4_journal_ops {
    reiser4_plugin_header_t h;
    
    /* Opens journal on specified device */
    reiser4_entity_t *(*open) (reiser4_entity_t *);

    /* Creates journal on specified device */
    reiser4_entity_t *(*create) (reiser4_entity_t *, void *);

    /* Frees journal instance */
    void (*close) (reiser4_entity_t *);

    /* Checks journal metadata on validness */
    errno_t (*valid) (reiser4_entity_t *);
    
    /* Synchronizes journal */
    errno_t (*sync) (reiser4_entity_t *);

    /* Replays journal */
    errno_t (*replay) (reiser4_entity_t *);
};

typedef struct reiser4_journal_ops reiser4_journal_ops_t;

union reiser4_plugin {
    reiser4_plugin_header_t h;
	
    reiser4_file_ops_t file_ops;
    reiser4_dir_ops_t dir_ops;
    reiser4_item_ops_t item_ops;
    reiser4_node_ops_t node_ops;
    reiser4_hash_ops_t hash_ops;
    reiser4_tail_ops_t tail_ops;
    reiser4_hook_ops_t hook_ops;
    reiser4_perm_ops_t perm_ops;
    reiser4_format_ops_t format_ops;
    reiser4_oid_ops_t oid_ops;
    reiser4_alloc_ops_t alloc_ops;
    reiser4_journal_ops_t journal_ops;
    reiser4_key_ops_t key_ops;
    reiser4_sdext_ops_t sdext_ops;
};

/* 
    Replica of coord for using in plugins. Field "cache" is void * because we 
    should keep libreiser4 structures unknown for plugins.
*/
struct reiser4_place {
   void *cache;
   reiser4_pos_t pos;
};

typedef struct reiser4_place reiser4_place_t;

/* The common node header */
struct reiser4_node_header {
    uint16_t pid;
    uint16_t num_items;
};

typedef struct reiser4_node_header reiser4_node_header_t;

/* 
    This structure is passed to all plugins in initialization time and used
    for access libreiser4 factories.
*/
struct reiser4_core {
    
    struct {
	
	/* Finds plugin by its attribues (type and id) */
	reiser4_plugin_t *(*plugin_ifind)(rpid_t, rpid_t);
	
	/* Finds plugin by its type and name */
	reiser4_plugin_t *(*plugin_nfind)(rpid_t, const char *);
	
    } factory_ops;
    
    struct {
	/*
	    Makes lookup in the tree in order to know where say stat data item of a
	    file realy lies. It is used in all object plugins.
	*/
	int (*lookup) (const void *, reiser4_key_t *, reiser4_place_t *);

	/* 
	    Inserts item/unit into the tree by calling reiser4_tree_insert function,
	    used by all object plugins (dir, file, etc)
	*/
	errno_t (*item_insert)(const void *, reiser4_item_hint_t *);
    
	/*
	    Removes item/unit from the tree. It is used in all object plugins for
	    modification purposes.
	*/
	errno_t (*item_remove)(const void *, reiser4_key_t *);

	/*
	    Returns pointer on the item and its size by passed coord from the tree.
	    It is used all object plugins in order to access data stored in items.
	*/
	errno_t (*item_body) (const void *, reiser4_place_t *, void **, uint32_t *);

	/* Returns key by specified coord */
	errno_t (*item_key) (const void *, reiser4_place_t *, reiser4_key_t *);
    
	errno_t (*item_right) (const void *, reiser4_place_t *);
	errno_t (*item_left) (const void *, reiser4_place_t *);

	/* Returs plugin id by coord */
	rpid_t (*item_pid) (const void *, reiser4_place_t *, 
	    reiser4_plugin_type_t type);
	
    } tree_ops;
};

typedef struct reiser4_core reiser4_core_t;

/* Plugin functions and macros */
#ifndef ENABLE_COMPACT

#define plugin_call(action, ops, method, args...)		    \
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

#define plugin_call(action, ops, method, args...)		    \
    ({ops.method(args);})					    \
    
#endif

typedef reiser4_plugin_t *(*reiser4_plugin_entry_t) (reiser4_core_t *);
typedef errno_t (*reiser4_plugin_func_t) (reiser4_plugin_t *, void *);

#if defined(ENABLE_COMPACT) || defined(ENABLE_MONOLITHIC)
    
#define plugin_register(entry)					    \
    static reiser4_plugin_entry_t __plugin_entry		    \
	__attribute__((__section__(".plugins"))) = entry
#else

#define plugin_register(entry)					    \
    reiser4_plugin_entry_t __plugin_entry = entry

#endif

#endif

