/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Basic plugin infrastructure. Locking, lookup etc.
 */

/* $Id$ */

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

Nikita, be consistent in whether it is a perm or a sec plugin.


 *  . item plugin
 *  . node plugin
 *
 * Object plugins determine how file-system objects
 * serves standard VFS requests for read, write, seek, mmap etc.
 * Instances of object plugins are: regular file, directory, symlink, audit.
 *
 * Hash plugins compute hashes used by reiser4 to store and locate
 * files within directories. Instances of hash plugin type are: r5, 
 * tea, rupasov.
 * 
 * Tail plugins (or, more specifically, tail policy plugins) determine
 * when last part of the file should be stored in a direct item.
 * 
 * Perm plugins control permissions granted for process accessing file.
 *
 *
 * Scope and lookup:
 *
 * Plugins are persistent, and it is necessary to assign them if you
 * don't want the default plugins.  The ways of assigning or
 * inheriting them are:

INSERT TEXT HERE....

We need to create objects.  Do we create them by assigning to non-existent objects like perl does?  I think yes.

We need to define multiple aspects of the object when creating it.
Additionally, we need to assign default values to those aspects of the
definition not defined.  The problem arises when we have a multi-part
definition.  We should avoid assigning one part, then assigning
default values for all other parts, then overwriting those default
values, some of which actually cannot be overwritten (e.g. pluginid).

This means we need to name the object, and then perform multiple assignments

--POSIX-only, --rich-semantics, 


cat "michael_jackson=denied" >> filename/..acl

cat: permission denied

Example: reiser4("transcrash/(create/(glove_location_file/..plugin/(object/audit/encrypted, sec/acl)<=\"\", glove_location_file/..acl/michael_jackson<=\"disallowed\", glove_location_file/..audit/mailto<=\"teletubbies@pbs.org\", glove_location_file<=\"the teletubbies stole it\")) "

This inserts an audit plugin on top of an encrypted file plugin.

 *
 *
 * Each plugin type has a unique label. Each plugin instance also has a label
 * such that the pair ( type_label, plugin_label ) is unique. This pair is a global,
 * persistent, and user-visible plugin identifier. Internally the kernel maintains
 * plugins and plugin types in arrays using an index into those arrays as
 * plugin and plugin type identifiers. The file-system in turn, also maintains a
 * persistent "dictionary" which is a mapping from plugin label to numerical
 * identifier that is used by file-system objects. On mount time, kernel resolves
 * this dictionary against in-memory arrays and builds a mapping from persistent
 * on-disk numerical ids to the in-memory array indices.
 *
 * This scheme reaches two goals:
 *  . it allows disk data to be independent of internal kernel data
 *    structures and thus potentially survive changes in layout of kernel 
 *    data structures without much computational or storage overhead
 *  . it allows assigning plugins by name from user space 
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
 * There is a natural hierarchy among plugin types. This hierarchy is
 * determined through "used-by" relationship. For example, hash plugin
 * is used by object plugin (in particular, by directory plugin); audit
 * plugin is used by any instance of object plugin and so on.
 *
 * There exists only one instance of each plugin instance, but this single
 * instance can be associated with many entities (file-system objects,
 * items, nodes, transactions, file-descriptors etc.). Entity to which
 * plugin of given type is termed "subject" of this plugin type and, by
 * abuse of terminology, subject of particular instance of this type to
 * which it's attached currently. For example, inode is subject of
 * object plugin type. Inode representing directory is subject of
 * directory plugin, hash plugin type and some particular instance of
 * hash plugin type. Inode, representing regular file is subject of
 * "regular file" plugin, tail-policy plugin type etc.
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
 *    + reiser4("filename/..file_plugin<='audit:on'");
 *    + write(open("filename/..file_plugin"), "audit:on", 8);
 *    + ioctl(open("filename/..file_plugin"), REISER4_SET_PLUGIN, &...);
 *      ioctl() is low priority.
 *
 *  . user level utilities lsplug and chplug to manipulate plugins.
 *    Utilities are not of primary priority. Possibly they will be not
 *    working on v4.0
 *
 *  . mount option "plug" to set-up plugins of super-block.
 *    "plug=foo:bar" will set "bar" as default plugin of type "foo".
 *
 * Limitations: 
 *
 *  . each plugin type has to provide at least one builtin
 *    plugin. This is technical limitation and it can be lifted in the
 *    future.
 * 
 * Module handling:
 *
 *  Code relies on __MOD_{INC|DEC}_USE_COUNT and doesn't explicitly keep
 *  its own track of reference counts. With each super-block a list of
 *  plugins used by this mount is kept in .u.reiser4_sb.s_plug_chain.
 *  Whenever new plugin is set up for inode or super-block, this list is
 *  traversed and new entry is allocated and added if none found. During
 *  umount this list is traversed and reference counts of all plugins
 *  used by this mount are decreased.
 *   
 * NOTE: dynamic plugin loading will be deferred until some future version
 * or until we have enough time to implement it efficiently.
 *
 * Locking:
 *   
 *  Each plugin type is protected by spin-lock. This lock is held during
 *  plugin loading/unloading and during traversal of the list of dynamic
 *  plugins.
 *   
 */
/*
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

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/kmod.h>

#include <asm/uaccess.h>

#include <linux/reiser4_fs.h>
#include <linux/spinlock.h>

#include "debug.h"
#include "plugin/types.h"

/* plugin type representation. Nobody outside of this file
   should care about this, so define it right here. */
typedef struct reiser4_plugin_type_data {
	reiser4_plugin_type  type_id;
	reiser4_plugin_id    fallback;
	char                 *label;
	char                 *desc;
	unsigned int          builtin_num;
	reiser4_plugin       *builtin;
	struct list_head      dynamic;
	spinlock_t            custodian;
} reiser4_plugin_type_data;

/* public interface */

/** initialise plugin sub-system. Just call this once on reiser4 startup. */
int init_plugins( void );
/** main function for external code to get plugins associated with 
    given file-system object */
reiser4_plugin_ref *reiser4_get_object_plugin( struct inode *inode );
/** set plugin associated with object. Called from ioctl handlers etc. */
int reiser4_set_object_plugin( struct inode *inode, reiser4_plugin_id id );
/** fill super-block plugin context by "fallback" plugins */
int reiser4_default_plugin_suite_super( struct super_block *super );
/** fill inode plugin context by plugins from parent or
    inode's super-block if parent is NULL */
int reiser4_default_plugin_suite_inode( struct inode *parent,
					struct inode *inode );
/** parse mount time option and update super-block plugins
    appropriately. Option should has form "type:label", where
    "type" is label of plugin type and "label" is label of
    plugin instance within this type. */
int reiser4_handle_plugin_option( struct super_block *super, char *option );
/** set plugin for super-block */
int reiser4_set_plugin_super( struct super_block *super, 
			      reiser4_plugin_type type_id,
			      reiser4_plugin_id id );
/** update plugins of super-block to handle legacy
    mount options like "hash", "notail" etc. Also set up
    plugin context of root directory if it is read already. */
int reiser4_setup_plugins( struct super_block *super );
/** lookup plugin by scanning tables */
reiser4_plugin *lookup_plugin( char *type_label, char *plug_label );
/** convert string labels to in-memory identifiers and visa versa.
    Requered for proper interaction with user-land */
int locate_plugin( struct inode *inode, plugin_locator *loc );

/* internal functions. */

static reiser4_plugin *entry_to_plugin( struct list_head *entry );
static reiser4_plugin *get_plugin( reiser4_plugin_type type_id,
				   reiser4_plugin_id id );
static int is_type_id_valid( reiser4_plugin_type type_id );
static void print_plugin( const char *prefix, reiser4_plugin *plugin );
static reiser4_plugin **get_inode_plugin_suite( struct inode *inode );
static reiser4_plugin **get_super_plugin_suite( struct super_block *super );
static void lock_type( reiser4_plugin_type type_id );
static void unlock_type( reiser4_plugin_type type_id );
static reiser4_plugin_type find_type( const char *label );
static reiser4_plugin *find_plugin( reiser4_plugin_type_data *ptype, 
				    const char *label );
static reiser4_plugin_type_data plugins[ reiser4_plugin_types ];
static reiser4_plugin_id max_id = 0;

int init_plugins( void )
{
	reiser4_plugin_type type_id;

	dinfo( "Builtin plugins:\n" );
	for( type_id = 0 ; type_id < reiser4_plugin_types ; ++ type_id ) {
		reiser4_plugin_type_data *ptype;
		int i;

		ptype = &plugins[ type_id ];
		INIT_LIST_HEAD( &ptype -> dynamic );
		dinfo( "Of type %s (%s):\n", ptype -> label, ptype -> desc );
		for( i = 0 ; i < ptype -> builtin_num ; ++ i ) {
			reiser4_plugin *plugin;

			plugin = &ptype -> builtin[ i ];
			assert( plugin -> h.type_id == type_id );
			plugin -> h.id = i;
			plugin -> h.builtin = 1;
			print_plugin( "\t", plugin ); 
			if( plugin -> h.id > max_id ) {
				max_id = plugin -> h.id;
			}
		}
	}
	return 0;
}


reiser4_plugin_ref *reiser4_get_object_plugin( struct inode *inode )
{
	assert( inode != NULL );
	assert( inode -> u.reiser4_i.plugin.plugin -> h.type_id == 
		reiser4_file_plugin_id );

	return &inode -> u.reiser4_i.plugin;
}

int reiser4_set_object_plugin( struct inode *inode, reiser4_plugin_id id )
{
	reiser4_plugin *plugin;
	const reiser4_plugin_type file_type = reiser4_file_plugin_id;

	assert( inode != NULL );

	lock_type( file_type );
	plugin = get_plugin( file_type, id );
	if( plugin == NULL )
		plugin = get_plugin( file_type, plugins[ file_type ].fallback );
	else
	unlock_type( file_type );
	assert( plugin -> h.type_id == file_type );
	if( plugin != NULL )
		/* ->change() will update state correspondingly */
		return plugin -> h.pops -> change( inode, plugin );
	else
		return -ENOENT;
}

int reiser4_default_plugin_suite_super( struct super_block *super )
{
	reiser4_plugin_type type_id;

	assert( super != NULL );

	for( type_id = 0 ; type_id < reiser4_plugin_types ; ++ type_id ) {
		lock_type( type_id );
		get_super_plugin_suite( super )[ type_id ] = 
			get_plugin( type_id, plugins[ type_id ].fallback );
		assert( get_super_plugin_suite( super )[ type_id ] != NULL );
		unlock_type( type_id );
	}
	return 0;
}

/* parent may be NULL */
int reiser4_default_plugin_suite_inode( struct inode *parent,
					struct inode *inode )
{
	reiser4_plugin **suite;

	assert( inode != NULL );

	if( parent != NULL )
		suite = get_inode_plugin_suite( parent );
	else
		suite = get_super_plugin_suite( inode -> i_sb );
	memcpy( get_inode_plugin_suite( inode ), suite,
		sizeof( reiser4_plugin_suite ) );
	return 0;
}

int reiser4_handle_plugin_option( struct super_block *super, char *option )
{
	int result;
	char *type_label;
	char *plug_label;

	assert( super != NULL );
	assert( option != NULL );
	
	result = 0;
	type_label = option;
	plug_label = strchr( option, ':' );
	if( plug_label != NULL ) {
		reiser4_plugin *plugin;
		
		*plug_label = '\0';
		++ plug_label;

		plugin = lookup_plugin( type_label, plug_label );
		if( plugin != NULL ) {
			get_super_plugin_suite( super )
				[ plugin -> h.type_id ] = plugin;
			print_plugin( "installed", plugin );
		}
	} else {
		info( "Use 'plug=type:label'\n" );
		result = -EINVAL;
	}
	return result;
}

int reiser4_set_plugin_super( struct super_block *super, 
			      reiser4_plugin_type type_id,
			      reiser4_plugin_id id )
{
	reiser4_plugin *plugin;

	assert( super != NULL );
	assert( is_type_id_valid( type_id ) );
	
	lock_type( type_id );
	plugin = get_plugin( type_id, id );
	get_super_plugin_suite( super )[ type_id ] = plugin;
	if( plugin != NULL )
		add_plugin_to_super( plugin, super );
	unlock_type( type_id );
	return plugin != NULL;
}

int reiser4_setup_plugins( struct super_block *super )
{
	reiser4_plugin_id hash_f;
#if defined( CONFIG_REISER4_CHECK )
	reiser4_plugin_type type_id;
#endif
	extern int what_hash (struct super_block *); /* from super.c */

	assert( super != NULL );
	
	switch( what_hash( super ) ) {
	case TEA_HASH:
		hash_f = tea_hash_id;
		break;
	case YURA_HASH:
		hash_f = rupasov_hash_id;
		break;
	default:
		info( "Unknown hash: %i. Falling back to r5", 
		      what_hash( super ) );
	case R5_HASH:
		hash_f = r5_hash_id;
		break;
	}
	reiser4_set_plugin_super( super, reiser4_hash_plugin_id, hash_f );
	/* now kludge: root directory is already read and its hash was set-up
	   from reiser4_default_plugin_suite_super(). Update it now. */
	if( super -> s_root && super -> s_root -> d_inode )
		reiser4_default_plugin_suite_inode
			( NULL, super -> s_root -> d_inode );
#if defined( CONFIG_REISER4_CHECK )
	for( type_id = 0 ; type_id < reiser4_plugin_types ; ++ type_id ) {
		print_plugin( "super block", 
			      get_super_plugin_suite( super )[ type_id ] );
	}
#endif
	return 0;
}

int reiser4_release_super_plugins( struct super_block *super )
{
	struct list_head *head;

	assert( super != NULL );

	/* we are not taking any locks here, because this is only
	   supposed to be called from umount, with all file-system
	   activity at zero. */
	head = &super -> u.reiser4_sb.s_plug_chain;
	while( !list_empty( head ) ) {
		super_to_plug_chain *chain;

		chain = list_entry( head -> next, super_to_plug_chain, chain );
		pput( chain -> plugin );
		list_del_init( &chain -> chain );
		kfree( chain );
	}
	return 0;
}

reiser4_plugin *lookup_plugin( char *type_label, char *plug_label )
{
	reiser4_plugin     *result;
	reiser4_plugin_type type_id;
		
	assert( type_label != NULL );
	assert( plug_label != NULL );

	result = NULL;
	type_id = find_type( type_label );
	if( is_type_id_valid( type_id ) ) {

		lock_type( type_id );
		result = find_plugin( &plugins[ type_id ], plug_label );
		unlock_type( type_id );
		if( result == NULL )
			info( "Unknown plugin: %s\n", plug_label );
	} else
		info( "Unknown plugin type '%s'\n", type_label );
	return result;
}

static __inline__ int min( unsigned int a, unsigned int b )
{
	if (a > b)
		a = b; 
	return a;
}

int locate_plugin( struct inode *inode, plugin_locator *loc )
{
	reiser4_plugin_type type_id;

	assert( inode != NULL );
	assert( loc != NULL );

	if( loc -> type_label[ 0 ] != '\0' )
		loc -> type_id = find_type( loc -> type_label );
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

static reiser4_plugin *entry_to_plugin( struct list_head *entry )
{
	assert( entry != NULL );
	return list_entry( entry, reiser4_plugin, h.chain );
}

/* called with .custodian held */
static reiser4_plugin *get_plugin( reiser4_plugin_type type_id,
				   reiser4_plugin_id id )
{
	reiser4_plugin *result;

	result = NULL;

	if( is_type_id_valid( type_id ) ) {
		reiser4_plugin_type_data *ptype;

		ptype = &plugins[ type_id ];

		/* reiser4_plugin_id is unsigned and cannot be negative */
		if( id < ptype -> builtin_num )
			result = &ptype -> builtin[ id ];
		else if( REISER4_DYNAMIC_PLUGINS ) {
			struct list_head *scan;

			/* lookup dynamically loaded plugin */
			list_for_each( scan, &ptype -> dynamic ) {
				result = entry_to_plugin( scan );
				if( result -> h.id == id )
					break;
				else
					result = NULL;
			}
		}
	} else {
		/* type_id out of bounds */
		dinfo( "Invalid type_id: %i", type_id );
	}
	return result;
}

static int is_type_id_valid( reiser4_plugin_type type_id )
{
	return( ( 0 <= type_id ) && ( type_id < reiser4_plugin_types ) );
}

static void print_plugin( const char *prefix, reiser4_plugin *plugin )
{
	if( plugin != NULL ) {
		dinfo( "%s: %s (%s:%i)\n",
		       prefix,
		       plugin -> h.desc, 
		       plugin -> h.label, plugin -> h.id );
	} else
		dinfo( "%s: (nil)\n", prefix );
}

static reiser4_plugin **get_inode_plugin_suite( struct inode *inode )
{
	assert( inode != NULL );
	return ( reiser4_plugin ** ) inode -> u.reiser4_i.i_plugins;
}

static reiser4_plugin **get_super_plugin_suite( struct super_block *super )
{
	assert( super != NULL );
	return ( reiser4_plugin ** ) super -> u.reiser4_sb.s_plugins;
}

static void lock_type( reiser4_plugin_type type_id )
{
	assert( is_type_id_valid( type_id ) );
	spin_lock( &plugins[ type_id ].custodian );
}

static void unlock_type( reiser4_plugin_type type_id )
{
	assert( is_type_id_valid( type_id ) );
	spin_unlock( &plugins[ type_id ].custodian );
}

static int dump_hook( struct super_block *super, ... )
{
	dinfo( "dump hook called for %p\n", super );
	return 0;
}

static reiser4_plugin_type find_type( const char *label )
{
	reiser4_plugin_type type_id;

	assert( label != NULL );
		
	for( type_id = 0 ; ( type_id < reiser4_plugin_types ) && 
		     strcmp( label, plugins[ type_id ].label ) ; 
	     ++ type_id ) 
		{;}
	return type_id;
}

static reiser4_plugin *find_plugin( reiser4_plugin_type_data *ptype,
				    const char *label )
{
	int i;
	struct list_head *scan;
	reiser4_plugin *result;

	assert( ptype != NULL );
	assert( label != NULL );

	for( i = 0 ; i < ptype -> builtin_num ; ++ i ) {
		result = &ptype -> builtin[ i ];
		if( ! strcmp( result -> h.label, label ) )
			return result;
	}
	return NULL;
}

reiser4_plugin perm_plugins[] = {
	[ rwx_perm_id ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = reiser4_perm_plugin_id,
			.id      = rwx_perm_id,
			.label   = "rwx",
			.desc    = "standard UNIX permissions",
			.chain   = { NULL, NULL },
		},
		.u = {
			.perm = {
				.permission = vfs_permission
			}
		}
	}
};

/* these exist solely for debugging plugin code */

reiser4_plugin hook_plugins[] = {
	[ dump_hook_id ] = {
		.h = {
			.rec_len = sizeof( reiser4_plugin ),
			.type_id = reiser4_hook_plugin_id,
			.id      = dump_hook_id,
			.label   = "dump",
			.desc    = "dump hook",
			.chain   = { NULL, NULL },
		},
		.u = {
			.hook = {
				.hook = dump_hook
			}
		}
	}
};

static reiser4_plugin_type_data plugins[ reiser4_plugin_types ] = {
	/* C90 initializers */
	[ reiser4_hash_plugin_id ] = {
		.type_id       = reiser4_hash_plugin_id,
		.fallback      = r5_hash_id,
		.label         = "hash",
		.desc          = "Directory hashes",
		.builtin_num   = sizeof hash_plugins / sizeof hash_plugins[ 0 ],
		.builtin       = hash_plugins,
		.dynamic       = { NULL, NULL },
		.custodian     = SPIN_LOCK_UNLOCKED
	},
	[ reiser4_tail_plugin_id ] = {
		.type_id       = reiser4_tail_plugin_id,
		.fallback      = fourK_tail_id,
		.label         = "tail",
		.desc          = "Tail inlining policies",
		.builtin_num   = sizeof tail_plugins / sizeof tail_plugins[ 0 ],
		.builtin       = tail_plugins,
		.dynamic       = { NULL, NULL },
		.custodian     = SPIN_LOCK_UNLOCKED
	},
	[ reiser4_hook_plugin_id ] = {
		.type_id       = reiser4_hook_plugin_id,
		.fallback      = dump_hook_id,
		.label         = "hook",
		.desc          = "Generic loadable hooks",
		.builtin_num   = sizeof hook_plugins / sizeof hook_plugins[ 0 ],
		.builtin       = hook_plugins,
		.dynamic       = { NULL, NULL },
		.custodian     = SPIN_LOCK_UNLOCKED
	},
	[ reiser4_perm_plugin_id ] = {
		.type_id       = reiser4_perm_plugin_id,
		.fallback      = rwx_perm_id,
		.label         = "perm",
		.desc          = "Permission checks",
		.builtin_num   = sizeof perm_plugins / sizeof perm_plugins[ 0 ],
		.builtin       = perm_plugins,
		.dynamic       = { NULL, NULL },
		.custodian     = SPIN_LOCK_UNLOCKED
	},
	[ reiser4_audi_plugin_id ] = {
		.type_id       = reiser4_audi_plugin_id,
		.fallback      = no_audi_id,
		.label         = "audi",
		.desc          = "Audit",
		.builtin_num   = sizeof audi_plugins / sizeof audi_plugins[ 0 ],
		.builtin       = audi_plugins,
		.dynamic       = { NULL, NULL },
		.custodian     = SPIN_LOCK_UNLOCKED
	}
};

EXPORT_SYMBOL( reiser4_register_plugin );
EXPORT_SYMBOL( reiser4_unregister_plugin );
EXPORT_SYMBOL( get_unused_plugin_id );

/*
 * $Log$
 * Revision 1.8  2001/08/29 20:37:32  reiser
 * renamed to plugin.c
 *
 * Revision 1.7  2001/08/29 19:27:03  reiser
 * We need to clean up code before writing more.
 *
 * Revision 1.6  2001/08/26 22:05:25  reiser
 * Contains less cruft, but still lots of cruft, Nikita, you have things in here that you agreed to change but have not.
 *
 * Revision 1.5  2001/08/17 13:26:48  reiser
 * *** empty log message ***
 *
 * Revision 1.4  2001/08/16 11:18:29  god
 * removed. Content is in plugin/plugin.c
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.3  2001/08/15 19:24:56  god
 * get rid of "plugin contexts"
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.2  2001/08/15 19:08:05  reiser
 * Base.c was moved to plugin.c, and plugin.c had already been copied to file/file.c.
 *
 * Nikita did not implement plugins using statically compiled in array offsets like he was asked.  He should do so.  I want this code to be trivially simple, I want it done early.
 *
 * Revision 1.1  2001/08/15 15:45:50  god
 * import plugin prototype from 3.6
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 */
/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
