/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * stat data manipulation.
 */

#include "../../reiser4.h"

/* see static_stat.h for explanation */

/**
 * ->print() method of static sd item. Prints human readable information about
 * sd at @coord
 */
/* Audited by: green(2002.06.14) */
void sd_print( const char *prefix /* prefix to print */, 
	       new_coord *coord /* coord of item */ )
{
	reiser4_stat_data_base *sd_base;

	assert( "nikita-1254", prefix != NULL );
	assert( "nikita-1255", coord != NULL );

	sd_base = ( reiser4_stat_data_base * ) item_body_by_coord( coord );
	if( item_length_by_coord( coord ) < ( int ) sizeof *sd_base ) {
		info( "%s: wrong size: %i < %i\n", prefix,
		      item_length_by_coord( coord ), sizeof *sd_base );
	} else {
		info( "%s: mode: %o, extmask: %x, nlink: %u, size: %qu\n", 
		      prefix,
		      d16tocpu( &sd_base -> mode ), 
		      d16tocpu( &sd_base -> extmask ),
		      d32tocpu( &sd_base -> nlink ), 
		      d64tocpu( &sd_base -> size ) );
		/*
		 * FIXME-NIKITA should call ->print() sub-methods for all sd
		 * extensions.
		 */
	}
}

/** helper function used while we are dumping/loading inode/plugin state
    to/from the stat-data. */
/* Audited by: green(2002.06.14) */
static void move_on( int *length /* space remaining in stat-data */, 
		     char **area /* current coord in stat data */, 
		     int size_of /* how many bytes to move forward */ )
{
	assert( "nikita-615", length != NULL );
	assert( "nikita-616", area != NULL );

	*length -= size_of;
	*area   += size_of;

	assert( "nikita-617", *length >= 0 );
}

/** helper function used while loading inode/plugin state from stat-data.
    Complain if there is less space in stat-data than was expected.
    Can only happen on disk corruption. */
/* Audited by: green(2002.06.14) */
static int not_enough_space( struct inode *inode /* object being processed */, 
			     const char *where /* error message */ )
{
	assert( "nikita-618", inode != NULL );

	warning( "nikita-619", "Not enough space in %lx while loading %s", 
		 ( long ) inode -> i_ino, where );
	return -EINVAL;
}

/** helper funtion used while loading inode/plugin state from
    stat-data. Call it if invalid plugin id was found. */
/* Audited by: green(2002.06.14) */
static int unknown_plugin( reiser4_plugin_id id /* invalid id */, 
			   struct inode *inode /* object being processed */ )
{
	warning( "nikita-620", "Unknown plugin %i in %lx", 
		 id, ( long ) inode -> i_ino );
	return -EINVAL;
}

/** helper function used while storing/loading inode/plugin data to/from
    stat-data. Move current coord in stat-data ("area") to position
    aligned up to "alignment" bytes. */
/* Audited by: green(2002.06.14) */
static int align( struct inode *inode /* object being processed */, 
		  int *length /* space remaining in stat-data */, 
		  char **area /* current coord in stat data */, 
		  int alignment /* required alignment */ )
{
	int delta;

	assert( "nikita-621", inode != NULL );
	assert( "nikita-622", length != NULL );
	assert( "nikita-623", area != NULL );
	assert( "nikita-624", alignment > 0 );

	delta = round_up( *area, alignment ) - *area;
	if( delta > *length )
		return not_enough_space( inode, "padding" );
	if( delta > 0 )
		move_on( length, area, delta );
	return 0;
}

/** this is installed as ->init_inode() method of 
    item_plugins[ STATIC_STAT_DATA_IT ] (fs/reiser4/plugin/item/item.c).
    Copies data from on-disk stat-data format into inode.
    Hanldes stat-data extensions. */
/* Audited by: green(2002.06.14) */
int sd_load( struct inode *inode /* object being processed */, 
	     char *sd /* stat-data body */, 
	     int len /* length of stat-data */ )
{
	int   result;
	int   bit;
	int   chunk;
	__u16 mask;
	reiser4_stat_data_base *sd_base;
	reiser4_inode_info *state;

	assert( "nikita-625", inode != NULL );
	assert( "nikita-626", sd != NULL );

	result = 0;
	sd_base = ( reiser4_stat_data_base * ) sd;
	inode -> i_mode       = d16tocpu( &sd_base -> mode );
	inode -> i_nlink      = d32tocpu( &sd_base -> nlink );
	inode -> i_size       = d64tocpu( &sd_base -> size );
	state = reiser4_inode_data( inode );
	mask = state -> extmask = d16tocpu( &sd_base -> extmask );
	state -> sd_len = len;

	move_on( &len, &sd, sizeof *sd_base );
	for( bit = 0, chunk = 0 ; 
	     ( mask != 0 ) || ( bit <= LAST_IMPORTANT_SD_EXTENSION ) ; 
	     ++ bit, mask >>= 1 ) {
		if( ( ( bit + 1 ) % 16 ) != 0 ) {
			/* handle extension */
			sd_ext_plugin *sdplug;

			sdplug = sd_ext_plugin_by_id( bit );
			if( sdplug == NULL ) {
				warning( "nikita-627", 
					 "No such extension %i in inode %lx",
					 bit, ( long ) inode -> i_ino );
				result = -EINVAL;
				break;
			}
			if( mask & 1 ) {
				assert( "nikita-628", sdplug -> present );
				result = align( inode, &len, &sd, 
						sdplug -> alignment );
				if( result != 0 )
					return result;
				result = sdplug -> present( inode, 
							    &sd, &len );
			} else if( sdplug -> absent != NULL )
				result = sdplug -> absent( inode );
			if( result )
				break;
		/* else, we are looking at the last bit in 16-bit
		   portion of bitmask */
		} else if( mask & 1 ) {
			/* next portion of bitmask */
			if( len < ( int ) sizeof( d16 ) ) {
				warning( "nikita-629", 
					 "No space for bitmap in inode %lx",
					 ( long ) inode -> i_ino );
				result = -EINVAL;
				break;
			}
			mask = d16tocpu( ( d16 * ) sd );
			state -> extmask <<= 16;
			state -> extmask |= mask;
			move_on( &len, &sd, sizeof( d16 ) );
			++ chunk;
			if( chunk == 3 ) {
				if( !( mask & 0x8000 ) ) {
					/* clear last bit */
					mask &= ~0x8000;
					continue;
				}
				/* too much */
				warning( "nikita-630", "Too many extensions in %lx",
					 ( long ) inode -> i_ino );
				result = -EINVAL;
				break;
			}
		} else
			/* bitmask exhausted */
			break;
	}
	/* common initialisations */
	/* AUDIT: Off by one blocks calculation error here. PErhaps
	   (VFS_BLKSIZE-1) should be added to state -> bytes before shifting */
	inode -> i_blocks     = state -> bytes >> VFS_BLKSIZE_BITS;
	inode -> i_blksize    = REISER4_OPTIMAL_IO_SIZE( inode -> i_sb, inode );
	inode -> i_version    = ++ event;
	if( len > 0 )
		warning( "nikita-631", "unused space in inode %lx", 
			 ( long ) inode -> i_ino );
	return result;
}

/** estimates size of stat-data required to store inode.
    Installed as ->save_len() method of
    item_plugins[ STATIC_STAT_DATA_IT ] (fs/reiser4/plugin/item/item.c). */
/* Audited by: green(2002.06.14) */
int sd_len( struct inode *inode /* object being processed */ )
{
	unsigned int result;
	__u64 mask;
	int bit;

	assert( "nikita-632", inode != NULL );

	result = sizeof( reiser4_stat_data_base );
	mask = reiser4_inode_data( inode ) -> extmask;
	for( bit = 0 ; mask != 0 ; ++ bit, mask >>= 1 ) {
		if( mask & 1 ) {
			sd_ext_plugin *sdplug;

			sdplug = sd_ext_plugin_by_id( bit );
			assert( "nikita-633", sdplug != NULL );
			result += 
				round_up( result, sdplug -> alignment ) - result;
			result += sdplug -> save_len( inode );
		}
	}
	result += sizeof( d16 ) * bit / 16;
	return result;
}

/** saves inode into stat-data.
    Installed as ->save() method of
    item_plugins[ STATIC_STAT_DATA_IT ] (fs/reiser4/plugin/item/item.c). */
/* Audited by: green(2002.06.14) */
int sd_save( struct inode *inode /* object being processed */, 
	     char **area /* where to save stat-data */ )
{
	int   result;
	__u64 emask;
	int bit;
	unsigned int len;
	reiser4_stat_data_base *sd_base;

	assert( "nikita-634", inode != NULL );
	assert( "nikita-635", area != NULL );

	result = 0;
	emask = reiser4_inode_data( inode ) -> extmask;
	sd_base = ( reiser4_stat_data_base * ) *area;
	cputod16( inode -> i_mode, &sd_base -> mode );
	cputod16( ( unsigned ) ( emask & 0xffff ), &sd_base -> extmask );
	cputod32( inode -> i_nlink, &sd_base -> nlink );
	cputod64( ( __u64 ) inode -> i_size, &sd_base -> size );

	len = 0xffffffffu;
	for( bit = 0 ; emask != 0 ; ++ bit, emask >>= 1 ) {
		if( emask & 1 ) {
			if( ( bit + 1 ) % 16 != 0 ) {
				sd_ext_plugin *sdplug;
				sdplug = sd_ext_plugin_by_id( bit );
				assert( "nikita-636", sdplug != NULL );
				align( inode, &len, area, 
				       sdplug -> alignment );
				result = sdplug -> save( inode, area );
				if( result )
					break;
			} else {
				cputod16( ( unsigned ) ( emask & 0xffff ),
					  ( d16 * ) *area );
				*area += sizeof( d16 );
			}
		}
	}
	return result;
}

/* stat-data extension handling functions. */

/* Audited by: green(2002.06.14) */
static int unix_sd_present( struct inode *inode /* object being processed */, 
			    char **area /* position in stat-data */, 
			    int *len /* remaining length */ )
{
	assert( "nikita-637", inode != NULL );
	assert( "nikita-638", area != NULL );
	assert( "nikita-639", *area != NULL );
	assert( "nikita-640", len != NULL );
	assert( "nikita-641", *len > 0 );

	if( *len >= ( int ) sizeof( reiser4_unix_stat ) ) {
		reiser4_unix_stat *sd;

		sd = ( reiser4_unix_stat * ) *area;

		inode -> i_uid   = d32tocpu( &sd -> uid );
		inode -> i_gid   = d32tocpu( &sd -> gid );
		inode -> i_atime = d32tocpu( &sd -> atime );
		inode -> i_mtime = d32tocpu( &sd -> mtime );
		inode -> i_ctime = d32tocpu( &sd -> ctime );
		inode -> i_rdev  = val_to_kdev (d32tocpu( &sd -> rdev ));
		reiser4_inode_data( inode ) -> bytes = d64tocpu( &sd -> bytes );
		move_on( len, area, sizeof *sd );
		return 0;
	} else
		return not_enough_space( inode, "unix sd" );
}

/* Audited by: green(2002.06.14) */
static int unix_sd_absent( struct inode *inode /* object being processed */ )
{
	inode -> i_uid = get_super_private( inode -> i_sb ) -> default_uid;
	inode -> i_gid = get_super_private( inode -> i_sb ) -> default_gid;
	inode -> i_atime = inode -> i_mtime = inode -> i_ctime = CURRENT_TIME;
	reiser4_inode_data( inode ) -> bytes = inode -> i_size;
	/* mark inode as lightweight, so that caller (reiser4_lookup)
	   will complete initialisation by copying [ug]id from a
	   parent.*/
	reiser4_inode_data( inode ) -> flags |= REISER4_LIGHT_WEIGHT_INODE;
	return 0;
}

/* Audited by: green(2002.06.14) */
static int unix_sd_save_len( struct inode *inode UNUSED_ARG /* object being
							     * processed */ )
{
	return sizeof( reiser4_unix_stat );
}

/* Audited by: green(2002.06.14) */
static int unix_sd_save( struct inode *inode /* object being processed */, 
			 char **area /* position in stat-data */ )
{
	reiser4_unix_stat *sd;

	assert( "nikita-642", inode != NULL );
	assert( "nikita-643", area != NULL );
	assert( "nikita-644", *area != NULL );

	sd = ( reiser4_unix_stat * ) *area;
	cputod32( inode -> i_uid, &sd -> uid );
	cputod32( inode -> i_gid, &sd -> gid );
	cputod32( ( __u32 ) inode -> i_atime, &sd -> atime );
	cputod32( ( __u32 ) inode -> i_ctime, &sd -> ctime );
	cputod32( ( __u32 ) inode -> i_mtime, &sd -> mtime );
	cputod32( kdev_val( inode -> i_rdev ), &sd -> rdev );
	cputod64( reiser4_inode_data( inode ) -> bytes, &sd -> bytes );
	*area += sizeof *sd;
	return 0;
}

/* Audited by: green(2002.06.14) */
static int gaf_sd_present( struct inode *inode /* object being processed */, 
			   char **area /* position in stat-data */, 
			   int *len /* remaining length */ )
{
	assert( "nikita-645", inode != NULL );
	assert( "nikita-646", area != NULL );
	assert( "nikita-647", *area != NULL );
	assert( "nikita-648", len != NULL );
	assert( "nikita-649", *len > 0 );

	if( *len >= ( int ) sizeof( reiser4_gen_and_flags_stat ) ) {
		reiser4_gen_and_flags_stat *sd;

		sd = ( reiser4_gen_and_flags_stat * ) *area;

		inode -> i_flags      = d32tocpu( &sd -> flags );
		inode -> i_generation = d32tocpu( &sd -> generation );

		move_on( len, area, sizeof *sd );
		return 0;
	} else
		return not_enough_space( inode, "generation and attrs" );
}

/* Audited by: green(2002.06.14) */
static int gaf_sd_save_len( struct inode *inode UNUSED_ARG /* object being
							    * processed */ )
{
	return sizeof( reiser4_gen_and_flags_stat );
}

/* Audited by: green(2002.06.14) */
static int gaf_sd_save( struct inode *inode /* object being processed */, 
			char **area /* position in stat-data */ )
{
	reiser4_gen_and_flags_stat *sd;

	assert( "nikita-650", inode != NULL );
	assert( "nikita-651", area != NULL );
	assert( "nikita-652", *area != NULL );

	sd = ( reiser4_gen_and_flags_stat * ) *area;
	cputod32( inode -> i_generation, &sd -> generation );
	cputod32( inode -> i_flags, &sd -> flags );
	*area += sizeof *sd;
	return 0;
}

static int plugin_sd_absent( struct inode *inode );
/* Audited by: green(2002.06.14) */
static int plugin_sd_present( struct inode *inode /* object being processed */, 
			      char **area /* position in stat-data */, 
			      int *len /* remaining length */ )
{
	reiser4_plugin_stat *sd;
	reiser4_plugin      *plugin;
	int                  i;
	__u16                mask;
	int                  result;
	int                  num_of_plugins;

	assert( "nikita-653", inode != NULL );
	assert( "nikita-654", area != NULL );
	assert( "nikita-655", *area != NULL );
	assert( "nikita-656", len != NULL );
	assert( "nikita-657", *len > 0 );

	if( *len < ( int ) sizeof( reiser4_plugin_stat ) ) {
		return not_enough_space( inode, "plugin" );
	}

	sd = ( reiser4_plugin_stat * ) *area;

	mask = 0;
	num_of_plugins = d16tocpu( &sd -> plugins_no );
	move_on( len, area, sizeof *sd );
	result = 0;
	for( i = 0 ; i < num_of_plugins ; ++ i ) {
		reiser4_plugin_slot *slot;
		
		slot = ( reiser4_plugin_slot * ) *area;
		if( *len < ( int ) sizeof *slot )
			return not_enough_space( inode, "additional plugin" );
		plugin = plugin_by_id( d16tocpu( &slot -> type_id ), 
				       d16tocpu( &slot -> id ) );
		if( plugin == NULL ) {
			return unknown_plugin( d16tocpu( &slot -> id ), inode );
		}
		move_on( len, area, sizeof *slot );
		align( inode, len, area, plugin -> h.pops -> alignment );
		/* load plugin data, if any */
		if( plugin -> h.pops -> load ) {
			result = plugin -> h.pops -> load( inode, 
							   plugin, area, len );
			if( result != 0 ) {
				return result;
			}
		}
		/* plugin is loaded into inode, mark this into inode's
		   bitmask of loaded non-standard plugins */
		if( !( mask & ( 1 << plugin -> h.type_id ) ) ) {
			mask |= ( 1 << plugin -> h.type_id );
		} else {
			warning( "nikita-658", "duplicate plugin for %lx",
				 ( long ) inode -> i_ino );
			print_plugin( "plugin", plugin );
			return -EINVAL;
		}
	}
	/* if object plugin wasn't loaded from stat-data, guess it by
	   mode bits */
	plugin = file_plugin_to_plugin( inode_file_plugin( inode ) );
	if( plugin == NULL ) {
		result = plugin_sd_absent( inode );
	}
	/* FIXME-VS: activate was called here */

	reiser4_inode_data( inode ) -> plugin_mask = mask;
	return result;
}

/* Audited by: green(2002.06.14) */
static int plugin_sd_absent( struct inode *inode /* object being processed */ )
{
	int result;

	assert( "nikita-659", inode != NULL );

	result = guess_plugin_by_mode( inode );
	/* if mode was wrong, guess_plugin_by_mode() returns "regular file",
	   but setup_inode_ops() will call make_bad_inode().
	   Another, more logical but bit more complex solution is to add 
	   "bad-file plugin". */
	/*
	 * FIXME-VS: activate was called here
	 */
	return result;
}

/** helper function for plugin_sd_save_len(): calculate how much space
    required to save state of given plugin */
/* Audited by: green(2002.06.14) */
static int len_for( reiser4_plugin *plugin /* plugin to save */, 
		    struct inode *inode /* object being processed */, int len )
{
	assert( "nikita-661", inode != NULL );
	assert( "nikita-662", plugin != NULL );

	if( reiser4_inode_data( inode ) -> plugin_mask & 
	    ( 1 << ( plugin -> h.type_id ) ) ) {
		len += sizeof( reiser4_plugin_slot );
		if( plugin -> h.pops -> save_len != NULL ) {
			/* non-standard plugin, call method */
			len = round_up( len, plugin -> h.pops -> alignment );
			len += plugin -> h.pops -> save_len( inode, plugin );
		}
	}
	return len;
}

/** calculate how much space is required to save state of all plugins,
    associated with inode */
/* Audited by: green(2002.06.14) */
static int plugin_sd_save_len( struct inode *inode /* object being processed */ )
{
	int                 len;
	reiser4_inode_info *state;

	assert( "nikita-663", inode != NULL );
	
	state = reiser4_inode_data( inode );
	/* common case: no non-standard plugins */
	if( state -> plugin_mask == 0 )
		return 0;
	len = sizeof( reiser4_plugin_stat );
	/* AUDIT this looks really ugly. And are you going to add more plugins
	   here later hardwired???
	   Why not simply get len_for() to return size of that exact plugin?
	   Addition can be performed here. Also probably some kind of loop
	   should be done thriugh all plugins, not blind hardwiring of all
	   plugins known at compilation time */
	len = len_for( file_plugin_to_plugin( state -> file ), inode, len );
	len = len_for( perm_plugin_to_plugin( state -> perm ), inode, len );
	len = len_for( tail_plugin_to_plugin( state -> tail ), inode, len );
	len = len_for( hash_plugin_to_plugin( state -> hash ), inode, len );
	assert( "nikita-664", len > ( int ) sizeof( reiser4_plugin_stat ) );
	return len;
}

/** helper function for plugin_sd_save(): save plugin, associated with
    inode. */
/* Audited by: green(2002.06.14) */
static int save_plug( reiser4_plugin *plugin /* plugin to save */, 
		      struct inode *inode /* object being processed */,
		      char **area /* position in stat-data */, 
		      int *count /* incremented if plugin were actually
				  * saved. */ )
{
	reiser4_plugin_slot *slot;
	int                  fake_len;

	assert( "nikita-665", inode != NULL );
	assert( "nikita-666", area != NULL );
	assert( "nikita-667", *area != NULL );
	assert( "nikita-668", plugin != NULL );

	if( !( reiser4_inode_data( inode ) -> plugin_mask & 
	       ( 1 << plugin -> h.type_id ) ) )
		return 0;
	slot = ( reiser4_plugin_slot * ) *area;
	cputod16( plugin -> h.type_id, &slot -> type_id );
	cputod16( ( unsigned ) plugin -> h.id, &slot -> id );
	fake_len = ( int ) 0xffff;
	move_on( &fake_len, area, sizeof *slot );
	align( inode, &fake_len, area, plugin -> h.pops -> alignment );
	++ *count;
	if( plugin -> h.pops -> save != NULL )
		return plugin -> h.pops -> save( inode, plugin, area );
	else
		return 0;
}

/** save state of all non-standard plugins associated with inode */
/* Audited by: green(2002.06.14) */
static int plugin_sd_save( struct inode *inode /* object being processed */, 
			   char **area /* position in stat-data */ )
{
	int result;
	int num_of_plugins;
	reiser4_plugin_stat *sd;
	reiser4_inode_info  *state;
	int fake_len;

	assert( "nikita-669", inode != NULL );
	assert( "nikita-670", area != NULL );
	assert( "nikita-671", *area != NULL );

	state = reiser4_inode_data( inode );
	if( state -> plugin_mask == 0 )
		return 0;
	sd = ( reiser4_plugin_stat * ) *area;
	fake_len = ( int ) 0xffff;
	move_on( &fake_len, area, sizeof *sd );

	num_of_plugins = 0;
	/* for now, use hardcoded list of plugins that can be associated
	   with inode */
	/* AUDIT. Hardcoded list of plugins is bad */
	result = 
		save_plug( file_plugin_to_plugin( state -> file ), inode, area, &num_of_plugins ) &&
		save_plug( perm_plugin_to_plugin( state -> perm ), inode, area, &num_of_plugins ) &&
		save_plug( tail_plugin_to_plugin( state -> tail ), inode, area, &num_of_plugins ) &&
		save_plug( hash_plugin_to_plugin( state -> hash ), inode, area, &num_of_plugins );

	cputod16( ( unsigned ) num_of_plugins, &sd -> plugins_no );
	return result;
}

reiser4_plugin sd_ext_plugins[ LAST_SD_EXTENSION ] = {
	[ UNIX_STAT ] = {
		.sd_ext = {
			.h = {
				.type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				.id      = UNIX_STAT,
				.pops    = NULL,
				.label   = "unix-sd",
				.desc    = "unix stat-data fields",
				.linkage = TS_LIST_LINK_ZERO
			},
			.present   = unix_sd_present, 
			.absent    = unix_sd_absent,
			.save_len  = unix_sd_save_len,
			.save      = unix_sd_save,
			.alignment = 8
		}
	},
	[ PLUGIN_STAT ] = {
		.sd_ext = {
			.h = {
				.type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				.id      = PLUGIN_STAT,
				.pops    = NULL,
				.label   = "plugin-sd",
				.desc    = "plugin stat-data fields",
				.linkage = TS_LIST_LINK_ZERO
			},
			.present   = plugin_sd_present, 
			.absent    = plugin_sd_absent,
			.save_len  = plugin_sd_save_len,
			.save      = plugin_sd_save,
			.alignment = 8
		}
	},
	[ GEN_AND_FLAGS_STAT ] = {
		.sd_ext = {
			.h = {
				.type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				.id      = GEN_AND_FLAGS_STAT,
				.pops    = NULL,
				.label   = "gaf-sd",
				.desc    = "generation and attrs fields",
				.linkage = TS_LIST_LINK_ZERO
			},
			.present   = gaf_sd_present, 
			.absent    = NULL,
			.save_len  = gaf_sd_save_len,
			.save      = gaf_sd_save,
			.alignment = 8
		}
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
