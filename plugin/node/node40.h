/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */


typedef d16 node_offset_40;

/*
    flushstamp is made of mk_id and write_counter. mk_id is an id generated 
    randomly at mkreiserfs time. So we can just skip all nodes with different 
    mk_id. write_counter is d64 incrementing counter of writes on disk. It is 
    used for choosing the newest data at fsck time.
 */

typedef struct flush_stamp {
    d32 mk_fs_id;
    d64 flush_time;
} flush_stamp_t;

/** format of node header for 40 node layouts. Keep bloat out of this struct.  */
typedef struct node_header_40 {
	/** 
	 * identifier of node plugin. Must be located at the very beginning
	 * of a node.
	 */
	common_node_header common_header;
	/** free space in node measured in bytes */
	/* it might make some of the code simpler to store this just
	   before the last item header, but then free_space finding
	   code would be more complex.... A thought.... */
	node_offset_40    free_space; /**/
	/** offset to start of free space in node */
	node_offset_40    free_space_start;
	/** 1 is leaf level, 2 is twig level, root is the numerically largest
	 * level */
	d8	       level;
	/** magic field we need to tell formatted nodes */
  	d32	       magic;
	/** number of items --- is it too large? Probably.  */
	d16           num_items;
	/** node flags to be used by fsck (reiser4ck or reiser4fsck?)
	    and repacker */
/* commented out because it was uncommented */
/* 	char           flags; */
	/** for reiser4_fsck.  When information about what is a free
	    block is corrupted, and we try to recover everything even
	    if marked as freed, then old versions of data may
	    duplicate newer versions, and this field allows us to
	    restore the newer version.  Also useful for when users
	    delete the wrong files and send us desperate emails
	    offering $25 for them back.  */
	flush_stamp_t flush;
} node_header_40;

/* item headers are not standard across all node layouts, pass
 * pos_in_node to functions instead */
typedef struct item_header_40 {
	/** key of item */
/* this will get compressed to a few bytes on average in 4.1, so don't get too excited about how it doesn't hurt much to
 * add more bytes to item headers.  Probably you'll want your code to work for the 4.1 format also.... -Hans */
	/*  0 */ reiser4_key  key;
	/** offset from start of a node measured in 8-byte chunks */
	/* 24 */ node_offset_40  offset;
	/* 26 */ d16             length;
	/* 28 */ d16             plugin_id;
} item_header_40;

size_t             node40_item_overhead    ( const znode *node, flow_t * aflow);
size_t             node40_free_space       ( znode *node );
node_search_result node40_lookup           ( znode *node, 
					     const reiser4_key *key, 
					     lookup_bias bias, 
					     new_coord *coord );
int                node40_num_of_items     ( const znode *node );
char              *node40_item_by_coord    ( const new_coord *coord );
int                node40_length_by_coord  ( const new_coord *coord );
item_plugin       *node40_plugin_by_coord  ( const new_coord *coord );
reiser4_key       *node40_key_at           ( const new_coord *coord, 
					     reiser4_key *key );
size_t             node40_estimate         ( znode *node );
int                node40_check            ( const znode *node, __u32 flags,
					     const char **error );
int                node40_parse            ( const znode *node );
void               node40_print            ( const znode *node, __u32 flags );
int                node40_init             ( znode *node );
int                node40_guess            ( const znode *node );
void               node40_change_item_size ( new_coord * coord, int by );
int                node40_create_item      ( new_coord * target, 
					     const reiser4_key * key,
					     reiser4_item_data *data, 
					     carry_level * todo );
void               node40_update_item_key  ( new_coord * target, 
					     reiser4_key * key, 
					     carry_level * todo);
int                node40_cut_and_kill     ( new_coord * from, 
					     new_coord * to, 
					     const reiser4_key * from_key,
					     const reiser4_key * to_key,
					     reiser4_key * smallest_removed,
					     carry_level * todo, 
					     void *kill_params, __u32 flags);
int                node40_cut              ( new_coord * from, 
					     new_coord * to,
					     const reiser4_key * from_key,
					     const reiser4_key * to_key,
					     reiser4_key * smallest_removed,
					     carry_level * todo, __u32 flags);
int                node40_shift            ( new_coord * from, 
					     znode * to, 
					     shift_direction pend,
					     /* 
					      * if @from->node becomes
					      * empty - it will be deleted from
					      * the tree if this is set to 1 
					      */
					     int delete_child, 
					     int including_stop_coord,
					     carry_level *todo );

int                node40_fast_insert      ( const new_coord *coord );
int                node40_fast_paste       ( const new_coord *coord );
int                node40_fast_cut         ( const new_coord *coord );
int                node40_max_item_size    ( void );

void update_znode_dkeys (znode * left, znode * right);

/* 
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
