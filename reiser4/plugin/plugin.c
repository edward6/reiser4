/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */


// VOVA TEST

/*
 * Basic plugin infrastructure, lookup etc.
 */

/* PLUGINS: 
 *
 * Plugins are internal Reiser4 "modules" or "objects" used to increase
 * extensibility and allow external users to easily adapt reiser4 to
 * their needs.
 *
 * Plugins are classified into several disjoint "types". Plugins
 * belonging to the particular plugin type are termed "instances" of
 * this type. Currently the following types are present:
 *
 *  . object plugin
 *  . hash plugin
 *  . tail plugin
 *  . perm plugin
 *  . item plugin
 *  . node layout plugin
 *
 * Object (file) plugin determines how given file-system object serves
 * standard VFS requests for read, write, seek, mmap etc. Instances of
 * file plugins are: regular file, directory, symlink. Another example
 * of file plugin is audit plugin, that optionally records accesses to
 * underlying object and forward request to it.
 *
 * Hash plugins compute hashes used by reiser4 to store and locate
 * files within directories. Instances of hash plugin type are: r5, 
 * tea, rupasov.
 * 
 * Tail plugins (or, more precisely, tail policy plugins) determine
 * when last part of the file should be stored in a direct item.
 * 
 * Perm plugins control permissions granted for process accessing file.
 *
 * Scope and lookup:
 *
 * label such that pair ( type_label, plugin_label ) is unique.  This
 * pair is a globally persistent and user-visible plugin
 * identifier. Internally kernel maintains plugins and plugin types in
 * arrays using an index into those arrays as plugin and plugin type
 * identifiers. File-system in turn, also maintains persistent
 * "dictionary" which is mapping from plugin label to numerical
 * identifier which is stored in file-system objects.  That is, we
 * store the offset into the plugin array for that plugin type as the
 * plugin id in the stat data of the filesystem object.  

 * plugin_labels have meaning for the user interface that assigns
 * plugins to files, and may someday have meaning for dynamic loading of
 * plugins and for copying of plugins from one fs instance to
 * another by utilities like cp and tar.
 *
 * Internal kernel plugin type identifier (index in plugins[] array) is
 * of type reiser4_plugin_type. Set of available plugin types is
 * currently static, but dynamic loading doesn't seem to pose
 * insurmountable problems.
 *
 * Within each type plugins are addressed by the identifiers of type
 * reiser4_plugin_id (indices in
 * reiser4_plugin_type_data.builtin[]). Such identifiers are only
 * required to be unique within one type, not globally.
 *
 * Thus, plugin in memory is uniquely identified by the pair (type_id,
 * id). Each plugin is either builtin, or dynamic. Builtin plugins are
 * ones, required to provide standard file-system semantics and are
 * hard-coded into kernel image, or reiser4 module. Dynamic plugins, on
 * the other hand, are loaded as modules on demand.
 *
 * NOTE: dynamic plugin loading will be deferred until some future version
 * or until we have enough time to implement it efficiently.
 *
 * Usage:
 *
 * There exists only one instance of each plugin instance, but this
 * single instance can be associated with many entities (file-system
 * objects, items, nodes, transactions, file-descriptors etc.). Entity
 * to which plugin of given type is termed (due to the lack of
 * imagination) "subject" of this plugin type and, by abuse of
 * terminology, subject of particular instance of this type to which
 * it's attached currently. For example, inode is subject of object
 * plugin type. Inode representing directory is subject of directory
 * plugin, hash plugin type and some particular instance of hash plugin
 * type. Inode, representing regular file is subject of "regular file"
 * plugin, tail-policy plugin type etc.
 *
 * With each subject plugin possibly stores some state. For example,
 * state of directory plugin (instance of object plugin type) is pointer
 * to hash plugin (if directories always use hashing that is). State of
 * audit plugin is file descriptor (struct file) of log file or some
 * magic value to do logging through printk().
 * 
 * Interface:
 *
 * In addition to a scalar identifier, each plugin type and plugin
 * proper has a "label": short string and a "description"---longer
 * descriptive string. Labels and descriptions of plugin types are
 * hard-coded into plugins[] array, declared and defined in
 * plugin.c. Label and description of plugin are stored in .label and
 * .desc fields of reiser4_plugin_header respectively. It's possible to
 * locate plugin by the pair of labels. This is used to implement "plug"
 * mount option and ioctl(REISER4_IOC_SETPLG).  If plugin with given
 * pair of labels is not found, code tries to load certain module. Name
 * of this module is determined by request_plugin() function. For
 * example, for hash plugin with label "thash", module name would be
 * "reiserplug-hash-thash". After module requesting, lookup by labels is
 * repeated, so that if module registers itself through
 * reiser4_register_plugin() it will be found.
 *
 * NOTE: dynamic plugin loading will be deferred until some future version
 * or until we have enough time to implement it efficiently.
 *
 * Features:
 *
 *  . user-level plugin manipulations:
 *    + reiser4("filename/..file_plugin<='audit'");
 *    + write(open("filename/..file_plugin"), "audit", 8);
 *
 *  . user level utilities lsplug and chplug to manipulate plugins.
 *    Utilities are not of primary priority. Possibly they will be not
 *    working on v4.0
 *
 *  . mount option "plug" to set-up plugins of root-directory.
 *    "plug=foo:bar" will set "bar" as default plugin of type "foo".
 *
 * Limitations: 
 *
 *  . each plugin type has to provide at least one builtin
 *    plugin. This is technical limitation and it can be lifted in the
 *    future.
 *
 * TODO:
 *
 * New plugin types/plugings:
 * 
 *  . perm:acl
 * 
 *  d audi---audit plugin intercepting and possibly logging all 
 *    accesses to object. Requires to put stub functions in file_operations
 *    in stead of generic_file_*. 
 * 
 *  . over---handle hash overflows
 *
 *  . sqnt---handle different access patterns and instruments read-ahead
 *
 *  . hier---handle inheritance of plugins along file-system hierarchy
 *
 * Different kinds of inheritance: on creation vs. on access.
 * Compatible/incompatible plugins.
 * Inheritance for multi-linked files.
 * Layered plugins.
 * Notion of plugin context is abandoned. Each file is associated
 * with one plugin and dependant plugins (hash, etc.) are stored as
 * main plugin state. Now, if we have plugins used for regular files
 * but not for directories, how such plugins would be inherited?
 *  . always store them with directories also
 *  . use inheritance hierarchy, independent of file-system namespace
 *
 */

#include "../reiser4.h"

/* plugin type representation. Nobody outside of this file
   should care about this, so define it right here. */
typedef struct reiser4_plugin_type_data {
	/** internal plugin type identifier. Should coincide with
	    index of this item in plugins[] array. */
	reiser4_plugin_type   type_id;
	/** fallback function. Used to determine default instance of
	    this type for given subject. For example, fallback
	    function for object (file) plugin, determines plugin instance to use
	    by looking into i_mode of inode. */
	reiser4_plugin    *( *fallback )( void *subj );
	/** short symbolic label of this plugin type. Should be no longer
	    than MAX_PLUGIN_TYPE_LABEL_LEN characters including '\0'. */
	const char           *label;
	/** plugin type description longer than .label */
	const char           *desc;
	/** number of built-in plugin instances of this type */
	int                   builtin_num;
	/** array of built-in plugins */
	reiser4_plugin       *builtin;
	plugin_list_head      plugins_list;
} reiser4_plugin_type_data;

/* public interface */

/** initialise plugin sub-system. Just call this once on reiser4 startup. */
int init_plugins( void );
int reiser4_handle_default_plugin_option( char *option, reiser4_plugin **area );
int reiser4_setup_plugins( struct super_block *super, reiser4_plugin **area );
reiser4_plugin *lookup_plugin( char *type_label, char *plug_label );
reiser4_plugin *lookup_plugin_name( char *plug_label );
int locate_plugin( struct inode *inode, plugin_locator *loc );

/* internal functions. */

static int is_type_id_valid( reiser4_plugin_type type_id );
static int is_plugin_id_valid( reiser4_plugin_type type_id, 
			       reiser4_plugin_id id );
static reiser4_plugin_type find_type( const char *label );
static reiser4_plugin *find_plugin( reiser4_plugin_type_data *ptype, 
				    const char *label );
static reiser4_plugin_type_data plugins[ REISER4_PLUGIN_TYPES ];
static reiser4_plugin_id max_id = 0;

/** initialise plugin sub-system. Just call this once on reiser4 startup. */
int init_plugins( void )
{
	reiser4_plugin_type type_id;

	dinfo( "Builtin plugins:\n" );
	for( type_id = 0 ; type_id < REISER4_PLUGIN_TYPES ; ++ type_id ) {
		reiser4_plugin_type_data *ptype;
		int i;

		ptype = &plugins[ type_id ];
		plugin_list_init( &ptype -> plugins_list );
		dinfo( "Of type %s (%s):\n", ptype -> label, ptype -> desc );
		for( i = 0 ; i < ptype -> builtin_num ; ++ i ) {
			reiser4_plugin *plugin;

			plugin = &ptype -> builtin[ i ];
			if( plugin -> h.rec_len == 0 )
				continue;
				
			assert( "nikita-537", plugin -> h.type_id == type_id );
			plugin -> h.id = i;
			print_plugin( "\t", plugin ); 
			if( plugin -> h.id > max_id ) {
				max_id = plugin -> h.id;
			}
			plugin_list_clean( plugin );
			plugin_list_push_back( &ptype -> plugins_list, plugin );
		}
	}
	return 0;
}

/** parse mount time option and update root-directory plugin
    appropriately. */
int reiser4_handle_default_plugin_option( char *option, /* Option should
							   has form
							   "type:label",
							   where "type"
							   is label of
							   plugin type
							   and "label"
							   is label of
							   plugin
							   instance
							   within this
							   type. */
					  reiser4_plugin **area /* where
								 * result
								 * is to
								 * be
								 * stored */ )
{
	char *type_label;
	char *plug_label;
	reiser4_plugin *plugin;

	assert( "nikita-538", option != NULL );
	assert( "nikita-539", area != NULL );
	
	type_label = option;
	plug_label = strchr( option, ':' );
	if( plug_label == NULL ) {
		info( "Use 'plug=type:label'\n" );
		return -EINVAL;
	}
		
	*plug_label = '\0';
	++ plug_label;

	plugin = lookup_plugin( type_label, plug_label );
	if( plugin == NULL ) {
		info( "Unknown plugin: %s:%s\n", type_label, plug_label );
		return -EINVAL;
	}
	if( area[ plugin -> h.type_id ] != NULL ) {
		info( "Plugin already set\n" );
		print_plugin( "existing", area[ plugin -> h.type_id ] );
		print_plugin( "new", plugin );
		return -EINVAL;
	}
	area[ plugin -> h.type_id ] = plugin;
	return 0;
}

/** install plugins on root directory. */
int reiser4_setup_plugins( struct super_block *super, reiser4_plugin **area )
{
	reiser4_plugin_type  type_id;
	struct inode        *root_dir;
	int                  result;


	assert( "nikita-540", super != NULL );
	assert( "nikita-541", is_reiser4_super( super ) );
	assert( "nikita-542", area != NULL );
	assert( "nikita-543", super -> s_root != NULL );

	root_dir = super -> s_root -> d_inode;
	assert( "nikita-544", is_reiser4_inode( root_dir ) );
	
	result = 0;
	for( type_id = 0 ; type_id < REISER4_PLUGIN_TYPES ; ++ type_id ) {
		reiser4_plugin *plugin;

		plugin = area[ type_id ];
		if( plugin == NULL )
			plugin = plugins[ type_id ].fallback( root_dir );
		assert( "nikita-545", plugin != NULL );
		if( plugin -> h.pops -> change != NULL ) {
			result = plugin -> h.pops -> change( root_dir, plugin );
			if( result == 0 )
				print_plugin( "installed", plugin );
			else {
				print_plugin( "failed to install", plugin );
				break;
			}
		}
	}
	return result;
}




/** lookup plugin name by scanning tables */
reiser4_plugin *lookup_plugin_name( char *plug_label )
{
	reiser4_plugin_type type_id;
	reiser4_plugin     *plugin;

	assert( "vova-001", plug_label != NULL );

	plugin = NULL;

	dinfo( "lookup_plugin_name: %s\n", plug_label );

	for( type_id = 0 ; type_id < REISER4_PLUGIN_TYPES ; ++ type_id ) {
		plugin = find_plugin( &plugins[ type_id ], plug_label );
		if( plugin != NULL )
			break;
	}
	return plugin;
}


/** lookup plugin by scanning tables */
reiser4_plugin *lookup_plugin( char *type_label, char *plug_label )
{
	reiser4_plugin     *result;
	reiser4_plugin_type type_id;
		
	assert( "nikita-546", type_label != NULL );
	assert( "nikita-547", plug_label != NULL );

	result = NULL;
	type_id = find_type( type_label );
	if( is_type_id_valid( type_id ) ) {
		result = find_plugin( &plugins[ type_id ], plug_label );
		if( result == NULL )
			info( "Unknown plugin: %s\n", plug_label );
	} else
		info( "Unknown plugin type '%s'\n", type_label );
	return result;
}

#if NOT_YET
/** convert string labels to in-memory identifiers and visa versa.
    Requered for proper interaction with user-land */
/* takes loc->type_label and loc->plug_label and fills in loc->type_id and loc->id */
	/* it is not necessary to have a non-NULL type label to find a plugin
	   by the plug_label */

int locate_plugin( struct inode *inode, plugin_locator *loc )
{
	reiser4_plugin_type type_id;

	assert( "nikita-548", inode != NULL );
	assert( "nikita-549", loc != NULL );

	if( loc -> type_label[ 0 ] != '\0' )
		loc -> type_id = type_by_label( loc -> type_label );
	type_id = loc -> type_id;
	if( is_type_id_valid( type_id ) ) {
		reiser4_plugin *plugin;

		if( loc -> plug_label[ 0 ] != '\0' )
			plugin = find_plugin( &plugins[ type_id ], 
					      loc -> plug_label );
		else
			plugin = reiser4_get_plugin( inode, type_id );
		if( plugin == NULL )
			return -ENOENT;

		strncpy( loc -> plug_label, plugin -> h.label,
			 min( MAX_PLUGIN_PLUG_LABEL_LEN, 
			      strlen( plugin -> h.label ) + 1 ) );
		if( loc -> type_label[ 0 ] == '\0' )
			strncpy( loc -> type_label, 
				 plugins[ type_id ].label,
				 min( MAX_PLUGIN_TYPE_LABEL_LEN, 
				      strlen( plugins[ type_id ].label ) + 1 ) );
		loc -> id = plugin -> h.id;
		return 0;
	} else
		return -EINVAL;
	
}
#endif

/** 
 * return plugin by its @type_id and @id.
 *
 * Both arguments are checked for validness: this is supposed to be called
 * from user-level.
 */
reiser4_plugin *plugin_by_unsafe_id( reiser4_plugin_type type_id, 
				     reiser4_plugin_id id )
{
	if( is_type_id_valid( type_id ) ) {
		if( is_plugin_id_valid( type_id, id ) )
			return &plugins[ type_id ].builtin[ id ];
		else
			/* id out of bounds */
			dinfo( "Invalid plugin id: [%i:%i]", type_id, id );
	} else
		/* type_id out of bounds */
		dinfo( "Invalid type_id: %i", type_id );
	return NULL;
}

/** 
 * return plugin by its @type_id and @id
 */
reiser4_plugin *plugin_by_id( reiser4_plugin_type type_id, reiser4_plugin_id id )
{
	assert( "nikita-1651", is_type_id_valid( type_id ) );
	assert( "nikita-1652", is_plugin_id_valid( type_id, id ) );
	return &plugins[ type_id ].builtin[ id ];
}

/** get plugin whose id is stored in disk format */
reiser4_plugin *plugin_by_disk_id( reiser4_tree *tree UNUSED_ARG, 
				 reiser4_plugin_type type_id, d16 *did )
{
	/* what we should do properly is to maintain within each
	   file-system a dictionary that maps on-disk plugin ids to
	   "universal" ids. This dictionary will be resolved on mount
	   time, so that this function will perform just one additional
	   array lookup. */
	return plugin_by_id( type_id, d16tocpu( did ) );
}

int save_plugin_id( reiser4_plugin *plugin, d16 *area )
{
	assert( "nikita-1261", plugin != NULL );
	assert( "nikita-1262", area != NULL );

	cputod16( ( __u16 ) plugin -> h.id, area );
	return 0;
}

plugin_list_head *get_plugin_list( reiser4_plugin_type type_id )
{
	assert( "nikita-1056", is_type_id_valid( type_id ) );
	return &plugins[ type_id ].plugins_list;
}

static int is_type_id_valid( reiser4_plugin_type type_id )
{
	/* "type_id" is unsigned, so no comparison with 0 is
	   necessary */
	return( type_id < REISER4_PLUGIN_TYPES );
}

static int is_plugin_id_valid( reiser4_plugin_type type_id, 
			       reiser4_plugin_id id )
{
	assert( "nikita-1653", is_type_id_valid( type_id ) );
	return( ( id < plugins[ type_id ].builtin_num ) && ( id >= 0 ) );
}

void print_plugin( const char *prefix, reiser4_plugin *plugin )
{
	if( plugin != NULL ) {
		info( "%s: %s (%s:%i)\n",
		      prefix, plugin -> h.desc, 
		      plugin -> h.label, plugin -> h.id );
	} else
		info( "%s: (nil)\n", prefix );
}

static int dump_hook( struct super_block *super UNUSED_ARG, ... )
{
	dinfo( "dump hook called for %p\n", super );
	return 0;
}

static reiser4_plugin_type find_type( const char *label )
{
	reiser4_plugin_type type_id;

	assert( "nikita-550", label != NULL );
		
	for( type_id = 0 ; ( type_id < REISER4_PLUGIN_TYPES ) && 
		     strcmp( label, plugins[ type_id ].label ) ; 
	     ++ type_id ) 
	{;}
	return type_id;
}

/** given plugin label find it within given plugin type by scanning
    array. Used to map user-visible symbolic name to internal kernel
    id */
static reiser4_plugin *find_plugin( reiser4_plugin_type_data *ptype,
				    const char *label )
{
	int i;
	reiser4_plugin *result;

	assert( "nikita-551", ptype != NULL );
	assert( "nikita-552", label != NULL );

	for( i = 0 ; i < ptype -> builtin_num ; ++ i ) {
		result = &ptype -> builtin[ i ];
		if( ! strcmp( result -> h.label, label ) )
			return result;
	}
	return NULL;
}

/** helper function used by object.c to inherit missing plugins from
    parent. Install plugin only if it's not already installed. */
int inherit_if_nil( reiser4_plugin **to, reiser4_plugin **from )
{
	assert( "nikita-553", to != NULL );
	assert( "nikita-554", from != NULL );

	if( *to == NULL ) {
		*to = *from;
		return 1;
	} else
		return 0;
}

/* defined in fs/reiser4/plugin/item/static_stat.c */
extern reiser4_plugin sd_ext_plugins[ LAST_SD_EXTENSION ];
/* defined in fs/reiser4/plugin/hash.c */
extern reiser4_plugin hash_plugins[ LAST_HASH_ID ];
/* defined in fs/reiser4/plugin/tail.c */
extern reiser4_plugin tail_plugins[ LAST_TAIL_ID ];
/* defined in fs/reiser4/plugin/plugin.c */
extern reiser4_plugin hook_plugins[ DUMP_HOOK_ID + 1 ];
/* defined in fs/reiser4/plugin/security/security.c */
extern reiser4_plugin perm_plugins[ RWX_PERM_ID + 1 ];
/* defined in fs/reiser4/plugin/item/item.c */
extern reiser4_plugin item_plugins[ LAST_ITEM_ID ];
/* defined in fs/reiser4/plugin/node/node.c */
extern reiser4_plugin node_plugins[ LAST_NODE_ID ];

reiser4_plugin perm_plugins[] = {
	[ RWX_PERM_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_PERM_PLUGIN_ID,
			.id      = RWX_PERM_ID,
			.pops    = NULL,
			.label   = "rwx",
			.desc    = "standard UNIX permissions",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.u = {
			.perm = {
				.rw_ok     = NULL,
				.lookup_ok = NULL,
				.create_ok = NULL,
				.link_ok   = NULL,
				.unlink_ok = NULL,
				.delete_ok = NULL
			}
		}
	}
};

reiser4_plugin hook_plugins[] = {
	[ DUMP_HOOK_ID ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = REISER4_HOOK_PLUGIN_ID,
			.id      = DUMP_HOOK_ID,
			.pops    = NULL,
			.label   = "dump",
			.desc    = "dump hook",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.u = {
			.hook = {
				.hook = dump_hook
			}
		}
	}
};

static reiser4_plugin *file_fallback( void *subj )
{
	assert( "nikita-555", subj != NULL );

	return guess_plugin_by_mode( ( struct inode * ) subj );
}

static reiser4_plugin *hash_fallback( void *subj UNUSED_ARG )
{
	assert( "nikita-556", subj != NULL );
	assert( "nikita-557", S_ISDIR( ( ( struct inode * ) subj ) -> i_mode ) );

	return &hash_plugins[ R5_HASH_ID ];
}

static reiser4_plugin *tail_fallback( void *subj UNUSED_ARG )
{
	return &tail_plugins[ FOURK_TAIL_ID ];
}

static reiser4_plugin *hook_fallback( void *subj UNUSED_ARG )
{
	return &hook_plugins[ DUMP_HOOK_ID ];
}

static reiser4_plugin *node_fallback( void *subj UNUSED_ARG )
{
	return &node_plugins[ NODE40_ID ];
}

static reiser4_plugin *perm_fallback( void *subj UNUSED_ARG )
{
	return &perm_plugins[ RWX_PERM_ID ];
}

static reiser4_plugin *item_fallback( void *subj UNUSED_ARG )
{
	return &item_plugins[ SD_ITEM_ID ];
}

static reiser4_plugin *sd_ext_fallback( void *subj UNUSED_ARG )
{
	return &sd_ext_plugins[ UNIX_STAT ];
}

static reiser4_plugin_type_data plugins[ REISER4_PLUGIN_TYPES ] = {
	/* C90 initializers */
	[ REISER4_FILE_PLUGIN_ID ] = {
		.type_id       = REISER4_FILE_PLUGIN_ID,
		.fallback      = file_fallback,
		.label         = "file",
		.desc          = "Object plugins",
		.builtin_num   = sizeof_array( file_plugins ),
		.builtin       = file_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	},
	[ REISER4_HASH_PLUGIN_ID ] = {
		.type_id       = REISER4_HASH_PLUGIN_ID,
		.fallback      = hash_fallback,
		.label         = "hash",
		.desc          = "Directory hashes",
		.builtin_num   = sizeof_array( hash_plugins ),
		.builtin       = hash_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	},
	[ REISER4_TAIL_PLUGIN_ID ] = {
		.type_id       = REISER4_TAIL_PLUGIN_ID,
		.fallback      = tail_fallback,
		.label         = "tail",
		.desc          = "Tail inlining policies",
		.builtin_num   = sizeof_array( tail_plugins ),
		.builtin       = tail_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	},
	[ REISER4_HOOK_PLUGIN_ID ] = {
		.type_id       = REISER4_HOOK_PLUGIN_ID,
		.fallback      = hook_fallback,
		.label         = "hook",
		.desc          = "Generic loadable hooks",
		.builtin_num   = sizeof_array( hook_plugins ),
		.builtin       = hook_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	},
	[ REISER4_PERM_PLUGIN_ID ] = {
		.type_id       = REISER4_PERM_PLUGIN_ID,
		.fallback      = perm_fallback,
		.label         = "perm",
		.desc          = "Permission checks",
		.builtin_num   = sizeof_array( perm_plugins ),
		.builtin       = perm_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	},
	[ REISER4_ITEM_PLUGIN_ID ] = {
		.type_id       = REISER4_ITEM_PLUGIN_ID,
		.fallback      = item_fallback,
		.label         = "item",
		.desc          = "Item handlers",
		.builtin_num   = sizeof_array( item_plugins ),
		.builtin       = item_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	},
	[ REISER4_NODE_PLUGIN_ID ] = {
		.type_id       = REISER4_NODE_PLUGIN_ID,
		.fallback      = node_fallback,
		.label         = "node",
		.desc          = "node layout handlers",
		.builtin_num   = sizeof_array( node_plugins ),
		.builtin       = node_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	},
	[ REISER4_SD_EXT_PLUGIN_ID ] = {
		.type_id       = REISER4_SD_EXT_PLUGIN_ID,
		.fallback      = sd_ext_fallback,
		.label         = "sd_ext",
		.desc          = "Parts of stat-data",
		.builtin_num   = sizeof_array( sd_ext_plugins ),
		.builtin       = sd_ext_plugins,
		.plugins_list  = TS_LIST_HEAD_ZERO
	}
};

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
