/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* stat data manipulation. */

#include "../../forward.h"
#include "../../super.h"
#include "../../vfs_ops.h"
#include "../../inode.h"
#include "../../debug.h"
#include "../../dformat.h"
#include "../object.h"
#include "../plugin.h"
#include "../plugin_header.h"
#include "static_stat.h"
#include "item.h"

#include <linux/types.h>
#include <linux/fs.h>

/* see static_stat.h for explanation */

/* helper function used while we are dumping/loading inode/plugin state
    to/from the stat-data. */

static void
next_stat(int *length /* space remaining in stat-data */ ,
	  char **area /* current coord in stat data */ ,
	  int size_of /* how many bytes to move forward */ )
{
	assert("nikita-615", length != NULL);
	assert("nikita-616", area != NULL);

	*length -= size_of;
	*area += size_of;

	assert("nikita-617", *length >= 0);
}

#if REISER4_DEBUG_OUTPUT
/* ->print() method of static sd item. Prints human readable information about
   sd at @coord */
void
sd_print(const char *prefix /* prefix to print */ ,
	 coord_t * coord /* coord of item */ )
{
	char *sd;
	int len;
	int bit;
	int chunk;
	__u16 mask;
	reiser4_stat_data_base *sd_base;

	assert("nikita-1254", prefix != NULL);
	assert("nikita-1255", coord != NULL);

	sd = item_body_by_coord(coord);
	len = item_length_by_coord(coord);

	sd_base = (reiser4_stat_data_base *) sd;
	if (len < (int) sizeof *sd_base) {
		printk("%s: wrong size: %i < %i\n", prefix, item_length_by_coord(coord), sizeof *sd_base);
		return;
	}

	mask = d16tocpu(&sd_base->extmask);
	printk("%s: extmask: %x\n", prefix, mask);

	next_stat(&len, &sd, sizeof *sd_base);

	for (bit = 0, chunk = 0; (mask != 0); ++bit, mask >>= 1) {
		if (((bit + 1) % 16) != 0) {
			/* handle extension */
			sd_ext_plugin *sdplug;

			sdplug = sd_ext_plugin_by_id(bit);
			if (sdplug == NULL) {
				continue;
			}
			if ((mask & 1) && (sdplug->print != NULL)) {
				/* alignment is not supported in node layout
				   plugin yet.
				 result = align( inode, &len, &sd, 
				 sdplug -> alignment );
				 if( result != 0 )
				 return result; */
				sdplug->print(prefix, &sd, &len);
			}
		} else if (mask & 1) {
			/* next portion of bitmask */
			if (len < (int) sizeof (d16)) {
				warning("nikita-2708", "No space for bitmap");
				break;
			}
			mask = d16tocpu((d16 *) sd);
			next_stat(&len, &sd, sizeof (d16));
			++chunk;
			if (chunk == 3) {
				if (!(mask & 0x8000)) {
					/* clear last bit */
					mask &= ~0x8000;
					continue;
				}
				/* too much */
				warning("nikita-2709", "Too many extensions");
				break;
			}
		} else
			/* bitmask exhausted */
			break;
	}
}
#endif

void
sd_item_stat(const coord_t * coord, void *vp)
{
	reiser4_stat_data_base *sd;
	mode_t mode;
	sd_stat *stat;

	stat = (sd_stat *) vp;
	sd = (reiser4_stat_data_base *) item_body_by_coord(coord);
	mode = 0;		// d16tocpu( &sd -> mode );

	if (S_ISREG(mode))
		stat->files++;
	else if (S_ISDIR(mode))
		stat->dirs++;
	else
		stat->others++;
}

/* helper function used while loading inode/plugin state from stat-data.
    Complain if there is less space in stat-data than was expected.
    Can only happen on disk corruption. */
static int
not_enough_space(struct inode *inode /* object being processed */ ,
		 const char *where /* error message */ )
{
	assert("nikita-618", inode != NULL);

	warning("nikita-619", "Not enough space in %llu while loading %s", get_inode_oid(inode), where);
	return RETERR(-EINVAL);
}

/* helper function used while loading inode/plugin state from
    stat-data. Call it if invalid plugin id was found. */
static int
unknown_plugin(reiser4_plugin_id id /* invalid id */ ,
	       struct inode *inode /* object being processed */ )
{
	warning("nikita-620", "Unknown plugin %i in %llu", id, get_inode_oid(inode));
	return RETERR(-EINVAL);
}

/* helper function used while storing/loading inode/plugin data to/from
    stat-data. Move current coord in stat-data ("area") to position
    aligned up to "alignment" bytes. */
static int
align(struct inode *inode /* object being processed */ ,
      int *length /* space remaining in stat-data */ ,
      char **area /* current coord in stat data */ ,
      int alignment /* required alignment */ )
{
	int delta;

	assert("nikita-621", inode != NULL);
	assert("nikita-622", length != NULL);
	assert("nikita-623", area != NULL);
	assert("nikita-624", alignment > 0);

	delta = round_up(*area, alignment) - *area;
	if (delta > *length)
		return not_enough_space(inode, "padding");
	if (delta > 0)
		next_stat(length, area, delta);
	return 0;
}

/* this is installed as ->init_inode() method of 
    item_plugins[ STATIC_STAT_DATA_IT ] (fs/reiser4/plugin/item/item.c).
    Copies data from on-disk stat-data format into inode.
    Handles stat-data extensions. */
int
sd_load(struct inode *inode /* object being processed */ ,
	char *sd /* stat-data body */ ,
	int len /* length of stat-data */ )
{
	int result;
	int bit;
	int chunk;
	__u16 mask;
	__u64 bigmask;
	reiser4_stat_data_base *sd_base;
	reiser4_inode *state;

	assert("nikita-625", inode != NULL);
	assert("nikita-626", sd != NULL);

	result = 0;
	sd_base = (reiser4_stat_data_base *) sd;
	state = reiser4_inode_data(inode);
	mask = d16tocpu(&sd_base->extmask);
	bigmask = mask;
	inode_set_flag(inode, REISER4_SDLEN_KNOWN);

	next_stat(&len, &sd, sizeof *sd_base);
	for (bit = 0, chunk = 0; (mask != 0) || (bit <= LAST_IMPORTANT_SD_EXTENSION); ++bit, mask >>= 1) {
		if (((bit + 1) % 16) != 0) {
			/* handle extension */
			sd_ext_plugin *sdplug;

			sdplug = sd_ext_plugin_by_id(bit);
			if (sdplug == NULL) {
				warning("nikita-627", "No such extension %i in inode %llu", bit, get_inode_oid(inode));
				result = RETERR(-EINVAL);
				break;
			}
			if (mask & 1) {
				assert("nikita-628", sdplug->present);
				/* alignment is not supported in node layout
				   plugin yet.
				 result = align( inode, &len, &sd, 
				 sdplug -> alignment );
				 if( result != 0 )
				 return result; */
				result = sdplug->present(inode, &sd, &len);
			} else if (sdplug->absent != NULL)
				result = sdplug->absent(inode);
			if (result)
				break;
			/* else, we are looking at the last bit in 16-bit
			   portion of bitmask */
		} else if (mask & 1) {
			/* next portion of bitmask */
			if (len < (int) sizeof (d16)) {
				warning("nikita-629", "No space for bitmap in inode %llu", get_inode_oid(inode));
				result = RETERR(-EINVAL);
				break;
			}
			mask = d16tocpu((d16 *) sd);
			bigmask <<= 16;
			bigmask |= mask;
			next_stat(&len, &sd, sizeof (d16));
			++chunk;
			if (chunk == 3) {
				if (!(mask & 0x8000)) {
					/* clear last bit */
					mask &= ~0x8000;
					continue;
				}
				/* too much */
				warning("nikita-630", "Too many extensions in %llu", get_inode_oid(inode));
				result = RETERR(-EINVAL);
				break;
			}
		} else
			/* bitmask exhausted */
			break;
	}
	scint_pack(&state->extmask, bigmask, GFP_ATOMIC);
	/* common initialisations */
	inode->i_blksize = get_super_private(inode->i_sb)->optimal_io_size;
	if (len > 0)
		warning("nikita-631", "unused space in inode %llu", get_inode_oid(inode));
	return result;
}

/* estimates size of stat-data required to store inode.
    Installed as ->save_len() method of
    item_plugins[ STATIC_STAT_DATA_IT ] (fs/reiser4/plugin/item/item.c). */
int
sd_len(struct inode *inode /* object being processed */ )
{
	unsigned int result;
	__u64 mask;
	int bit;

	assert("nikita-632", inode != NULL);

	result = sizeof (reiser4_stat_data_base);
	mask = scint_unpack(&reiser4_inode_data(inode)->extmask);
	for (bit = 0; mask != 0; ++bit, mask >>= 1) {
		if (mask & 1) {
			sd_ext_plugin *sdplug;

			sdplug = sd_ext_plugin_by_id(bit);
			assert("nikita-633", sdplug != NULL);
			/* no aligment support 
			   result += 
			   round_up( result, sdplug -> alignment ) - result; */
			result += sdplug->save_len(inode);
		}
	}
	result += sizeof (d16) * bit / 16;
	return result;
}

/* saves inode into stat-data.
    Installed as ->save() method of
    item_plugins[ STATIC_STAT_DATA_IT ] (fs/reiser4/plugin/item/item.c). */
int
sd_save(struct inode *inode /* object being processed */ ,
	char **area /* where to save stat-data */ )
{
	int result;
	__u64 emask;
	int bit;
	unsigned int len;
	reiser4_stat_data_base *sd_base;

	assert("nikita-634", inode != NULL);
	assert("nikita-635", area != NULL);

	result = 0;
	emask = scint_unpack(&reiser4_inode_data(inode)->extmask);
	sd_base = (reiser4_stat_data_base *) * area;
	cputod16((unsigned) (emask & 0xffff), &sd_base->extmask);

	*area += sizeof *sd_base;
	len = 0xffffffffu;
	for (bit = 0; emask != 0; ++bit, emask >>= 1) {
		if (emask & 1) {
			if ((bit + 1) % 16 != 0) {
				sd_ext_plugin *sdplug;
				sdplug = sd_ext_plugin_by_id(bit);
				assert("nikita-636", sdplug != NULL);
				/* no alignment support yet
				   align( inode, &len, area, 
				   sdplug -> alignment ); */
				result = sdplug->save(inode, area);
				if (result)
					break;
			} else {
				cputod16((unsigned) (emask & 0xffff), (d16 *) * area);
				*area += sizeof (d16);
			}
		}
	}
	return result;
}

/* stat-data extension handling functions. */

static int
lw_sd_present(struct inode *inode /* object being processed */ ,
	      char **area /* position in stat-data */ ,
	      int *len /* remaining length */ )
{
	if (*len >= (int) sizeof (reiser4_light_weight_stat)) {
		reiser4_light_weight_stat *sd_lw;

		sd_lw = (reiser4_light_weight_stat *) * area;

		inode->i_mode = d16tocpu(&sd_lw->mode);
		inode->i_nlink = d32tocpu(&sd_lw->nlink);
		inode->i_size = d64tocpu(&sd_lw->size);
		next_stat(len, area, sizeof *sd_lw);
		return 0;
	} else
		return not_enough_space(inode, "lw sd");
}

static int
lw_sd_save_len(struct inode *inode UNUSED_ARG	/* object being
						 * processed */ )
{
	return sizeof (reiser4_light_weight_stat);
}

static int
lw_sd_save(struct inode *inode /* object being processed */ ,
	   char **area /* position in stat-data */ )
{
	reiser4_light_weight_stat *sd;

	assert("nikita-2705", inode != NULL);
	assert("nikita-2706", area != NULL);
	assert("nikita-2707", *area != NULL);

	sd = (reiser4_light_weight_stat *) * area;

	cputod16(inode->i_mode, &sd->mode);
	cputod32(inode->i_nlink, &sd->nlink);
	cputod64((__u64) inode->i_size, &sd->size);
	*area += sizeof *sd;
	return 0;
}

#if REISER4_DEBUG_OUTPUT
static void
lw_sd_print(const char *prefix, char **area /* position in stat-data */ ,
	    int *len /* remaining length */ )
{
	reiser4_light_weight_stat *sd;

	sd = (reiser4_light_weight_stat *) * area;
	printk("%s: mode: %o, nlink: %i, size: %llu\n", prefix,
	       d16tocpu(&sd->mode), d32tocpu(&sd->nlink), d64tocpu(&sd->size));
	next_stat(len, area, sizeof *sd);
}
#endif

static int
unix_sd_present(struct inode *inode /* object being processed */ ,
		char **area /* position in stat-data */ ,
		int *len /* remaining length */ )
{
	assert("nikita-637", inode != NULL);
	assert("nikita-638", area != NULL);
	assert("nikita-639", *area != NULL);
	assert("nikita-640", len != NULL);
	assert("nikita-641", *len > 0);

	if (*len >= (int) sizeof (reiser4_unix_stat)) {
		reiser4_unix_stat *sd;

		sd = (reiser4_unix_stat *) * area;

		inode->i_uid = d32tocpu(&sd->uid);
		inode->i_gid = d32tocpu(&sd->gid);
		inode->i_atime.tv_sec = d32tocpu(&sd->atime);
		inode->i_mtime.tv_sec = d32tocpu(&sd->mtime);
		inode->i_ctime.tv_sec = d32tocpu(&sd->ctime);
		inode->i_rdev = val_to_kdev(d32tocpu(&sd->rdev));
		inode_set_bytes(inode, (loff_t) d64tocpu(&sd->bytes));
		next_stat(len, area, sizeof *sd);
		return 0;
	} else
		return not_enough_space(inode, "unix sd");
}

static int
unix_sd_absent(struct inode *inode /* object being processed */ )
{
	inode->i_uid = get_super_private(inode->i_sb)->default_uid;
	inode->i_gid = get_super_private(inode->i_sb)->default_gid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode_set_bytes(inode, inode->i_size);
	/* mark inode as lightweight, so that caller (reiser4_lookup) will
	   complete initialisation by copying [ug]id from a parent. */
	inode_set_flag(inode, REISER4_LIGHT_WEIGHT);
	return 0;
}

/* Audited by: green(2002.06.14) */
static int
unix_sd_save_len(struct inode *inode UNUSED_ARG	/* object being
						 * processed */ )
{
	return sizeof (reiser4_unix_stat);
}

static int
unix_sd_save(struct inode *inode /* object being processed */ ,
	     char **area /* position in stat-data */ )
{
	reiser4_unix_stat *sd;

	assert("nikita-642", inode != NULL);
	assert("nikita-643", area != NULL);
	assert("nikita-644", *area != NULL);

	sd = (reiser4_unix_stat *) * area;
	cputod32(inode->i_uid, &sd->uid);
	cputod32(inode->i_gid, &sd->gid);
	cputod32((__u32) inode->i_atime.tv_sec, &sd->atime);
	cputod32((__u32) inode->i_ctime.tv_sec, &sd->ctime);
	cputod32((__u32) inode->i_mtime.tv_sec, &sd->mtime);
	cputod32(kdev_val(inode->i_rdev), &sd->rdev);
	cputod64((__u64) inode_get_bytes(inode), &sd->bytes);
	*area += sizeof *sd;
	return 0;
}

#if REISER4_DEBUG_OUTPUT
static void
unix_sd_print(const char *prefix, char **area /* position in stat-data */ ,
	      int *len /* remaining length */ )
{
	reiser4_unix_stat *sd;

	sd = (reiser4_unix_stat *) * area;
	printk("%s: uid: %i, gid: %i, atime: %i, mtime: %i, ctime: %i, "
	       "rdev: %o, bytes: %llu\n", prefix,
	       d32tocpu(&sd->uid),
	       d32tocpu(&sd->gid),
	       d32tocpu(&sd->atime),
	       d32tocpu(&sd->mtime), d32tocpu(&sd->ctime), d32tocpu(&sd->rdev), d64tocpu(&sd->bytes));
	next_stat(len, area, sizeof *sd);
}
#endif

static int
large_times_sd_present(struct inode *inode /* object being processed */,
		       char **area /* position in stat-data */,
		       int *len /* remaining length */)
{
	if (*len >= (int) sizeof (reiser4_large_times_stat)) {
		reiser4_large_times_stat *sd_lt;

		sd_lt = (reiser4_large_times_stat *) * area;

		inode->i_atime.tv_nsec = d32tocpu(&sd_lt->atime);
		inode->i_mtime.tv_nsec = d32tocpu(&sd_lt->mtime);
		inode->i_ctime.tv_nsec = d32tocpu(&sd_lt->ctime);

		next_stat(len, area, sizeof *sd_lt);
		return 0;
	} else
		return not_enough_space(inode, "large times sd");
}

static int
large_times_sd_save_len(struct inode *inode UNUSED_ARG	/* object being
						 * processed */ )
{
	return sizeof (reiser4_large_times_stat);
}

static int
large_times_sd_save(struct inode *inode /* object being processed */ ,
	   char **area /* position in stat-data */ )
{
	reiser4_large_times_stat *sd;

	assert("nikita-2817", inode != NULL);
	assert("nikita-2818", area != NULL);
	assert("nikita-2819", *area != NULL);

	sd = (reiser4_large_times_stat *) * area;

	cputod32((__u32) inode->i_atime.tv_nsec, &sd->atime);
	cputod32((__u32) inode->i_ctime.tv_nsec, &sd->ctime);
	cputod32((__u32) inode->i_mtime.tv_nsec, &sd->mtime);

	*area += sizeof *sd;
	return 0;
}

#if REISER4_DEBUG_OUTPUT
static void
large_times_sd_print(const char *prefix, char **area /* position in stat-data */,
		     int *len /* remaining length */ )
{
	reiser4_large_times_stat *sd;

	sd = (reiser4_large_times_stat *) * area;
	printk("%s: nanotimes: a: %i, m: %i, c: %i\n", prefix,
	       d32tocpu(&sd->atime), d32tocpu(&sd->mtime), d32tocpu(&sd->ctime));
	next_stat(len, area, sizeof *sd);
}
#endif

/* symlink stat data extention */

/* allocate memory for symlink target and attach it to inode->u.generic_ip */
static int
symlink_target_to_inode(struct inode *inode, const char *target, int len)
{
	assert("vs-845", inode->u.generic_ip == 0);
	assert("vs-846", !inode_get_flag(inode, REISER4_GENERIC_VP_USED));

	/* FIXME-VS: this is prone to deadlock. Not more than other similar
	   places, though */
	inode->u.generic_ip = reiser4_kmalloc((size_t) len + 1, GFP_KERNEL);
	if (!inode->u.generic_ip)
		return RETERR(-ENOMEM);

	xmemcpy((char *) (inode->u.generic_ip), target, (size_t) len);
	((char *) (inode->u.generic_ip))[len] = 0;
	inode_set_flag(inode, REISER4_GENERIC_VP_USED);
	return 0;
}

/* this is called on read_inode. There is nothing to do actually, but some
   sanity checks */
static int
symlink_sd_present(struct inode *inode, char **area, int *len)
{
	int result;
	int length;
	reiser4_symlink_stat *sd;

	length = (int) inode->i_size;
	/* *len is number of bytes in stat data item from *area to the end of
	   item. It must be not less than size of symlink + 1 for ending 0 */
	assert("vs-839", length <= *len);
	assert("vs-840", *(*area + length) == 0);

	sd = (reiser4_symlink_stat *) * area;
	result = symlink_target_to_inode(inode, sd->body, length);

	next_stat(len, area, length + 1);
	return result;
}

/* symlink_sd_absent */

static int
symlink_sd_save_len(struct inode *inode)
{
	/* FIXME-VS: no alignment */
	return inode->i_size + 1;
}

/* this is called on create and update stat data. Do nothing on update but
   update @area */
static int
symlink_sd_save(struct inode *inode, char **area)
{
	int result;
	int length;
	reiser4_symlink_stat *sd;

	length = (int) inode->i_size;
	/* inode->i_size must be set already */
	assert("vs-841", length);

	result = 0;
	sd = (reiser4_symlink_stat *) * area;
	if (!inode_get_flag(inode, REISER4_GENERIC_VP_USED)) {
		const char *target;

		target = (const char *) (inode->u.generic_ip);
		inode->u.generic_ip = 0;

		result = symlink_target_to_inode(inode, target, length);

		/* copy symlink to stat data */
		xmemcpy(sd->body, target, (size_t) length);
		(*area)[length] = 0;
	} else {
		/* there is nothing to do in update but move area */
		assert("vs-844", !memcmp(inode->u.generic_ip, sd->body, (size_t) length + 1));
	}

	*area += (length + 1);
	return result;
}

#if REISER4_DEBUG_OUTPUT
static void
symlink_sd_print(const char *prefix, char **area /* position in stat-data */ ,
		 int *len /* remaining length */ )
{
	reiser4_symlink_stat *sd;
	int length;

	sd = (reiser4_symlink_stat *) * area;
	length = strlen(sd->body);
	printk("%s: \"%s\"\n", prefix, sd->body);
	next_stat(len, area, length + 1);
}
#endif

static int
gaf_sd_present(struct inode *inode /* object being processed */ ,
	       char **area /* position in stat-data */ ,
	       int *len /* remaining length */ )
{
	assert("nikita-645", inode != NULL);
	assert("nikita-646", area != NULL);
	assert("nikita-647", *area != NULL);
	assert("nikita-648", len != NULL);
	assert("nikita-649", *len > 0);

	if (*len >= (int) sizeof (reiser4_gen_and_flags_stat)) {
		reiser4_gen_and_flags_stat *sd;

		sd = (reiser4_gen_and_flags_stat *) * area;

		inode->i_flags = d32tocpu(&sd->flags);
		inode->i_generation = d32tocpu(&sd->generation);

		next_stat(len, area, sizeof *sd);
		return 0;
	} else
		return not_enough_space(inode, "generation and attrs");
}

/* Audited by: green(2002.06.14) */
static int
gaf_sd_save_len(struct inode *inode UNUSED_ARG	/* object being
						 * processed */ )
{
	return sizeof (reiser4_gen_and_flags_stat);
}

/* Audited by: green(2002.06.14) */
static int
gaf_sd_save(struct inode *inode /* object being processed */ ,
	    char **area /* position in stat-data */ )
{
	reiser4_gen_and_flags_stat *sd;

	assert("nikita-650", inode != NULL);
	assert("nikita-651", area != NULL);
	assert("nikita-652", *area != NULL);

	sd = (reiser4_gen_and_flags_stat *) * area;
	cputod32(inode->i_generation, &sd->generation);
	cputod32(inode->i_flags, &sd->flags);
	*area += sizeof *sd;
	return 0;
}

static int plugin_sd_absent(struct inode *inode);
static int
plugin_sd_present(struct inode *inode /* object being processed */ ,
		  char **area /* position in stat-data */ ,
		  int *len /* remaining length */ )
{
	reiser4_plugin_stat *sd;
	reiser4_plugin *plugin;
	int i;
	__u16 mask;
	int result;
	int num_of_plugins;

	assert("nikita-653", inode != NULL);
	assert("nikita-654", area != NULL);
	assert("nikita-655", *area != NULL);
	assert("nikita-656", len != NULL);
	assert("nikita-657", *len > 0);

	if (*len < (int) sizeof (reiser4_plugin_stat)) {
		return not_enough_space(inode, "plugin");
	}

	sd = (reiser4_plugin_stat *) * area;

	mask = 0;
	num_of_plugins = d16tocpu(&sd->plugins_no);
	next_stat(len, area, sizeof *sd);
	result = 0;
	for (i = 0; i < num_of_plugins; ++i) {
		reiser4_plugin_slot *slot;

		slot = (reiser4_plugin_slot *) * area;
		if (*len < (int) sizeof *slot)
			return not_enough_space(inode, "additional plugin");
		plugin = plugin_by_id(d16tocpu(&slot->type_id), d16tocpu(&slot->id));
		if (plugin == NULL) {
			return unknown_plugin(d16tocpu(&slot->id), inode);
		}
		/* plugin is loaded into inode, mark this into inode's
		   bitmask of loaded non-standard plugins */
		if (!(mask & (1 << plugin->h.type_id))) {
			mask |= (1 << plugin->h.type_id);
		} else {
			warning("nikita-658", "duplicate plugin for %llu", get_inode_oid(inode));
			print_plugin("plugin", plugin);
			return RETERR(-EINVAL);
		}
		next_stat(len, area, sizeof *slot);
		if (plugin->h.pops == NULL)
			continue;
		align(inode, len, area, plugin->h.pops->alignment);
		/* load plugin data, if any */
		if (plugin->h.pops->load) {
			result = plugin->h.pops->load(inode, plugin, area, len);
			if (result != 0) {
				return result;
			}
		}
	}
	/* if object plugin wasn't loaded from stat-data, guess it by
	   mode bits */
	plugin = file_plugin_to_plugin(inode_file_plugin(inode));
	if (plugin == NULL) {
		result = plugin_sd_absent(inode);
	}
	/* FIXME-VS: activate was called here */

	reiser4_inode_data(inode)->plugin_mask = mask;
	return result;
}

/* Audited by: green(2002.06.14) */
static int
plugin_sd_absent(struct inode *inode /* object being processed */ )
{
	int result;

	assert("nikita-659", inode != NULL);

	result = guess_plugin_by_mode(inode);
	/* if mode was wrong, guess_plugin_by_mode() returns "regular file",
	   but setup_inode_ops() will call make_bad_inode().
	   Another, more logical but bit more complex solution is to add 
	   "bad-file plugin". */
	/* FIXME-VS: activate was called here */
	return result;
}

/* helper function for plugin_sd_save_len(): calculate how much space
    required to save state of given plugin */
/* Audited by: green(2002.06.14) */
static int
len_for(reiser4_plugin * plugin /* plugin to save */ ,
	struct inode *inode /* object being processed */ , int len)
{
	assert("nikita-661", inode != NULL);

	if (plugin && (reiser4_inode_data(inode)->plugin_mask & (1 << (plugin->h.type_id)))) {
		len += sizeof (reiser4_plugin_slot);
		if (plugin->h.pops && plugin->h.pops->save_len != NULL) {
			/* non-standard plugin, call method */
			len = round_up(len, plugin->h.pops->alignment);
			len += plugin->h.pops->save_len(inode, plugin);
		}
	}
	return len;
}

/* calculate how much space is required to save state of all plugins,
    associated with inode */
static int
plugin_sd_save_len(struct inode *inode /* object being processed */ )
{
	int len;
	reiser4_inode *state;

	assert("nikita-663", inode != NULL);

	state = reiser4_inode_data(inode);
	/* common case: no non-standard plugins */
	if (state->plugin_mask == 0)
		return 0;
	len = sizeof (reiser4_plugin_stat);
	/* AUDIT this looks really ugly. And are you going to add more plugins
	   here later hardwired???
	   Why not simply get len_for() to return size of that exact plugin?
	   Addition can be performed here. Also probably some kind of loop
	   should be done through all plugins, not blind hardwiring of all
	   plugins known at compilation time */
	len = len_for(file_plugin_to_plugin(state->pset->file), inode, len);
	len = len_for(perm_plugin_to_plugin(state->pset->perm), inode, len);
	len = len_for(tail_plugin_to_plugin(state->pset->tail), inode, len);
	len = len_for(hash_plugin_to_plugin(state->pset->hash), inode, len);
	len = len_for(crypto_plugin_to_plugin(state->pset->crypto), inode, len);
	len = len_for(digest_plugin_to_plugin(state->pset->digest), inode, len);
	len = len_for(compression_plugin_to_plugin(state->pset->compression), inode, len);
	assert("nikita-664", len > (int) sizeof (reiser4_plugin_stat));
	return len;
}

/* helper function for plugin_sd_save(): save plugin, associated with
    inode. */
static int
save_plug(reiser4_plugin * plugin /* plugin to save */ ,
	  struct inode *inode /* object being processed */ ,
	  char **area /* position in stat-data */ ,
	  int *count		/* incremented if plugin were actually
				 * saved. */ )
{
	reiser4_plugin_slot *slot;
	int fake_len;
	int result;

	assert("nikita-665", inode != NULL);
	assert("nikita-666", area != NULL);
	assert("nikita-667", *area != NULL);

	if (plugin == NULL)
		return 0;
	if (!(reiser4_inode_data(inode)->plugin_mask & (1 << plugin->h.type_id)))
		return 0;
	slot = (reiser4_plugin_slot *) * area;
	cputod16(plugin->h.type_id, &slot->type_id);
	cputod16((unsigned) plugin->h.id, &slot->id);
	fake_len = (int) 0xffff;
	next_stat(&fake_len, area, sizeof *slot);
	++*count;
	result = 0;
	if (plugin->h.pops != NULL) {
		align(inode, &fake_len, area, plugin->h.pops->alignment);
		if (plugin->h.pops->save != NULL)
			result = plugin->h.pops->save(inode, plugin, area);
	}
	return result;
}

/* save state of all non-standard plugins associated with inode */
static int
plugin_sd_save(struct inode *inode /* object being processed */ ,
	       char **area /* position in stat-data */ )
{
	int result;
	int num_of_plugins;
	reiser4_plugin_stat *sd;
	reiser4_inode *state;
	int fake_len;

	assert("nikita-669", inode != NULL);
	assert("nikita-670", area != NULL);
	assert("nikita-671", *area != NULL);

	state = reiser4_inode_data(inode);
	if (state->plugin_mask == 0)
		return 0;
	sd = (reiser4_plugin_stat *) * area;
	fake_len = (int) 0xffff;
	next_stat(&fake_len, area, sizeof *sd);

	num_of_plugins = 0;
	/* for now, use hardcoded list of plugins that can be associated
	   with inode */
	/* AUDIT. Hardcoded list of plugins is bad */
	result = save_plug(file_plugin_to_plugin(state->pset->file), inode, area, &num_of_plugins)
	    || save_plug(perm_plugin_to_plugin(state->pset->perm), inode, area, &num_of_plugins)
	    || save_plug(tail_plugin_to_plugin(state->pset->tail), inode, area, &num_of_plugins)
            || save_plug(hash_plugin_to_plugin(state->pset->hash), inode, area, &num_of_plugins)
	    || save_plug(crypto_plugin_to_plugin(state->pset->crypto), inode, area, &num_of_plugins)
	    || save_plug(digest_plugin_to_plugin(state->pset->digest), inode, area, &num_of_plugins)
	    || save_plug(compression_plugin_to_plugin(state->pset->compression), inode, area, &num_of_plugins);

	cputod16((unsigned) num_of_plugins, &sd->plugins_no);
	return result;
}


/* helper function for crypto_sd_present(), crypto_sd_save.
   Allocates memory for crypto stat, keyid and attaches it to the inode */
 
static int crypto_stat_to_inode (struct inode *inode,
				 crypto_stat_t * tmp,
				 unsigned int size /* fingerprint size */)
{
	crypto_stat_t * stat;
	
	assert ("edward-11", (reiser4_inode_data(inode))->crypt == NULL);
	assert ("edward-33", !inode_get_flag(inode, REISER4_CRYPTO_STAT_LOADED));
	
	stat = reiser4_kmalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat)
		return RETERR(-ENOMEM);
	stat->keyid = reiser4_kmalloc((size_t)size, GFP_KERNEL);
	if (!stat->keyid) {
		reiser4_kfree(stat, sizeof(*stat));
		return RETERR(-ENOMEM);
	}
	/* load inode crypto-stat */
	stat->keysize = tmp->keysize;
	xmemcpy(stat->keyid, tmp->keyid, (size_t)size);
	(reiser4_inode_data(inode))->crypt = stat;
	
	inode_set_flag(inode, REISER4_CRYPTO_STAT_LOADED);
	return 0;
}

/* crypto stat-data extension */

static int crypto_sd_present(struct inode *inode, char **area, int *len)
{
	int result;
	reiser4_crypto_stat *sd;
	crypto_stat_t stat;
	digest_plugin * dplug = inode_digest_plugin(inode);
	unsigned int keyid_size;
	
	assert("edward-06", dplug != NULL);
	assert("edward-07", area != NULL);
	assert("edward-08", *area != NULL);
	assert("edward-09", len != NULL);
	assert("edward-10", *len > 0);

	if (*len < (int) sizeof (reiser4_crypto_stat)) {
		return not_enough_space(inode, "crypto-sd");
	}	

	keyid_size = dplug->digestsize;
	/* *len is number of bytes in stat data item from *area to the end of
	   item. It must be not less than size of this extension */
	assert("edward-75", sizeof(*sd) + keyid_size <= *len);
	
	sd = (reiser4_crypto_stat *) * area;
	stat.keysize = d16tocpu(&sd->keysize);
	stat.keyid = (__u8 *)sd->keyid;
	
	result = crypto_stat_to_inode(inode, &stat, keyid_size);
	next_stat(len, area, sizeof(*sd) + keyid_size);
	return result;
}

static int crypto_sd_save_len(struct inode *inode)
{
	return (sizeof(reiser4_crypto_stat) + inode_digest_plugin(inode)->digestsize);
}

static int crypto_sd_save(struct inode *inode, char **area) 
{
	int result = 0;
	reiser4_crypto_stat *sd;
	digest_plugin * dplug = inode_digest_plugin(inode);

	assert("edward-12", dplug != NULL);
	assert("edward-13", area != NULL);
	assert("edward-14", *area != NULL);
	assert("edward-76", reiser4_inode_data(inode) != NULL);
	
	sd = (reiser4_crypto_stat *) *area;
	if (!inode_get_flag(inode, REISER4_CRYPTO_STAT_LOADED)) {
		/* file is just created, so update inode's crypto-stat
		   which is a pointer to the temporary data */ 
		crypto_stat_t * stat = reiser4_inode_data(inode)->crypt;
		
		assert("edward-15", stat != NULL);

		reiser4_inode_data(inode)->crypt = NULL;
		result = crypto_stat_to_inode(inode, stat, dplug->digestsize);
		/* copy inode crypto-stat to the disk stat-data */
		cputod16(stat->keysize, &sd->keysize);
		xmemcpy(sd->keyid, stat->keyid, (size_t)dplug->digestsize);
	} else {
		/* do nothing */
	}
	*area += (sizeof(*sd) + dplug->digestsize);
	return result;
}

#if REISER4_DEBUG_OUTPUT
static void
crypto_sd_print(const char *prefix, char **area /* position in stat-data */ ,
		 int *len /* remaining length */ )
{
	/* FIXME-EDWARD Make sure we debug only with none digest plugin */
	digest_plugin * dplug = digest_plugin_by_id(NONE_DIGEST_ID);
	reiser4_crypto_stat *sd = (reiser4_crypto_stat *) * area;
	
	printk("%s: keysize: %u keyid: \"%llx\"\n", prefix, d16tocpu(&sd->keysize), *(__u64 *)(sd->keyid));
	next_stat(len, area, sizeof(*sd) + dplug->digestsize);
}
#endif

/* cluster stat-data extension */

static int cluster_sd_present(struct inode *inode, char **area, int *len)
{
	reiser4_inode * info;
	
	assert("edward-77", inode != NULL);
	assert("edward-78", area != NULL);
	assert("edward-79", *area != NULL);
	assert("edward-80", len != NULL);
	assert("edward-81", !inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
	
	info = reiser4_inode_data(inode);
	
	assert("edward-82", info != NULL);
	
	if (*len >= (int) sizeof (reiser4_cluster_stat)) {
		reiser4_cluster_stat *sd;
		sd = (reiser4_cluster_stat *) * area;
		info->cluster_shift = d8tocpu(&sd->cluster_shift);
		inode_set_flag(inode, REISER4_CLUSTER_KNOWN);
		next_stat(len, area, sizeof *sd);
		return 0;
	}
	else
		return not_enough_space(inode, "cluster sd");
}

static int cluster_sd_save_len(struct inode *inode UNUSED_ARG)
{
	return sizeof (reiser4_cluster_stat);
}

static int cluster_sd_save(struct inode *inode, char **area)
{
	reiser4_cluster_stat *sd;
	
	assert("edward-106", inode != NULL);
	assert("edward-107", area != NULL);
	assert("edward-108", *area != NULL);
	
	sd = (reiser4_cluster_stat *) * area;
	cputod8(reiser4_inode_data(inode)->cluster_shift, &sd->cluster_shift);
	*area += sizeof *sd;
	return 0;
}

#if REISER4_DEBUG_OUTPUT
static void
cluster_sd_print(const char *prefix, char **area /* position in stat-data */,
		int *len /* remaining length */ )
{
	reiser4_crypto_stat *sd = (reiser4_crypto_stat *) * area;

	printk("%s: %u\n", prefix, d8tocpu(&sd->clust));
	next_stat(len, area, sizeof *sd);
}
#endif

sd_ext_plugin sd_ext_plugins[LAST_SD_EXTENSION] = {
	[LIGHT_WEIGHT_STAT] = {
			       .h = {
				     .type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				     .id = LIGHT_WEIGHT_STAT,
				     .pops = NULL,
				     .label = "light-weight sd",
				     .desc = "sd for light-weight files",
				     .linkage = TS_LIST_LINK_ZERO
			       },
			       .present = lw_sd_present,
			       .absent = NULL,
			       .save_len = lw_sd_save_len,
			       .save = lw_sd_save,
#if REISER4_DEBUG_OUTPUT
			       .print = lw_sd_print,
#endif
			       .alignment = 8
	},
	[UNIX_STAT] = {
		       .h = {
			     .type_id = REISER4_SD_EXT_PLUGIN_TYPE,
			     .id = UNIX_STAT,
			     .pops = NULL,
			     .label = "unix-sd",
			     .desc = "unix stat-data fields",
			     .linkage = TS_LIST_LINK_ZERO
		       },
		       .present = unix_sd_present,
		       .absent = unix_sd_absent,
		       .save_len = unix_sd_save_len,
		       .save = unix_sd_save,
#if REISER4_DEBUG_OUTPUT
		       .print = unix_sd_print,
#endif
		       .alignment = 8
	},
	[LARGE_TIMES_STAT] = {
		       .h = {
			     .type_id = REISER4_SD_EXT_PLUGIN_TYPE,
			     .id = LARGE_TIMES_STAT,
			     .pops = NULL,
			     .label = "64time-sd",
			     .desc = "nanosecond resolution for times",
			     .linkage = TS_LIST_LINK_ZERO
		       },
		       .present = large_times_sd_present,
		       .absent = NULL,
		       .save_len = large_times_sd_save_len,
		       .save = large_times_sd_save,
#if REISER4_DEBUG_OUTPUT
		       .print = large_times_sd_print,
#endif
		       .alignment = 8
	},
	[SYMLINK_STAT] = {
			  /* stat data of symlink has this extension */
			  .h = {
				.type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				.id = SYMLINK_STAT,
				.pops = NULL,
				.label = "symlink-sd",
				.desc = "stat data is appended with symlink name",
				.linkage = TS_LIST_LINK_ZERO
			  },
			  .present = symlink_sd_present,
			  .absent = NULL,
			  .save_len = symlink_sd_save_len,
			  .save = symlink_sd_save,
#if REISER4_DEBUG_OUTPUT
			  .print = symlink_sd_print,
#endif
			  .alignment = 8
	},
	[PLUGIN_STAT] = {
			 .h = {
			       .type_id = REISER4_SD_EXT_PLUGIN_TYPE,
			       .id = PLUGIN_STAT,
			       .pops = NULL,
			       .label = "plugin-sd",
			       .desc = "plugin stat-data fields",
			       .linkage = TS_LIST_LINK_ZERO
			 },
			 .present = plugin_sd_present,
			 .absent = plugin_sd_absent,
			 .save_len = plugin_sd_save_len,
			 .save = plugin_sd_save,
#if REISER4_DEBUG_OUTPUT
			 .print = NULL,
#endif
			 .alignment = 8
	},
	[GEN_AND_FLAGS_STAT] = {
				.h = {
				      .type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				      .id = GEN_AND_FLAGS_STAT,
				      .pops = NULL,
				      .label = "gaf-sd",
				      .desc = "generation and attrs fields",
				      .linkage = TS_LIST_LINK_ZERO}
				,
				.present = gaf_sd_present,
				.absent = NULL,
				.save_len = gaf_sd_save_len,
				.save = gaf_sd_save,
#if REISER4_DEBUG_OUTPUT
				.print = NULL,
#endif
				.alignment = 8
	},
	[CLUSTER_STAT] = {
				.h = {
				      .type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				      .id = CLUSTER_STAT,
				      .pops = NULL,
				      .label = "cluster-sd",
				      .desc = "cluster shift",
				      .linkage = TS_LIST_LINK_ZERO}
				,
				.present = cluster_sd_present,
				.absent = NULL,
				.save_len = cluster_sd_save_len,
				.save = cluster_sd_save,
#if REISER4_DEBUG_OUTPUT
				.print = cluster_sd_print,
#endif
				.alignment = 8
	},
	[CRYPTO_STAT] = {
				.h = {
				      .type_id = REISER4_SD_EXT_PLUGIN_TYPE,
				      .id = CRYPTO_STAT,
				      .pops = NULL,
				      .label = "crypto-sd",
				      .desc = "secret key size and id",
				      .linkage = TS_LIST_LINK_ZERO}
				,
				.present = crypto_sd_present,
				.absent = NULL,
				.save_len = crypto_sd_save_len,
				.save = crypto_sd_save,
#if REISER4_DEBUG_OUTPUT
				.print = crypto_sd_print,
#endif
				.alignment = 8
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
