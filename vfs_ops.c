/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to VFS. Reiser4 {file|inode|address_space|dentry}_operations
   are defined here. */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "plugin/file/file.h"
#include "plugin/security/perm.h"
#include "plugin/oid/oid.h"
#include "plugin/disk_format/disk_format.h"
#include "plugin/plugin.h"
#include "plugin/plugin_set.h"
#include "plugin/plugin_hash.h"
#include "plugin/object.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "trace.h"
#include "vfs_ops.h"
#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"
#include "kattr.h"
#include "entd.h"
#include "emergency_flush.h"

#include <linux/profile.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>
#include <linux/quotaops.h>
#include <linux/security.h>

/* super operations */

static struct inode *reiser4_alloc_inode(struct super_block *super);
static void reiser4_destroy_inode(struct inode *inode);
static void reiser4_drop_inode(struct inode *);
static void reiser4_delete_inode(struct inode *);
static void reiser4_write_super(struct super_block *);
static int reiser4_statfs(struct super_block *, struct statfs *);
static void reiser4_kill_super(struct super_block *);
static int reiser4_show_options(struct seq_file *m, struct vfsmount *mnt);
static int reiser4_fill_super(struct super_block *s, void *data, int silent);
#if 0
static void reiser4_dirty_inode(struct inode *);
static void reiser4_write_inode(struct inode *, int);
static void reiser4_put_inode(struct inode *);
static void reiser4_write_super_lockfs(struct super_block *);
static void reiser4_unlockfs(struct super_block *);
static int reiser4_remount_fs(struct super_block *, int *, char *);
static void reiser4_clear_inode(struct inode *);
static struct dentry *reiser4_fh_to_dentry(struct super_block *sb, __u32 * fh, int len, int fhtype, int parent);
static int reiser4_dentry_to_fh(struct dentry *, __u32 * fh, int *lenp, int need_parent);
#endif

extern struct dentry_operations reiser4_dentry_operation;

static struct file_system_type reiser4_fs_type;

/* return number of files in a filesystem. It is used in reiser4_statfs to
   fill ->f_ffiles */
/* Audited by: umka (2002.06.12) */
static long
oids_used(struct super_block *s	/* super block of file system in
				 * queried */ )
{
	oid_allocator_plugin *oplug;
	__u64 used;

	assert("umka-076", s != NULL);
	assert("vs-484", get_super_private(s));

	oplug = get_super_private(s)->oid_plug;
	if (!oplug || !oplug->oids_used)
		return (long) -1;

	used = oplug->oids_used(&get_super_private(s)->oid_allocator);
	if (used < (__u64) ((long) ~0) >> 1)
		return (long) used;
	else
		return (long) -1;
}

/* number of oids available for use by users. It is used in reiser4_statfs to
   fill ->f_ffree */
/* Audited by: umka (2002.06.12) */
static long
oids_free(struct super_block *s	/* super block of file system in
				 * queried */ )
{
	oid_allocator_plugin *oplug;
	__u64 used;

	assert("umka-077", s != NULL);
	assert("vs-485", get_super_private(s));

	oplug = get_super_private(s)->oid_plug;
	if (!oplug || !oplug->oids_free)
		return (long) -1;

	used = oplug->oids_free(&get_super_private(s)->oid_allocator);
	if (used < (__u64) ((long) ~0) >> 1)
		return (long) used;
	else
		return (long) -1;
}

/* ->statfs() VFS method in reiser4 super_operations */
/* Audited by: umka (2002.06.12) */
static int
reiser4_statfs(struct super_block *super	/* super block of file
						 * system in queried */ ,
	       struct statfs *buf	/* buffer to fill with
					 * statistics */ )
{
	long bfree;
	reiser4_context ctx;
	
	assert("nikita-408", super != NULL);
	assert("nikita-409", buf != NULL);

	init_context(&ctx, super);
	reiser4_stat_inc(vfs_calls.statfs);

	buf->f_type = statfs_type(super);
	buf->f_bsize = super->s_blocksize;

	buf->f_blocks = reiser4_block_count(super);
        bfree = reiser4_free_blocks(super);
	buf->f_bfree = bfree;
	
	buf->f_bavail = buf->f_bfree - reiser4_reserved_blocks(super, 0, 0);
	buf->f_files = oids_used(super);
	buf->f_ffree = oids_free(super);

	/* maximal acceptable name length depends on directory plugin. */
	buf->f_namelen = -1;

	reiser4_exit_context(&ctx);
	return 0;
}

/* this is called whenever mark_inode_dirty is to be called. It links ("captures") inode to the atom. This allows to
   postpone stat data update until atom commits */
int
reiser4_mark_inode_dirty(struct inode *inode)
{
	assert("vs-1207", is_in_reiser4_context());
	return reiser4_update_sd(inode);
	/*return capture_inode(inode);*/
}

/* update inode stat-data by calling plugin */
int
reiser4_update_sd(struct inode *object)
{
        file_plugin *fplug;

	assert("nikita-2338", object != NULL);

	if (IS_RDONLY(object))
		return 0;

	fplug = inode_file_plugin(object);
	assert("nikita-2339", fplug != NULL);
	return fplug->write_sd_by_inode(object);
}

/* helper function: increase inode nlink count and call plugin method to save
   updated stat-data.
  
   Used by link/create and during creation of dot and dotdot in mkdir
*/
int
reiser4_add_nlink(struct inode *object /* object to which link is added */ ,
		  struct inode *parent /* parent where new entry will be */ ,
		  int write_sd_p	/* true is stat-data has to be
					 * updated */ )
{
	file_plugin *fplug;
	int result;

	assert("nikita-1351", object != NULL);

	fplug = inode_file_plugin(object);
	assert("nikita-1445", fplug != NULL);

	/* ask plugin whether it can add yet another link to this
	   object */
	if (!fplug->can_add_link(object)) {
		return -EMLINK;
	}

	assert("nikita-2211", fplug->add_link != NULL);
	/* call plugin to do actual addition of link */
	result = fplug->add_link(object, parent);
	if ((result == 0) && write_sd_p)
		result = fplug->write_sd_by_inode(object);
	return result;
}

/* helper function: decrease inode nlink count and call plugin method to save
   updated stat-data.
  
   Used by unlink/create
*/
int
reiser4_del_nlink(struct inode *object	/* object from which link is
					 * removed */ ,
		  struct inode *parent /* parent where entry was */ ,
		  int write_sd_p	/* true is stat-data has to be
					 * updated */ )
{
	file_plugin *fplug;
	int result;

	assert("nikita-1349", object != NULL);

	fplug = inode_file_plugin(object);
	assert("nikita-1350", fplug != NULL);
	assert("nikita-1446", object->i_nlink > 0);
	assert("nikita-2210", fplug->rem_link != NULL);

	/* call plugin to do actual deletion of link */
	result = fplug->rem_link(object, parent);
	if ((result == 0) && write_sd_p)
		result = fplug->write_sd_by_inode(object);
	return result;
}

/* initial prefix of names of pseudo-files like ..plugin, ..acl,
    ..whatnot, ..and, ..his, ..dog 

    Reminder: This is an optional style convention, not a requirement.
    If anyone builds in any dependence in the parser or elsewhere on a
    prefix existing for all pseudo files, and thereby hampers creating
    pseudo-files without this prefix, I will be pissed.  -Hans */
static const char PSEUDO_FILES_PREFIX[] = "..";

/* Return and lazily allocate if necessary per-dentry data that we
   attach to each dentry. */
/* Audited by: umka (2002.06.12) */
reiser4_dentry_fsdata *
reiser4_get_dentry_fsdata(struct dentry *dentry	/* dentry
						 * queried */ )
{
	assert("nikita-1365", dentry != NULL);

	if (dentry->d_fsdata == NULL) {
		reiser4_stat_inc(vfs_calls.private_data_alloc);
		/* NOTE-NIKITA use slab in stead */
		dentry->d_fsdata = kmalloc(sizeof (reiser4_dentry_fsdata), 
					   GFP_KERNEL);
		if (dentry->d_fsdata == NULL)
			return ERR_PTR(RETERR(-ENOMEM));
		xmemset(dentry->d_fsdata, 0, sizeof (reiser4_dentry_fsdata));
	}
	return dentry->d_fsdata;
}

void
reiser4_free_dentry_fsdata(struct dentry *dentry /* dentry released */ )
{
	if (dentry->d_fsdata != NULL) {
		kfree(dentry->d_fsdata);
		dentry->d_fsdata = NULL;
	}
}

/* Release reiser4 dentry. This is d_op->d_delease() method. */
/* Audited by: umka (2002.06.12) */
static void
reiser4_d_release(struct dentry *dentry /* dentry released */ )
{
	reiser4_free_dentry_fsdata(dentry);
}

/* Return and lazily allocate if necessary per-file data that we attach
   to each struct file. */
reiser4_file_fsdata *
reiser4_get_file_fsdata(struct file *f	/* file
					 * queried */ )
{
	assert("nikita-1603", f != NULL);

	if (f->private_data == NULL) {
		reiser4_file_fsdata *fsdata;
		struct inode *inode;

		reiser4_stat_inc(vfs_calls.private_data_alloc);
		/* NOTE-NIKITA use slab in stead */
		fsdata = reiser4_kmalloc(sizeof *fsdata, GFP_KERNEL);
		if (fsdata == NULL)
			return ERR_PTR(RETERR(-ENOMEM));
		xmemset(fsdata, 0, sizeof *fsdata);

		inode = f->f_dentry->d_inode;
		spin_lock_inode(inode);
		if (f->private_data == NULL) {
			fsdata->back = f;
			readdir_list_clean(fsdata);
			f->private_data = fsdata;
			fsdata = NULL;
		}
		spin_unlock_inode(inode);
		if (fsdata != NULL)
			/* other thread initialised ->fsdata */
			reiser4_kfree(fsdata, sizeof *fsdata);
	}
	assert("nikita-2665", f->private_data != NULL);
	return f->private_data;
}

/* our ->read_inode() is no-op. Reiser4 inodes should be loaded
    through fs/reiser4/inode.c:reiser4_iget() */
static void
noop_read_inode(struct inode *inode UNUSED_ARG)
{
}

/* initialisation and shutdown */

/* slab cache for inodes */
static kmem_cache_t *inode_cache;

/* initalisation function passed to the kmem_cache_create() to init new pages
   grabbed by our inodecache. */
static void
init_once(void *obj /* pointer to new inode */ ,
	  kmem_cache_t * cache UNUSED_ARG /* slab cache */ ,
	  unsigned long flags /* cache flags */ )
{
	reiser4_inode_object *info;

	info = obj;

	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) == SLAB_CTOR_CONSTRUCTOR) {
		/* NOTE-NIKITA add here initialisations for locks, list heads,
		   etc. that will be added to our private inode part. */
		inode_init_once(&info->vfs_inode);
		info->p.eflushed = 0;
		INIT_LIST_HEAD(&info->p.moved_pages);
		readdir_list_init(get_readdir_list(&info->vfs_inode));
		INIT_LIST_HEAD(&info->p.eflushed_jnodes);
		/* inode's builtin jnode is initialized in reiser4_alloc_inode */
	}
}

/* initialise slab cache where reiser4 inodes will live */
int
init_inodecache(void)
{
	inode_cache = kmem_cache_create("reiser4_icache",
					sizeof (reiser4_inode_object), 0, SLAB_HWCACHE_ALIGN, init_once, NULL);
	return (inode_cache != NULL) ? 0 : RETERR(-ENOMEM);
}

/* initialise slab cache where reiser4 inodes lived */
/* Audited by: umka (2002.06.12) */
static void
destroy_inodecache(void)
{
	if (kmem_cache_destroy(inode_cache) != 0)
		warning("nikita-1695", "not all inodes were freed");
}

/* ->alloc_inode() super operation: allocate new inode */
static struct inode *
reiser4_alloc_inode(struct super_block *super UNUSED_ARG	/* super block new
								 * inode is
								 * allocated for */ )
{
	reiser4_inode_object *obj;

	assert("nikita-1696", super != NULL);
	reiser4_stat_inc_at(super, vfs_calls.alloc_inode);
	obj = kmem_cache_alloc(inode_cache, SLAB_KERNEL);
	if (obj != NULL) {
		reiser4_inode *info;

		info = &obj->p;

		info->pset = plugin_set_get_empty();
		scint_init(&info->extmask);
		info->locality_id = 0ull;
		info->plugin_mask = 0;
#if !REISER4_INO_IS_OID
		info->oid_hi = 0;
#endif
		seal_init(&info->sd_seal, NULL, NULL);
		coord_init_invalid(&info->sd_coord, NULL);
		xmemset(&info->ra, 0, sizeof info->ra);
		info->expkey = NULL;
		info->keyid = NULL;
		info->flags = 0;
		spin_inode_object_init(info);

		/* initizalize inode's jnode */
		jnode_init(&info->inode_jnode, current_tree);
		jnode_set_type(&info->inode_jnode, JNODE_INODE);
		atomic_set(&info->inode_jnode.x_count, 1);

		return &obj->vfs_inode;
	} else
		return NULL;
}

/* ->destroy_inode() super operation: recycle inode */
static void
reiser4_destroy_inode(struct inode *inode /* inode being destroyed */)
{
	reiser4_inode *info;

	info = reiser4_inode_data(inode);
	reiser4_stat_inc_at(inode->i_sb, vfs_calls.destroy_inode);

	assert("vs-1220", list_empty(&info->eflushed_jnodes));
	assert("vs-1222", info->eflushed == 0);

	{
		/* complete with inode's jnode */
		jnode *j;

		j = &info->inode_jnode;
		assert("vs-1243", atomic_read(&j->x_count) == 1);
		atomic_set(&j->x_count, 0);
		JF_SET(j, JNODE_RIP);
		check_me("vs-1242", jnode_try_drop(j) == 0);
	}
	if (!is_bad_inode(inode) && inode_get_flag(inode, REISER4_LOADED)) {

		assert("nikita-2828", reiser4_inode_data(inode)->eflushed == 0);
		if (inode_get_flag(inode, REISER4_GENERIC_VP_USED)) {
			assert("vs-839", S_ISLNK(inode->i_mode));
			reiser4_kfree_in_sb(inode->u.generic_ip, (size_t) inode->i_size + 1, inode->i_sb);
			inode->u.generic_ip = 0;
			inode_clr_flag(inode, REISER4_GENERIC_VP_USED);
		}
		if (inode_get_flag(inode, REISER4_SECRET_KEY_INSTALLED)) {
			/* destroy secret key */
			crypto_plugin *cplug = inode_crypto_plugin(inode);
			assert("edward-35", cplug != NULL);
			assert("edward-37", info->expkey != NULL);
			xmemset(info->expkey, 0, cplug->keysize);
			reiser4_kfree_in_sb(info->expkey, cplug->keysize, inode->i_sb);
			inode_clr_flag(inode, REISER4_SECRET_KEY_INSTALLED);
		}
		if (inode_get_flag(inode, REISER4_KEYID_LOADED)) {
			assert("edward-38", info->keyid != NULL);
			reiser4_kfree_in_sb(info->keyid, sizeof(reiser4_keyid_stat), inode->i_sb);
			inode_clr_flag(inode, REISER4_KEYID_LOADED);
		}
	}
	phash_inode_destroy(inode);
	if (info->pset)
		plugin_set_put(info->pset);

	assert("nikita-2872", list_empty(&info->moved_pages));
	/* cannot add similar assertion about ->i_list as prune_icache return
	 * inode into slab with dangling ->list.{next,prev}. This is safe,
	 * because they are re-initialized in the new_inode(). */
	assert("nikita-2895", list_empty(&inode->i_dentry));
	assert("nikita-2896", hlist_unhashed(&inode->i_hash));
	assert("nikita-2898", readdir_list_empty(get_readdir_list(inode)));
	kmem_cache_free(inode_cache, container_of(info, reiser4_inode_object, p));
}

static void drop_object_body(struct inode *inode)
{
	if (!inode_file_plugin(inode)->pre_delete)
		return;
	if (inode_file_plugin(inode)->pre_delete(inode))
		warning("vs-1216", "Failed to delete file body for %llu)\n",
			get_inode_oid(inode));
}

static void
reiser4_drop_inode(struct inode *object)
{
	file_plugin *fplug;

	assert("nikita-2643", object != NULL);
	reiser4_stat_inc_at(object->i_sb, vfs_calls.drop_inode);

	/* -not- creating context in this method, because it is frequently
	   called and all existing ->not_linked() methods are one liners. */

	fplug = inode_file_plugin(object);
	/* fplug is NULL for fake inode */
	if (fplug != NULL && fplug->not_linked(object)) {
		/* create context here.

		   removal of inode from the hash table (done at the very
		   beginning of generic_delete_inode(), truncate of pages, and
		   removal of file's extents has to be performed in the same
		   atom. Otherwise, it may so happen, that twig node with
		   unallocated extent will be flushed to the disk.
		*/
		reiser4_context ctx;

		init_context(&ctx, object->i_sb);
		/*
		 * FIXME: this resembles generic_delete_inode
		 */
		hlist_del_init(&object->i_hash);
		list_del_init(&object->i_list);
		object->i_state|=I_FREEING;
		inodes_stat.nr_inodes--;
		spin_unlock(&inode_lock);

		uncapture_inode(object);

		if (!is_bad_inode(object))
			drop_object_body(object);

		if (object->i_data.nrpages)
			truncate_inode_pages(&object->i_data, 0);

		security_inode_delete(object);
		if (!is_bad_inode(object))
			DQUOT_INIT(object);

		reiser4_delete_inode(object);
		if (object->i_state != I_CLEAR)
			BUG();
		destroy_inode(object);
		(void)reiser4_exit_context(&ctx);
	} else
		generic_forget_inode(object);
}

/* ->delete_inode() super operation */
static void
reiser4_delete_inode(struct inode *object)
{
	reiser4_context ctx;

	init_context(&ctx, object->i_sb);
	reiser4_stat_inc(vfs_calls.delete_inode);
	if (inode_get_flag(object, REISER4_LOADED)) {
		file_plugin *fplug;
		fplug = inode_file_plugin(object);
		if ((fplug != NULL) && (fplug->delete != NULL))
			fplug->delete(object);
	}

	object->i_blocks = 0;
	clear_inode(object);
	(void)reiser4_exit_context(&ctx);
}

/* Audited by: umka (2002.06.12) */
const char *REISER4_SUPER_MAGIC_STRING = "R4Sb";
const int REISER4_MAGIC_OFFSET = 16 * 4096;	/* offset to magic string from the
						 * beginning of device */

/* type of option parseable by parse_option() */
typedef enum {
	/* value of option is arbitrary string */
	OPT_STRING,
	/* option specifies bit in a bitmask */
	OPT_BIT,
	/* value of option should conform to sprintf() format */
	OPT_FORMAT,
	/* option can take one of predefined values */
	OPT_ONEOF,
	/* option specifies reiser4 plugin */
	OPT_PLUGIN
} opt_type_t;

typedef struct opt_bitmask_bit {
	const char *bit_name;
	int bit_nr;
} opt_bitmask_bit;

/* description of option parseable by parse_option() */
typedef struct opt_desc {
	/* option name.
	   
	   parsed portion of string has a form "name=value".
	*/
	const char *name;
	/* type of option */
	opt_type_t type;
	union {
		/* where to store value of string option (type == OPT_STRING) */
		char **string;
		/* description of bits for bit option (type == OPT_BIT) */
		struct {
			int nr;
			void *addr;
		} bit;
		/* description of format and targets for format option (type
		   == OPT_FORMAT) */
		struct {
			const char *format;
			int nr_args;
			void *arg1;
			void *arg2;
			void *arg3;
			void *arg4;
		} f;
		struct {
			/* NOT YET */
		} oneof;
		/* description of plugin option */
		struct {
			reiser4_plugin **addr;
			const char *type_label;
		} plugin;
		struct {
			void *addr;
			int nr_bits;
			opt_bitmask_bit *bits;
		} bitmask;
	} u;
} opt_desc_t;

/* parse one option */
static int
parse_option(char *opt_string /* starting point of parsing */ ,
	     opt_desc_t * opt /* option description */ )
{
	/* foo=bar, 
	   ^   ^  ^
	   |   |  +-- replaced to '\0'
	   |   +-- val_start
	   +-- opt_string
	*/
	char *val_start;
	int result;
	const char *err_msg;

	/* NOTE-NIKITA think about using lib/cmdline.c functions here. */

	val_start = strchr(opt_string, '=');
	if (val_start != NULL) {
		*val_start = '\0';
		++val_start;
	}

	err_msg = NULL;
	result = 0;
	switch (opt->type) {
	case OPT_STRING:
		if (val_start == NULL) {
			err_msg = "String arg missing";
			result = RETERR(-EINVAL);
		} else
			*opt->u.string = val_start;
		break;
	case OPT_BIT:
		if (val_start != NULL)
			err_msg = "Value ignored";
		else
			set_bit(opt->u.bit.nr, opt->u.bit.addr);
		break;
	case OPT_FORMAT:
		if (val_start == NULL) {
			err_msg = "Formatted arg missing";
			result = RETERR(-EINVAL);
			break;
		}
		if (sscanf(val_start, opt->u.f.format,
			   opt->u.f.arg1, opt->u.f.arg2, opt->u.f.arg3, opt->u.f.arg4) != opt->u.f.nr_args) {
			err_msg = "Wrong conversion";
			result = RETERR(-EINVAL);
		}
		break;
	case OPT_ONEOF:
		not_yet("nikita-2099", "Oneof");
		break;
	case OPT_PLUGIN:{
			reiser4_plugin *plug;

			if (*opt->u.plugin.addr != NULL) {
				err_msg = "Plugin already set";
				result = RETERR(-EINVAL);
				break;
			}

			plug = lookup_plugin(opt->u.plugin.type_label, val_start);
			if (plug != NULL)
				*opt->u.plugin.addr = plug;
			else {
				err_msg = "Wrong plugin";
				result = RETERR(-EINVAL);
			}
			break;
		}
	default:
		wrong_return_value("nikita-2100", "opt -> type");
		break;
	}
	if (err_msg != NULL) {
		warning("nikita-2496", "%s when parsing option \"%s%s%s\"",
			err_msg, opt->name, val_start ? "=" : "", val_start ? : "");
	}
	return result;
}

/* parse options */
static int
parse_options(char *opt_string /* starting point */ ,
	      opt_desc_t * opts /* array with option description */ ,
	      int nr_opts /* number of elements in @opts */ )
{
	int result;

	result = 0;
	while ((result == 0) && opt_string && *opt_string) {
		int j;
		char *next;

		next = strchr(opt_string, ',');
		if (next != NULL) {
			*next = '\0';
			++next;
		}
		for (j = 0; j < nr_opts; ++j) {
			if (!strncmp(opt_string, opts[j].name, strlen(opts[j].name))) {
				result = parse_option(opt_string, &opts[j]);
				break;
			}
		}
		if (j == nr_opts) {
			warning("nikita-2307", "Unrecognized option: \"%s\"", opt_string);
			/* traditionally, -EINVAL is returned on wrong mount
			   option */
			result = RETERR(-EINVAL);
		}
		opt_string = next;
	}
	return result;
}

#define NUM_OPT( label, fmt, addr )				\
		{						\
			.name = ( label ),			\
			.type = OPT_FORMAT,			\
			.u = {					\
				.f = {				\
					.format  = ( fmt ),	\
					.nr_args = 1,		\
					.arg1 = ( addr ),	\
					.arg2 = NULL,		\
					.arg3 = NULL,		\
					.arg4 = NULL		\
				}				\
			}					\
		}

#define SB_FIELD_OPT( field, fmt ) NUM_OPT( #field, fmt, &sbinfo -> field )

#define PLUG_OPT( label, ptype, plug )					\
	{								\
		.name = ( label ),					\
		.type = OPT_PLUGIN,					\
		.u = {							\
			.plugin = {					\
				.type_label = #ptype,			\
				.addr = ( reiser4_plugin ** )( plug )	\
			}						\
		}							\
	}

static int
reiser4_parse_options(struct super_block *s, char *opt_string)
{
	int result;
	reiser4_super_info_data *sbinfo = get_super_private(s);
	char *trace_file_name;

	opt_desc_t opts[] = {
		/* trace_flags=N
		  
		   set trace flags to be N for this mount. N can be C numeric
		   literal recognized by %i scanf specifier.  It is treated as
		   bitfield filled by values of debug.h:reiser4_trace_flags
		   enum
		*/
		SB_FIELD_OPT(trace_flags, "%i"),
		/* debug_flags=N
		  
		   set debug flags to be N for this mount. N can be C numeric
		   literal recognized by %i scanf specifier.  It is treated as
		   bitfield filled by values of debug.h:reiser4_debug_flags
		   enum
		*/
		SB_FIELD_OPT(debug_flags, "%i"),
		/* txnmgr.atom_max_size=N
		  
		   Atoms containing more than N blocks will be forced to
		   commit. N is decimal.
		*/
		SB_FIELD_OPT(txnmgr.atom_max_size, "%u"),
		/* txnmgr.atom_max_age=N
		  
		   Atoms older than N seconds will be forced to commit. N is
		   decimal.
		*/
		SB_FIELD_OPT(txnmgr.atom_max_age, "%u"),
		/* tree.cbk_cache_slots=N
		  
		   Number of slots in the cbk cache.
		*/
		SB_FIELD_OPT(tree.cbk_cache.nr_slots, "%u"),

		/* If flush finds more than FLUSH_RELOCATE_THRESHOLD adjacent
		   dirty leaf-level blocks it will force them to be
		   relocated. */
		SB_FIELD_OPT(flush.relocate_threshold, "%u"),
		/* If flush finds can find a block allocation closer than at
		   most FLUSH_RELOCATE_DISTANCE from the preceder it will
		   relocate to that position. */
		SB_FIELD_OPT(flush.relocate_distance, "%u"),
		/* Flush defers actualy BIO submission until it gathers
		   FLUSH_QUEUE_SIZE blocks. */
		SB_FIELD_OPT(flush.queue_size, "%u"),
		/* If we have written this much or more blocks before
		   encountering busy jnode in flush list - abort flushing
		   hoping that next time we get called this jnode will be
		   clean already, and we will save some seeks. */
		SB_FIELD_OPT(flush.written_threshold, "%u"),
		/* The maximum number of nodes to scan left on a level during
		   flush. */
		SB_FIELD_OPT(flush.scan_maxnodes, "%u"),

		/* preferred IO size */
		SB_FIELD_OPT(optimal_io_size, "%u"),

		/* carry flags used for insertion of new nodes */
		SB_FIELD_OPT(tree.carry.new_node_flags, "%u"),
		/* carry flags used for insertion of new extents */
		SB_FIELD_OPT(tree.carry.new_extent_flags, "%u"),
		/* carry flags used for paste operations */
		SB_FIELD_OPT(tree.carry.paste_flags, "%u"),
		/* carry flags used for insert operations */
		SB_FIELD_OPT(tree.carry.insert_flags, "%u"),

		/* timeout (in seconds) to wait for ent thread in writepage */
		SB_FIELD_OPT(entd.timeout, "%lu"),

#ifdef CONFIG_REISER4_BADBLOCKS
		/* Alternative master superblock location in case if it's original
		   location is not writeable/accessable. This is offset in BYTES. */
		SB_FIELD_OPT(altsuper, "%lu"),
#endif

		PLUG_OPT("plugin.tail", tail, &sbinfo->plug.t),
		PLUG_OPT("plugin.sd", item, &sbinfo->plug.sd),
		PLUG_OPT("plugin.dir_item", item, &sbinfo->plug.dir_item),
		PLUG_OPT("plugin.perm", perm, &sbinfo->plug.p),
		PLUG_OPT("plugin.file", file, &sbinfo->plug.f),
		PLUG_OPT("plugin.dir", dir, &sbinfo->plug.d),
		PLUG_OPT("plugin.hash", hash, &sbinfo->plug.h),

		{
			/* turn on BSD-style gid assignment */
			.name = "bsdgroups",
			.type = OPT_BIT,
			.u = {
				.bit = {
					.nr = REISER4_BSD_GID,
					.addr = &sbinfo->fs_flags
				}
			}
		},

		{
			/* turn on 32 bit times */
			.name = "32bittimes",
			.type = OPT_BIT,
			.u = {
				.bit = {
					.nr = REISER4_32_BIT_TIMES,
					.addr = &sbinfo->fs_flags
				}
			}
		},
		{
			/* turn on concurrent flushing */
			.name = "mtflush",
			.type = OPT_BIT,
			.u = {
				.bit = {
					.nr = REISER4_MTFLUSH,
					.addr = &sbinfo->fs_flags
				}
			}
		},
		{
			/* tree traversal readahead parameters:
			   -o readahead:MAXNUM:FLAGS
			   MAXNUM - max number fo nodes to request readahead for: -1UL will set it to max_sane_readahead()
			   FLAGS - combination of bits: RA_ADJCENT_ONLY, RA_ALL_LEVELS, CONTINUE_ON_PRESENT
			*/
			.name = "readahead",
			.type = OPT_FORMAT,
			.u = {
				.f = {
					.format  = "%u:%u",
					.nr_args = 2,
					.arg1 = &sbinfo->ra_params.max,
					.arg2 = &sbinfo->ra_params.flags,
					.arg3 = NULL,
					.arg4 = NULL
				}
			}
		},

#if REISER4_TRACE_TREE
		{
			.name = "trace_file",
			.type = OPT_STRING,
			.u = {
				.string = &trace_file_name
			}
		}
#endif
	};

	sbinfo->txnmgr.atom_max_size = REISER4_ATOM_MAX_SIZE;
	sbinfo->txnmgr.atom_max_age = REISER4_ATOM_MAX_AGE / HZ;

	sbinfo->tree.cbk_cache.nr_slots = CBK_CACHE_SLOTS;

	sbinfo->flush.relocate_threshold = FLUSH_RELOCATE_THRESHOLD;
	sbinfo->flush.relocate_distance = FLUSH_RELOCATE_DISTANCE;
	sbinfo->flush.queue_size = FLUSH_QUEUE_SIZE;
	sbinfo->flush.written_threshold = FLUSH_WRITTEN_THRESHOLD;
	sbinfo->flush.scan_maxnodes = FLUSH_SCAN_MAXNODES;

	if (sbinfo->plug.t == NULL)
		sbinfo->plug.t = tail_plugin_by_id(REISER4_TAIL_PLUGIN);
	if (sbinfo->plug.sd == NULL)
		sbinfo->plug.sd = item_plugin_by_id(REISER4_SD_PLUGIN);
	if (sbinfo->plug.dir_item == NULL)
		sbinfo->plug.dir_item = item_plugin_by_id(REISER4_DIR_ITEM_PLUGIN);
	if (sbinfo->plug.p == NULL)
		sbinfo->plug.p = perm_plugin_by_id(REISER4_PERM_PLUGIN);
	if (sbinfo->plug.f == NULL)
		sbinfo->plug.f = file_plugin_by_id(REISER4_FILE_PLUGIN);
	if (sbinfo->plug.d == NULL)
		sbinfo->plug.d = dir_plugin_by_id(REISER4_DIR_PLUGIN);
	if (sbinfo->plug.h == NULL)
		sbinfo->plug.h = hash_plugin_by_id(REISER4_HASH_PLUGIN);

	sbinfo->optimal_io_size = REISER4_OPTIMAL_IO_SIZE;

	sbinfo->tree.carry.new_node_flags = REISER4_NEW_NODE_FLAGS;
	sbinfo->tree.carry.new_extent_flags = REISER4_NEW_EXTENT_FLAGS;
	sbinfo->tree.carry.paste_flags = REISER4_PASTE_FLAGS;
	sbinfo->tree.carry.insert_flags = REISER4_INSERT_FLAGS;

	sbinfo->entd.timeout = REISER4_ENTD_TIMEOUT;

	trace_file_name = NULL;

	/*
	  init default readahead params
	*/
	sbinfo->ra_params.max = num_physpages / 4;
	sbinfo->ra_params.flags = 0;

	result = parse_options(opt_string, opts, sizeof_array(opts));
	if (result != 0)
		return result;

	if (sbinfo->ra_params.max == -1UL)
		sbinfo->ra_params.max = max_sane_readahead(sbinfo->ra_params.max);

	sbinfo->txnmgr.atom_max_age *= HZ;
	if (sbinfo->txnmgr.atom_max_age <= 0)
		/* overflow */
		sbinfo->txnmgr.atom_max_age = REISER4_ATOM_MAX_AGE;

	/* NOTE add check for sane maximal value. After tuning. */
	if (sbinfo->entd.timeout <= 0)
		sbinfo->entd.timeout = REISER4_ENTD_TIMEOUT;

	/* round optimal io size up to 512 bytes */
	sbinfo->optimal_io_size >>= VFS_BLKSIZE_BITS;
	sbinfo->optimal_io_size <<= VFS_BLKSIZE_BITS;
	if (sbinfo->optimal_io_size == 0) {
		warning("nikita-2497", "optimal_io_size is too small");
		return RETERR(-EINVAL);
	}
#if REISER4_TRACE_TREE
	if (trace_file_name != NULL)
		result = open_trace_file(s, trace_file_name, REISER4_TRACE_BUF_SIZE, &sbinfo->trace_file);
	else
		sbinfo->trace_file.type = log_to_bucket;
#endif

	/* disable single-threaded flush as it leads to deadlock */
	sbinfo->fs_flags |= (1 << REISER4_MTFLUSH);
	return result;
}

static int
reiser4_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct super_block *super;
	reiser4_super_info_data *sbinfo;

	super = mnt->mnt_sb;
	sbinfo = get_super_private(super);

	seq_printf(m, ",trace=0x%x", sbinfo->trace_flags);
	seq_printf(m, ",debug=0x%x", sbinfo->debug_flags);
	seq_printf(m, ",atom_max_size=0x%x", sbinfo->txnmgr.atom_max_size);

	return 0;
}

extern ktxnmgrd_context kdaemon;

/* There are some 'committed' versions of reiser4 super block counters, which
   correspond to reiser4 on-disk state. These counters are initialized here */
static void
init_committed_sb_counters(const struct super_block *s)
{
	reiser4_super_info_data *sbinfo = get_super_private(s);

	sbinfo->blocks_free_committed = sbinfo->blocks_free;
	sbinfo->nr_files_committed = oid_used();
}

DEFINE_SPIN_PROFREGIONS(epoch);
DEFINE_SPIN_PROFREGIONS(jnode);
DEFINE_SPIN_PROFREGIONS(stack);
DEFINE_SPIN_PROFREGIONS(super);
DEFINE_SPIN_PROFREGIONS(atom);
DEFINE_SPIN_PROFREGIONS(txnh);
DEFINE_SPIN_PROFREGIONS(txnmgr);
DEFINE_SPIN_PROFREGIONS(ktxnmgrd);
DEFINE_SPIN_PROFREGIONS(inode_object);
DEFINE_SPIN_PROFREGIONS(fq);
DEFINE_SPIN_PROFREGIONS(cbk_cache);
DEFINE_SPIN_PROFREGIONS(super_eflush);

DEFINE_RW_PROFREGIONS(dk);
DEFINE_RW_PROFREGIONS(tree);

#if 0 && REISER4_LOCKPROF
static void jnode_most_wanted(struct profregion * preg)
{
	print_jnode("most wanted", container_of(preg->obj, jnode, guard.trying));
}

static void jnode_most_held(struct profregion * preg)
{
	print_jnode("most held", container_of(preg->obj, jnode, guard.held));
}
#endif

static int register_profregions(void)
{
#if 0 && REISER4_LOCKPROF
	pregion_spin_jnode_held.champion = jnode_most_held;
	pregion_spin_jnode_trying.champion = jnode_most_wanted;
#endif
	register_super_eflush_profregion();
	register_epoch_profregion();
	register_jnode_profregion();
	register_stack_profregion();
	register_super_profregion();
	register_atom_profregion();
	register_txnh_profregion();
	register_txnmgr_profregion();
	register_ktxnmgrd_profregion();
	register_inode_object_profregion();
	register_fq_profregion();
	register_cbk_cache_profregion();

	register_dk_profregion();
	register_tree_profregion();

	return 0;
}

static void unregister_profregions(void)
{
	unregister_super_eflush_profregion();
	unregister_epoch_profregion();
	unregister_jnode_profregion();
	unregister_stack_profregion();
	unregister_super_profregion();
	unregister_atom_profregion();
	unregister_txnh_profregion();
	unregister_txnmgr_profregion();
	unregister_ktxnmgrd_profregion();
	unregister_inode_object_profregion();
	unregister_fq_profregion();
	unregister_cbk_cache_profregion();

	unregister_dk_profregion();
	unregister_tree_profregion();
}

/* read super block from device and fill remaining fields in @s.
  
   This is read_super() of the past.   */
static int
reiser4_fill_super(struct super_block *s, void *data, int silent UNUSED_ARG)
{
	struct buffer_head *super_bh;
	struct reiser4_master_sb *master_sb;
	reiser4_super_info_data *sbinfo;
	int plugin_id;
	disk_format_plugin *df_plug;
	struct inode *inode;
	int result;
	unsigned long blocksize;
	reiser4_context ctx;

	assert("umka-085", s != NULL);

	if ((REISER4_DEBUG || 
	     REISER4_DEBUG_MODIFY || 
	     REISER4_TRACE ||
	     REISER4_STATS || 
	     REISER4_DEBUG_MEMCPY || 
	     REISER4_ZERO_NEW_NODE || 
	     REISER4_TRACE_TREE || 
	     REISER4_PROF || 
	     REISER4_LOCKPROF) && !silent)
		warning("nikita-2372", "Debugging is on. Benchmarking is invalid.");

	/* this is common for every disk layout. It has a pointer where layout
	   specific part of info can be attached to, though */
	sbinfo = kmalloc(sizeof (reiser4_super_info_data), GFP_KERNEL);

	if (!sbinfo)
		return RETERR(-ENOMEM);

	s->s_fs_info = sbinfo;
	memset(sbinfo, 0, sizeof (*sbinfo));
	ON_DEBUG(INIT_LIST_HEAD(&sbinfo->all_jnodes));

	sema_init(&sbinfo->delete_sema, 1);
	sema_init(&sbinfo->flush_sema, 1);
	s->s_op = &reiser4_super_operations;

	result = init_context(&ctx, s);
	if (result) {
		kfree(sbinfo);
		s->s_fs_info = NULL;
		return result;
	}

	result = reiser4_parse_options(s, data);
	if (result) {
		goto error1;
	}

read_super_block:
#ifdef CONFIG_REISER4_BADBLOCKS
	if ( sbinfo->altsuper )
		super_bh = sb_bread(s, (sector_t) (sbinfo->altsuper >> s->s_blocksize_bits));
	else
#endif
		/* look for reiser4 magic at hardcoded place */
		super_bh = sb_bread(s, (sector_t) (REISER4_MAGIC_OFFSET / s->s_blocksize));

	if (!super_bh) {
		result = RETERR(-EIO);
		goto error1;
	}

	master_sb = (struct reiser4_master_sb *) super_bh->b_data;
	/* check reiser4 magic string */
	result = -EINVAL;
	if (!strncmp(master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4)) {
		/* reset block size if it is not a right one FIXME-VS: better comment is needed */
		blocksize = d16tocpu(&master_sb->blocksize);

		if (blocksize != PAGE_CACHE_SIZE) {
			if (!silent)
				warning("nikita-2609", "%s: wrong block size %ld\n", s->s_id, blocksize);
			brelse(super_bh);
			goto error1;
		}
		if (blocksize != s->s_blocksize) {
			brelse(super_bh);
			if (!sb_set_blocksize(s, (int) blocksize)) {
				goto error1;
			}
			goto read_super_block;
		}

		plugin_id = d16tocpu(&master_sb->disk_plugin_id);
		/* only two plugins are available for now */
		assert("vs-476", (plugin_id == FORMAT40_ID || plugin_id == TEST_FORMAT_ID));
		df_plug = disk_format_plugin_by_id(plugin_id);
		sbinfo->diskmap_block = d64tocpu(&master_sb->diskmap);
		brelse(super_bh);
	} else {
		if (!silent)
			warning("nikita-2608", "Wrong magic: %x != %x",
				*(__u32 *) master_sb->magic, *(__u32 *) REISER4_SUPER_MAGIC_STRING);
		/* no standard reiser4 super block found */
		brelse(super_bh);
		/* FIXME-VS: call guess method for all available layout
		   plugins */
		/* umka (2002.06.12) Is it possible when format-specific super
		   block exists but there no master super block? */
		goto error1;
	}

	spin_super_init(sbinfo);
	spin_super_eflush_init(sbinfo);

	init_tree_0(&sbinfo->tree);

	/* init layout plugin */
	sbinfo->df_plug = df_plug;
	sbinfo->tree.super = s;

	txnmgr_init(&sbinfo->tmgr);

	result = ktxnmgrd_attach(&kdaemon, &sbinfo->tmgr);
	if (result) {
		goto error2;
	}

	init_entd_context(s);

	/* initialize fake inode, formatted nodes will be read/written through
	   it */
	result = init_formatted_fake(s);
	if (result) {
		goto error2;
	}

	/* call disk format plugin method to do all the preparations like
	   journal replay, reiser4_super_info_data initialization, read oid
	   allocator, etc */
	result = df_plug->get_ready(s, data);
	if (result) {
		goto error3;
	}

	init_committed_sb_counters(s);

	assert("nikita-2687", check_block_counters(s));

	/* FIXME. Later on, when plugins will be able to parse options too, here
	   should appear function call to parse plugin's options */

	result = cbk_cache_init(&sbinfo->tree.cbk_cache);
	if (result) {
		goto error4;
	}

	inode = reiser4_iget(s, df_plug->root_dir_key(s));
	if (IS_ERR(inode)) {
		result = PTR_ERR(inode);
		goto error4;
	}
	/* allocate dentry for root inode, It works with inode == 0 */
	s->s_root = d_alloc_root(inode);
	if (!s->s_root) {
		result = RETERR(-ENOMEM);
		goto error4;
	}
	s->s_root->d_op = &reiser4_dentry_operation;

	if (inode->i_state & I_NEW) {
		reiser4_inode *info;

		info = reiser4_inode_data(inode);

		grab_plugin_from(info, file, default_file_plugin(s));
		grab_plugin_from(info, dir, default_dir_plugin(s));
		grab_plugin_from(info, sd, default_sd_plugin(s));
		grab_plugin_from(info, hash, default_hash_plugin(s));
		grab_plugin_from(info, tail, default_tail_plugin(s));
		grab_plugin_from(info, perm, default_perm_plugin(s));
		grab_plugin_from(info, dir_item, default_dir_item_plugin(s));

		assert("nikita-1951", info->pset->file != NULL);
		assert("nikita-1814", info->pset->dir != NULL);
		assert("nikita-1815", info->pset->sd != NULL);
		assert("nikita-1816", info->pset->hash != NULL);
		assert("nikita-1817", info->pset->tail != NULL);
		assert("nikita-1818", info->pset->perm != NULL);
		assert("vs-545", info->pset->dir_item != NULL);

		unlock_new_inode(inode);
	}

	reiser4_sysfs_init(s);

	if (!silent)
		print_fs_info("mount ok", s);
	reiser4_exit_context(&ctx);
	return 0;

error4:
	get_super_private(s)->df_plug->release(s);
error3:
	done_formatted_fake(s);
	/* shutdown daemon */
	ktxnmgrd_detach(&sbinfo->tmgr);
error2:
	txnmgr_done(&sbinfo->tmgr);
error1:
	kfree(sbinfo);
	s->s_fs_info = NULL;

	ctx.trans = NULL;
	done_context(&ctx);
	return result;
}

static void
reiser4_kill_super(struct super_block *s)
{
	reiser4_super_info_data *sbinfo;
	reiser4_context context;

	sbinfo = (reiser4_super_info_data *) s->s_fs_info;
	if (!sbinfo) {
		/* mount failed */
		s->s_op = 0;
		kill_block_super(s);
		return;
	}

	if (init_context(&context, s)) {
		warning("nikita-2728", "Cannot initialize context.");
		return;
	}
	trace_on(TRACE_VFS_OPS, "kill_super\n");

	reiser4_sysfs_done(s);

	/* FIXME-VS: the problem is that there still might be dirty pages which
	   became dirty via mapping. Have them to go through reiser4_writepages */
	fsync_super(s);

	/* FIXME: complete removal of directories which were not deleted when they were supposed to be because their
	   dentries had negative child dentries */
	shrink_dcache_parent(s->s_root);
	
	if (reiser4_is_debugged(s, REISER4_VERBOSE_UMOUNT))
		get_current_context()->trace_flags |= (TRACE_PCACHE |
						       TRACE_TXN    |
						       TRACE_FLUSH  |
						       TRACE_ZNODES | 
						       TRACE_IO_R   | 
						       TRACE_IO_W);
	/* flushes transactions, etc. */
	if (get_super_private(s)->df_plug->release(s) != 0)
		goto out;

	done_entd_context(s);

	/* shutdown daemon if last mount is removed */
	ktxnmgrd_detach(&sbinfo->tmgr);

	check_block_counters(s);
	done_formatted_fake(s);

	/* FIXME: done_formatted_fake just has finished with last jnodes (bitmap ones) */
	done_tree(&sbinfo->tree);

	close_trace_file(&sbinfo->trace_file);

	/* we don't want ->write_super to be called any more. */
	s->s_op->write_super = NULL;
	kill_block_super(s);

#if REISER4_DEBUG
	{
		struct list_head *scan;

		/* print jnodes that survived umount. */
		list_for_each(scan, &sbinfo->all_jnodes) {
			jnode *busy;

			busy = list_entry(scan, jnode, jnodes);
			info_jnode("\nafter umount", busy);
		}
	}
	if (sbinfo->kmalloc_allocated > 0)
		warning("nikita-2622", "%i bytes still allocated", sbinfo->kmalloc_allocated);
#endif

	if (reiser4_is_debugged(s, REISER4_STATS_ON_UMOUNT))
		reiser4_print_stats();

out:
	/* no assertions below this line */
	(void)reiser4_exit_context(&context);

	phash_super_destroy(s);

	kfree(sbinfo);
	s->s_fs_info = NULL;
}

static void
reiser4_write_super(struct super_block *s)
{
	int ret;
	reiser4_context ctx;

	init_context(&ctx, s);
	reiser4_stat_inc(vfs_calls.write_super);
	ret = txnmgr_force_commit_all(s);
	if (ret != 0)
		warning("jmacd-77113", 
			"txn_force failed in write_super: %d", ret);

	/* Oleg says do this: */
	s->s_dirt = 0;

	(void)reiser4_exit_context(&ctx);
}

/* ->get_sb() method of file_system operations. */
static struct super_block *
reiser4_get_sb(struct file_system_type *fs_type	/* file
						 * system
						 * type */ ,
	       int flags /* flags */ ,
	       char *dev_name /* device name */ ,
	       void *data /* mount options */ )
{
	return get_sb_bdev(fs_type, flags, dev_name, data, reiser4_fill_super);
}

typedef enum {
	INIT_NONE,
	INIT_INODECACHE,
	INIT_CONTEXT_MGR,
	INIT_ZNODES,
	INIT_PLUGINS,
	INIT_PHASH,
	INIT_PLUGIN_SET,
	INIT_TXN,
	INIT_FAKES,
	INIT_JNODES,
	INIT_EFLUSH,
	INIT_SCINT,
	INIT_SPINPROF,
	INIT_FS_REGISTERED
} reiser4_init_stage;

static reiser4_init_stage init_stage;

/* finish with reiser4: this is called either at shutdown or at module unload. */
static void 
shutdown_reiser4(void)
{
#define DONE_IF( stage, exp )			\
	if( init_stage == ( stage ) ) {		\
		exp;				\
		-- init_stage;			\
	}

	DONE_IF(INIT_FS_REGISTERED, unregister_filesystem(&reiser4_fs_type));
	DONE_IF(INIT_SPINPROF, unregister_profregions());
	DONE_IF(INIT_SCINT, scint_done_once());
	DONE_IF(INIT_EFLUSH, eflush_done());
	DONE_IF(INIT_JNODES, jnode_done_static());
	DONE_IF(INIT_FAKES,;);
	DONE_IF(INIT_TXN, txnmgr_done_static());
	DONE_IF(INIT_PLUGIN_SET,plugin_set_done());
	DONE_IF(INIT_PHASH,phash_done());
	DONE_IF(INIT_PLUGINS,;);
	DONE_IF(INIT_ZNODES, znodes_done());
	DONE_IF(INIT_CONTEXT_MGR,;);
	DONE_IF(INIT_INODECACHE, destroy_inodecache());
	assert("nikita-2516", init_stage == INIT_NONE);

#undef DONE_IF
}

/* initialise reiser4: this is called either at bootup or at module load. */
static int __init
init_reiser4(void)
{
#define CHECK_INIT_RESULT( exp )		\
({						\
	result = exp;				\
	if( result == 0 )			\
		++ init_stage;			\
	else {					\
		shutdown_reiser4();		\
		return result;			\
	}					\
})

	int result;

	printk(KERN_INFO 
	       "Loading Reiser4. " 
	       "See www.namesys.com for a description of Reiser4.\n");
	init_stage = INIT_NONE;

	CHECK_INIT_RESULT(init_inodecache());
	CHECK_INIT_RESULT(init_context_mgr());
	CHECK_INIT_RESULT(znodes_init());
	CHECK_INIT_RESULT(init_plugins());
	CHECK_INIT_RESULT(phash_init());
	CHECK_INIT_RESULT(plugin_set_init());
	CHECK_INIT_RESULT(txnmgr_init_static());
	CHECK_INIT_RESULT(init_fakes());
	CHECK_INIT_RESULT(jnode_init_static());
	CHECK_INIT_RESULT(eflush_init());
	CHECK_INIT_RESULT(scint_init_once());
	CHECK_INIT_RESULT(register_profregions());
	CHECK_INIT_RESULT(register_filesystem(&reiser4_fs_type));

	calibrate_prof();

	assert("nikita-2515", init_stage == INIT_FS_REGISTERED);
	return 0;
#undef CHECK_INIT_RESULT
}

static void __exit
done_reiser4(void)
{
	shutdown_reiser4();
}

module_init(init_reiser4);
module_exit(done_reiser4);

MODULE_DESCRIPTION("Reiser4 filesystem");
MODULE_AUTHOR("Hans Reiser <Reiser@Namesys.COM>");

MODULE_LICENSE("GPL");

/* description of the reiser4 file system type in the VFS eyes. */
static struct file_system_type reiser4_fs_type = {
	.owner = THIS_MODULE,
	.name = "reiser4",
#if REISER4_USE_SYSFS
	.subsys = {
		.kset = {
			.ktype = &ktype_reiser4
		}
	},
#endif
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = reiser4_get_sb,
	.kill_sb = reiser4_kill_super,

	/* NOTE-NIKITA something more? */
	.next = NULL
};

struct super_operations reiser4_super_operations = {
	.alloc_inode = reiser4_alloc_inode,	/* d */
	.destroy_inode = reiser4_destroy_inode,	/* d */
	.read_inode = noop_read_inode,	/* d */
	.dirty_inode = NULL, /*reiser4_dirty_inode,*/	/* d */
 	.write_inode        = NULL, /* d */
 	.put_inode          = NULL, /* d */
	.drop_inode = reiser4_drop_inode,	/* d */
	.delete_inode = reiser4_delete_inode,	/* d */
	.put_super = NULL /* d */ ,
	.write_super = reiser4_write_super,
/* 	.write_super_lockfs = reiser4_write_super_lockfs, */
/* 	.unlockfs           = reiser4_unlockfs, */
	.statfs = reiser4_statfs,	/* d */
/* 	.remount_fs         = reiser4_remount_fs, */
/* 	.clear_inode        = reiser4_clear_inode, */
/* 	.umount_begin       = reiser4_umount_begin,*/
/* 	.fh_to_dentry       = reiser4_fh_to_dentry, */
/* 	.dentry_to_fh       = reiser4_dentry_to_fh */
	.show_options = reiser4_show_options	/* d */
};

struct dentry_operations reiser4_dentry_operation = {
	.d_revalidate = NULL,
	.d_hash = NULL,
	.d_compare = NULL,
	.d_delete = NULL,
	.d_release = reiser4_d_release,
	.d_iput = NULL,
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
