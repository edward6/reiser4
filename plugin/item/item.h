/* first read balance.c comments before reading this */

/* An item_plugin implements all of the operations required for
   balancing that are item specific. */

typedef enum { 
	DIR_ENTRY_ITEM_TYPE, STAT_DATA_ITEM_TYPE,
	EXTENT_ITEM_TYPE, 
	BODY_ITEM_TYPE, /* not yet */
	INTERNAL_ITEM_TYPE,
	OTHER_ITEM_TYPE
} item_type;

#define item_type_is_internal( coord ) \
( item_type_by_coord( coord ) == INTERNAL_ITEM_TYPE )

typedef struct item_plugin {
	/* in reiser4 key doesn't contain full item type only several bits of
	   it. So after doing coord_by_key() we need to check that we really
	   found what we have looked for, rather than some different item that
	   has the same minor packing locality. Use "item_type" to determine
	   what kind of item you have found.
	   
	   Item type distinguishes items not logically, but functionaly: all
	   item plugins share the same common set of balancing operations
	   (described below), but items of each generic type have different
	   set of operations to interact with object plugins. For example,
	   Items of STAT_DATA_ITEM_TYPE can load themselves from disk to inode
	   and store themselves back, which makes no sense for directory items
	   etc. Look at the .o union at the bottom of this declaration. */
	item_type item_type;
	/** operations called by balancing 

	It is interesting to consider that some of these item
	operations could be given sources or targets that are not
	really items in nodes.  This could be ok/useful.

*/
	struct balance_ops {
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
		reiser4_key *( *max_key_inside )( const tree_coord *coord, 
						  reiser4_key *area );
		/**
		 * true if item @coord can merge data at @key.
		 */
		int ( *can_contain_key )( const tree_coord *coord, 
					  const reiser4_key *key,
					  const reiser4_item_data *data );
		/**
		 * mergeable() - check items for mergeability
		 *
		 * Optional method. Returns true if two items can be merged.
		 *
		 */
		int ( *mergeable )( const tree_coord *p1, 
				    const tree_coord *p2 );

		/* used for debugging only, prints an ascii description of the
		   item contents */
		void ( *print )( const char *, tree_coord *coord ); 
		/* used for debugging, every item should have here the most
		   complete possible check of the consistency of the item that
		   the inventor can construct */
		int ( *check )( tree_coord *coord, const char **error );

		/* number of atomic things in an item */
		unsigned ( *nr_units )( const tree_coord *coord );

		/* search within item for a unit within the item, and return a
		   pointer to it.  This can be used to calculate how many
		   bytes to shrink an item if you use pointer arithmetic and
		   compare to the start of the item body if the item's data
		   are continuous in the node, if the item's data are not
		   continuous in the node, all sorts of other things are maybe
		   going to break as well. */
		lookup_result ( *lookup )( const reiser4_key *key, 
					   lookup_bias bias, 
					   tree_coord *coord );
		/** method called by ->create_item() to initialise new item */
		int ( *init )( tree_coord *coord );
		/** method called (e.g., by resize_item()) to place new data into
		    item when it grows*/
		int ( *paste )( tree_coord *coord, reiser4_item_data *data,
				carry_level *todo );
		/**
		 * return true if paste into @coord is allowed to skip
		 * carry. That is, if such paste would require any changes
		 * at the parent level
		 */
		int ( *fast_paste )( const tree_coord *coord );
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
		int ( *can_shift )(unsigned free_space, tree_coord * source,
				   znode * target, shift_direction pend,
				   unsigned * size, unsigned want);

		/* starting off @from-th unit of item @source append or
		   prepend @count units to @target. @target has been already
		   expanded by @free_space bytes. That must be exactly what is
		   needed for those items in @target. If @where_is_free_space
		   == append - free space is at the end of @target item,
		   othersize - it is in the beginning of it. */
		void ( *copy_units )( tree_coord *target, tree_coord *source,
				      unsigned from, unsigned count,
				      shift_direction where_is_free_space,
				      unsigned free_space);

		int  ( *create_hook )( const tree_coord *item, void *arg );
		/* do whatever is necessary to do when @count units starting
		   from @from-th one are removed from the tree */
		int ( *kill_hook )( const tree_coord *item, 
				    unsigned from, unsigned count );
		int ( *shift_hook )( const tree_coord *item, 
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
		int ( *cut_units )( tree_coord *, unsigned *from, unsigned *to,
				    const reiser4_key *from_key,
				    const reiser4_key *to_key,
				    reiser4_key *smallest_removed );

		/*
		 * like cut_units, except that these units are removed from the
		 * tree, not only from a node
		 */
		int ( *kill_units )( tree_coord *, unsigned *from, unsigned *to,
				     const reiser4_key *from_key,
				     const reiser4_key *to_key,
				     reiser4_key *smallest_removed );

		/* if @key_of_coord == 1 - returned key of coord, otherwise -
		   key of unit is returned. If @coord is not set to certain
		   unit - ERR_PTR(-ENOENT) is returned */
		reiser4_key * ( *unit_key )( const tree_coord *coord, 
					     reiser4_key *key );
		/**
		 * estimate how much space is needed for paste @data into item
		 * at @coord.
		 */
		int ( *estimate )( const tree_coord *coord, 
				   const reiser4_item_data *data );
		
		/* converts flow @f to item data. @coord == 0 on insert */
		int ( *item_data_by_flow )( const tree_coord *coord,
					    const flow_t *f,
					    reiser4_item_data *data );
	} b;
	
	/* item methods are separated into two groups:
            . methods common for all item types, used by balancing code.
              They are declared in .b struct above this point.
            . methods specific to particular type of item (as indicated by
	      ->item_type field) and used by object plugin (exception:
	      specific operations of internal items are used by balancing
	      code also). 
	   Explicit structs within this union should go inside .h files 
	   with declaration of respective items when they will appear. 
	   (Hans proposal). */
	union {
		dir_entry_ops     dir;
		file_ops	  file;
		sd_ops            sd;
		internal_item_ops internal;
	} s;			/* item specific item operations */

	/**
	 * return rightmost or leftmost child of this item with refcount incremented.
	 * If there is none, or child is not in a memory return NULL.
	 *
	 * This method is optional. It is used by slum gathering code.
	 *
	 * Maybe this method should go in balance_ops (.b)?
	 */
	jnode * ( *utmost_child )( const tree_coord *coord, sideof side );
} item_plugin;


typedef enum {
	SD_ITEM_ID, 
	EXTENT_ITEM_ID, 
	BODY_ITEM_ID,          /* not yet */
	SIMPLE_DIR_ITEM_ID,
	CMPND_DIR_ITEM_ID,
	ACL_ITEM_ID,           /* not yet */
	INTERNAL_ITEM_ID,
	LAST_ITEM_ID
} reiser4_item_id;

int item_can_contain_key( const tree_coord *item, const reiser4_key *key,
			  const reiser4_item_data * );
int are_items_mergeable( const tree_coord *i1, const tree_coord *i2 );

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
