/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/* first read balance.c comments before reading this */

/* An item_plugin implements all of the operations required for
   balancing that are item specific. */

/* an item plugin also implements other operations that are specific to that
 * item.  These go into the item specific operations portion of the item
 * handler, and all of the item specific portions of the item handler are put
 * into a union. */

typedef enum { 
	STAT_DATA_ITEM_TYPE,
	DIR_ENTRY_ITEM_TYPE,
	INTERNAL_ITEM_TYPE,
	ORDINARY_FILE_METADATA_TYPE,
	OTHER_ITEM_TYPE /* not used */
} item_type_id;

/* this is the part of each item plugin that all items are expected to
 * support or at least explicitly fail to support by setting the
 * pointer to null. */
typedef struct {
	/* in reiser4 the key doesn't contain the full item type only several bits of
	   it. So after doing coord_by_key() we need to check that we really
	   found what we have looked for, rather than some different item that
	   has the same minor packing locality. Use "item_plugin_id" to determine
	   what kind of item you have found.  
	*/
	item_type_id item_type;

	/** operations called by balancing 

	It is interesting to consider that some of these item
	operations could be given sources or targets that are not
	really items in nodes.  This could be ok/useful.

	*/
	/** 
	 * maximal key that can _possibly_ be occupied by this item
	 *
	 *  When node ->lookup() method (called by
	 *  coord_by_key()) reaches an item after binary search
	 *  ->max_key_inside() item plugin's method is used to determine
	 *  whether new item should pasted into existing item 
	 *   (new_key<=max_key_inside()) or new item has to be created
	 *  (new_key>max_key_inside()).
	 *
	 *  For items that occupy exactly one key (like stat-data)
	 *  this method should return this key. For items that can
	 *  grow indefinitely (extent, directory item) this should
	 *  return max_key().
	 *  
	 */
	reiser4_key *( *max_key_inside )( const coord_t *coord, 
					  reiser4_key *area );
	/**
	 * true if item @coord can merge data at @key.
	 */
	int ( *can_contain_key )( const coord_t *coord, 
				  const reiser4_key *key,
				  const reiser4_item_data *data );
	/**
	 * mergeable() - check items for mergeability
	 *
	 * Optional method. Returns true if two items can be merged.
	 *
	 */
	int ( *mergeable )( const coord_t *p1, 
			    const coord_t *p2 );
	
	/* used for debugging only, prints an ascii description of the
	   item contents */
	void ( *print )( const char *, coord_t *coord ); 
	/* used for debugging, every item should have here the most
	   complete possible check of the consistency of the item that
	   the inventor can construct */
	int ( *check )( coord_t *coord, const char **error );
	
	/* number of atomic things in an item */
	unsigned ( *nr_units )( const coord_t *coord );
	
	/* search within item for a unit within the item, and return a
	   pointer to it.  This can be used to calculate how many
	   bytes to shrink an item if you use pointer arithmetic and
	   compare to the start of the item body if the item's data
	   are continuous in the node, if the item's data are not
	   continuous in the node, all sorts of other things are maybe
	   going to break as well. */
	lookup_result ( *lookup )( const reiser4_key *key, 
				   lookup_bias bias, 
				   coord_t *coord );
	/** method called by ode_plugin->create_item() to initialise new
	 * item */
	int ( *init )( coord_t *coord, reiser4_item_data *data );
	/** method called (e.g., by resize_item()) to place new data into
	    item when it grows*/
	int ( *paste )( coord_t *coord, reiser4_item_data *data,
			carry_plugin_info *info );
	/**
	 * return true if paste into @coord is allowed to skip
	 * carry. That is, if such paste would require any changes
	 * at the parent level
	 */
	int ( *fast_paste )( const coord_t *coord );
	/**
	 * how many but not more than @want units of @source can be
	 * shifted into @target node. If pend == append - we try to
	 * append last item of @target by first units of @source. If
	 * pend == prepend - we try to "prepend" first item in @target
	 * by last units of @source. @target node has @free_space
	 * bytes of free space. Total size of those units are returned
	 * via @size.
	 *
	 * @target is not NULL if shifting to the mergeable item and
	 * NULL is new item will be created during shifting.
	 */
	int ( *can_shift )(unsigned free_space, coord_t * source,
			   znode * target, shift_direction pend,
			   unsigned * size, unsigned want);
	
	/* starting off @from-th unit of item @source append or
	   prepend @count units to @target. @target has been already
	   expanded by @free_space bytes. That must be exactly what is
	   needed for those items in @target. If @where_is_free_space
	   == append - free space is at the end of @target item,
	   othersize - it is in the beginning of it. */
	void ( *copy_units )( coord_t *target, coord_t *source,
			      unsigned from, unsigned count,
			      shift_direction where_is_free_space,
			      unsigned free_space);
	
	int  ( *create_hook )( const coord_t *item, void *arg );
	/* do whatever is necessary to do when @count units starting
	 * from @from-th one are removed from the tree */
	/*
	 * FIXME-VS: this is used to be here for, in particular,
	 * extents and items of internal type to free blocks they point
	 * to at the same time with removing items from a
	 * tree. Problems start, however, when dealloc_block fails due
	 * to some reason. Item gets removed, but blocks it pointed to
	 * are not freed. It is not clear how to fix this for items of
	 * internal type because a need to remove internal item may
	 * appear in the middle of balancing, and there is no way to
	 * undo changes made. OTOH, if space allocator involves
	 * balancing to perform dealloc_block - this will probably
	 * break balancing due to deadlock issues
	 */
	int ( *kill_hook )( const coord_t *item, 
			    unsigned from, unsigned count, void *kill_params );
	int ( *shift_hook )( const coord_t *item, 
			     unsigned from, unsigned count, 
			     znode *old_node );

	/*
	 * unit @*from contains @from_key. unit @*to contains
	 * @to_key. Cut all keys between @from_key and @to_key
	 * including boundaries. Set @from and @to to number of units
	 * which were removed. When units are cut from item beginning -
	 * move space which gets freed to head of item. When units are
	 * cut from item end - move freed space to item end. When units
	 * are cut from the middle of item - move freed space to item
	 * head. Return amount of space which got freed. Save smallest
	 * removed key is @smallest_removed is not 0
	 */
	int ( *cut_units )( coord_t *, unsigned *from, unsigned *to,
			    const reiser4_key *from_key,
			    const reiser4_key *to_key,
			    reiser4_key *smallest_removed );
	
	/*
	 * like cut_units, except that these units are removed from the
	 * tree, not only from a node
	 */
	int ( *kill_units )( coord_t *, unsigned *from, unsigned *to,
			     const reiser4_key *from_key,
			     const reiser4_key *to_key,
			     reiser4_key *smallest_removed );

	/* if @key_of_coord == 1 - returned key of coord, otherwise -
	   key of unit is returned. If @coord is not set to certain
	   unit - ERR_PTR(-ENOENT) is returned */
	reiser4_key * ( *unit_key )( const coord_t *coord, 
				     reiser4_key *key );
	/**
	 * estimate how much space is needed for paste @data into item at
	 * @coord. if @coord==0 - estimate insertion, otherwise - estimate
	 * pasting
	 */
	int ( *estimate )( const coord_t *coord, 
			   const reiser4_item_data *data );
	
	/* converts flow @f to item data. @coord == 0 on insert */
	int ( *item_data_by_flow )( const coord_t *coord,
				    const flow_t *f,
				    reiser4_item_data *data );

	/* return the right or left child of @coord, only if it is in memory */
	int ( *utmost_child )( const coord_t *coord, sideof side,
			       jnode **child );
	/* return whether the right or left child of @coord is dirty. */
	int ( *utmost_child_dirty )( const coord_t *coord, sideof side, int *is_dirty );
	
	/* return whether the right or left child of @coord has a non-fake block number. */
	int ( *utmost_child_real_block )( const coord_t *coord, sideof side,
					  reiser4_block_nr *block );

	reiser4_key *( *real_max_key_inside )( const coord_t *coord, reiser4_key * );

	/* return true if item contains key in it, coord is adjusted
	 * correspondingly */
	int ( *key_in_item )( coord_t *coord, const reiser4_key *key );

	/* return true if unit to which coord is set contains @key */
	int ( *key_in_unit )( const coord_t *coord, const reiser4_key *key );
} common_item_plugin;


/* operations specific to the directory item */
typedef struct {
	/**
	 * extract stat-data key from directory entry at @coord and place it
	 * into @key.
	 */
	int ( *extract_key )( const coord_t *coord, reiser4_key *key );
	/**
	 * update object key in item.
	 */
	int ( *update_key )( const coord_t *coord, 
			     const reiser4_key *key, lock_handle *lh );
	/**
	 * extract name from directory entry at @coord and return it
	 */
	char *( *extract_name )( const coord_t *coord );
	/**
	 * extract file type (DT_* stuff) from directory entry at @coord and
	 * return it
	 */
	unsigned ( *extract_file_type )( const coord_t *coord );
	int ( *add_entry )( const struct inode *dir,
			    coord_t *coord, lock_handle *lh,
			    const struct dentry *name, reiser4_dir_entry_desc *entry );
	int ( *rem_entry )( const struct inode *dir,
			    coord_t *coord, lock_handle *lh,
			    reiser4_dir_entry_desc *entry );
	int ( *max_name_len )( int block_size );
} dir_entry_ops;


/* operations specific to items regular file metadata are built of */
typedef struct {
	/* @page is used in extent's write. If it is set (when tail2extent
	 * conversion is in progress) - do not grab a page and do not copy data
	 * from flow into it because all the data are already */
	int (* write) (struct inode *, coord_t *, lock_handle *, flow_t *,
		       struct page *);
	int (* read) (struct inode *, coord_t *,
		      lock_handle *, flow_t *);
	int (* readpage) (void *, struct page *);
	int (* writepage) (coord_t *, lock_handle * lh, struct page *);
	int (* page_cache_readahead) (struct file *, coord_t *, lock_handle *,
				      unsigned long start, unsigned long count);
} file_ops;


/* operations specific to items of stat data type */
typedef struct {
	int ( *init_inode )( struct inode *inode, char *sd, int len );
	int ( *save_len ) ( struct inode *inode );
	int ( *save ) ( struct inode *inode, char **area );
} sd_ops;


/* operations specific to internal item */
typedef struct {
	/** all tree traversal want to know from internal item is where
	    to go next. */
	void ( *down_link )( const coord_t *coord, 
			     const reiser4_key *key, 
			     reiser4_block_nr *block );
	/** check that given internal item contains given pointer. */
	int ( *has_pointer_to )( const coord_t *coord, 
				 const reiser4_block_nr *block );
} internal_item_ops;

struct item_plugin {
	/** generic fields */
	plugin_header h;
	
	/* methods common for all item types */
	common_item_plugin common;
	/* methods specific to particular type of item */
	union {
		dir_entry_ops     dir;
		file_ops	  file;
		sd_ops            sd;
		internal_item_ops internal;
	} s;

};

static inline item_id item_id_by_plugin (item_plugin *plugin)
{
	return plugin->h.id;
}


int item_can_contain_key( const coord_t *item, const reiser4_key *key,
			  const reiser4_item_data * );
int are_items_mergeable( const coord_t *i1, const coord_t *i2 );
int item_is_internal(const coord_t * );
int item_is_extent(const coord_t * );
int item_is_statdata (const coord_t *item);


/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
