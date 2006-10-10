/* Copyright 2001, 2002, 2003 by Hans Reiser,
   licensing governed by reiser4/README */

/* This file contains hooks that converts (*) cryptcompress files to unix-files,
   and a set of protected (**) methods of a cryptcompress file plugin to perform
   such conversion.

(*)
   The conversion is performed for incompressible files to reduce cpu and memory
   usage. If first logical cluster (64K by default) of a file is incompressible,
   then we make a desicion, that the whole file is incompressible.
   The conversion can be enabled via installing a special compression mode
   plugin (CONVX_COMPRESSION_MODE_ID, see plugin/compress/compress_mode.c for
   details).

(**)
   The protection means serialization of critical sections (readers and writers
   of @pset->file)
*/

#include "../../inode.h"
#include "../cluster.h"
#include "file.h"

#define conversion_enabled(inode)                                      \
	 (inode_compression_mode_plugin(inode) ==		       \
	  compression_mode_plugin_by_id(CONVX_COMPRESSION_MODE_ID))


/* Located sections (readers and writers of @pset->file) are not
   permanently critical: cryptcompress file can be converted only
   if the conversion is enabled (see the macrio above). And we don't
   convert unix files at all.
   The following helper macro is a sanity check to decide if we
   need to protect a located section.
*/
#define should_protect(inode)						\
	(inode_file_plugin(inode) ==					\
	 file_plugin_by_id(CRYPTCOMPRESS_FILE_PLUGIN_ID) &&		\
	 conversion_enabled(inode))

/* All protected methods have prefix "prot" in their names.
   It is convenient to construct them by usual (unprotected) ones
   using the following common macros:
*/

/* Macro for passive protection.
   method_cryptcompress contains only readers */
#define PROT_PASSIVE(type, method, args)				\
({							                \
	type _result;							\
	struct rw_semaphore * guard =					\
		&reiser4_inode_data(inode)->conv_sem;			\
									\
	if (should_protect(inode)) {					\
		down_read(guard);					\
		if (!should_protect(inode))				\
			up_read(guard);					\
	}								\
	if (inode_file_plugin(inode) ==					\
	    file_plugin_by_id(UNIX_FILE_PLUGIN_ID))			\
		_result = method ## _unix_file args;			\
	else								\
		_result = method ## _cryptcompress args;		\
	if (should_protect(inode))					\
		up_read(guard);						\
	_result;							\
})

#define PROT_PASSIVE_VOID(method, args)					\
({							                \
	struct rw_semaphore * guard =					\
		&reiser4_inode_data(inode)->conv_sem;			\
									\
	if (should_protect(inode)) {					\
		down_read(guard);					\
		if (!should_protect(inode))				\
			up_read(guard);					\
	}								\
	if (inode_file_plugin(inode) ==					\
	    file_plugin_by_id(UNIX_FILE_PLUGIN_ID))			\
		method ## _unix_file args;				\
	else								\
		method ## _cryptcompress args;				\
	if (should_protect(inode))					\
		up_read(guard);						\
})

/* Macro for active protection.
   active_expr contains readers and writers; after its
   evaluation conversion should be disabled */
#define PROT_ACTIVE(type, method, args, active_expr)			\
({	                 						\
	type _result = 0;						\
	struct rw_semaphore * guard =					\
		&reiser4_inode_data(inode)->conv_sem;			\
	reiser4_context * ctx =	reiser4_init_context(inode->i_sb);	\
	if (IS_ERR(ctx))						\
		return PTR_ERR(ctx);					\
									\
	if (should_protect(inode)) {					\
		down_write(guard);					\
		if (should_protect(inode))				\
			_result = active_expr;				\
		up_write(guard);					\
	}								\
	if (_result == 0) {						\
		if (inode_file_plugin(inode) ==				\
		    file_plugin_by_id(UNIX_FILE_PLUGIN_ID))		\
			_result =  method ## _unix_file args;		\
		else							\
			_result =  method ## _cryptcompress args;	\
	}								\
	reiser4_exit_context(ctx);					\
	_result;							\
})

/* Pass management to the unix file plugin */
static int __cryptcompress2unixfile(struct file *file, struct inode * inode)
{
	int result;
	reiser4_inode *info;
	unix_file_info_t * uf;
	info = reiser4_inode_data(inode);

	result = aset_set_unsafe(&info->pset, PSET_FILE, (reiser4_plugin *)
				 file_plugin_by_id(UNIX_FILE_PLUGIN_ID));
	if (result)
		return result;
	/* get rid of non-standard plugins */
	info->plugin_mask &= ~cryptcompress_mask;
	/* get rid of plugin stat-data extension */
	info->extmask &= ~(1 << PLUGIN_STAT);

	reiser4_inode_clr_flag(inode, REISER4_SDLEN_KNOWN);

	/* FIXME use init_inode_data_unix_file() instead,
	   but aviod init_inode_ordering() */
	/* Init unix-file specific part of inode */
	uf = unix_file_inode_data(inode);
	uf->container = UF_CONTAINER_UNKNOWN;
	init_rwsem(&uf->latch);
	uf->tplug = inode_formatting_plugin(inode);
	uf->exclusive_use = 0;
#if REISER4_DEBUG
	uf->ea_owner = NULL;
	atomic_set(&uf->nr_neas, 0);
#endif
	inode->i_op =
		&file_plugin_by_id(UNIX_FILE_PLUGIN_ID)->inode_ops;
	inode->i_fop =
		&file_plugin_by_id(UNIX_FILE_PLUGIN_ID)->file_ops;
	inode->i_mapping->a_ops =
		&file_plugin_by_id(UNIX_FILE_PLUGIN_ID)->as_ops;
	file->f_op = inode->i_fop;
	return 0;
}

#if REISER4_DEBUG
static int disabled_conversion_inode_ok(struct inode * inode)
{
	__u64 extmask = reiser4_inode_data(inode)->extmask;
	__u16 plugin_mask = reiser4_inode_data(inode)->plugin_mask;

	return ((extmask & (1 << LIGHT_WEIGHT_STAT)) &&
		(extmask & (1 << UNIX_STAT)) &&
		(extmask & (1 << LARGE_TIMES_STAT)) &&
		(extmask & (1 << PLUGIN_STAT)) &&
		(plugin_mask & (1 << PSET_COMPRESSION_MODE)));
}
#endif

/* Assign another mode that will control
   compression at flush time only */
static int disable_conversion_no_update_sd(struct inode * inode)
{
	int result;
	result =
	       force_plugin_pset(inode,
				 PSET_COMPRESSION_MODE,
				 (reiser4_plugin *)compression_mode_plugin_by_id
				 (COL_8_COMPRESSION_MODE_ID));
	assert("edward-1500",
	       ergo(!result, disabled_conversion_inode_ok(inode)));
	return result;
}

/* Disable future attempts to check/convert. This function is called by
   conversion hooks. */
static int disable_conversion(struct inode * inode)
{
	return disable_conversion_no_update_sd(inode);
}

static int check_position(struct inode * inode,
			  loff_t pos /* initial position in the file */,
			  reiser4_cluster_t * clust,
			  int * check_compress)
{
	assert("edward-1505", conversion_enabled(inode));
	assert("edward-1506", inode->i_size <= inode_cluster_size(inode));
	/* if file size is more then cluster size, then compressible
	   status must be figured out (i.e. compression was disabled,
	   or file plugin was converted to unix_file) */

	if (pos > inode->i_size)
		/* first logical cluster will contain a (partial) hole */
		return disable_conversion(inode);
	if (inode->i_size == inode_cluster_size(inode))
		*check_compress = 1;
	return 0;
}

static void start_check_compressibility(struct inode * inode,
					reiser4_cluster_t * clust,
					hint_t * hint)
{
	assert("edward-1507", clust->index == 1);
	assert("edward-1508", !tfm_cluster_is_uptodate(&clust->tc));
	assert("edward-1509", cluster_get_tfm_act(&clust->tc) == TFM_READ_ACT);

	hint_init_zero(hint);
	clust->hint = hint;
	clust->index --;
	clust->nr_pages = count_to_nrpages(fsize_to_count(clust, inode));

	/* first logical cluster (of index #0) must be complete */
	assert("edward-1510", fsize_to_count(clust, inode) ==
	       inode_cluster_size(inode));
}

static void finish_check_compressibility(struct inode * inode,
					 reiser4_cluster_t * clust,
					 hint_t * hint)
{
	reiser4_unset_hint(clust->hint);
	clust->hint = hint;
	clust->index ++;
}

#if REISER4_DEBUG
int prepped_dclust_ok(hint_t * hint)
{
	reiser4_key key;
	coord_t * coord = &hint->ext_coord.coord;

	item_key_by_coord(coord, &key);
	return (item_id_by_coord(coord) == CTAIL_ID &&
		!coord_is_unprepped_ctail(coord) &&
		(get_key_offset(&key) + nr_units_ctail(coord) ==
		 dclust_get_extension_dsize(hint)));
}
#endif

#define thirty_persent(size) ((307 * size) >> 10)
/* evaluation of data compressibility */
#define data_is_compressible(osize, isize)		\
	(osize < (isize - thirty_persent(isize)))

/* This is called only once per file life.
   Read first logical cluster (of index #0) and estimate its compressibility.
   Save estimation result in @compressible */
static int read_check_compressibility(struct inode * inode,
				      reiser4_cluster_t * clust,
				      int * compressible)
{
	int i;
	int result;
	__u32 dst_len;
	hint_t tmp_hint;
	hint_t * cur_hint = clust->hint;

	start_check_compressibility(inode, clust, &tmp_hint);

	result = grab_cluster_pages(inode, clust);
	if (result)
		return result;
	/* Read page cluster here */
	for (i = 0; i < clust->nr_pages; i++) {
		struct page *page = clust->pages[i];
		lock_page(page);
		result = do_readpage_ctail(inode, clust, page,
					   ZNODE_READ_LOCK);
		unlock_page(page);
		if (result)
			goto error;
	}
	tfm_cluster_clr_uptodate(&clust->tc);

	cluster_set_tfm_act(&clust->tc, TFM_WRITE_ACT);

	if (hint_is_valid(&tmp_hint) && !hint_is_unprepped_dclust(&tmp_hint)) {
		/* lenght of compressed data is known, no need to compress */
		assert("edward-1511",
		       znode_is_write_locked(tmp_hint.ext_coord.coord.node));
		assert("edward-1512",
		       WITH_DATA(tmp_hint.ext_coord.coord.node,
				 prepped_dclust_ok(&tmp_hint)));
		dst_len = dclust_get_extension_dsize(&tmp_hint);
	}
	else {
		tfm_cluster_t * tc = &clust->tc;
		compression_plugin * cplug = inode_compression_plugin(inode);
		result = grab_tfm_stream(inode, tc, INPUT_STREAM);
		if (result)
			goto error;
		for (i = 0; i < clust->nr_pages; i++) {
			char *data;
			lock_page(clust->pages[i]);
			BUG_ON(!PageUptodate(clust->pages[i]));
			data = kmap(clust->pages[i]);
			memcpy(tfm_stream_data(tc, INPUT_STREAM) + pg_to_off(i),
			       data, PAGE_CACHE_SIZE);
			kunmap(clust->pages[i]);
			unlock_page(clust->pages[i]);
		}
		result = grab_tfm_stream(inode, tc, OUTPUT_STREAM);
		if (result)
			goto error;
		result = grab_coa(tc, cplug);
		if (result)
			goto error;
		tc->len = tc->lsize = fsize_to_count(clust, inode);
		assert("edward-1513", tc->len == inode_cluster_size(inode));
		dst_len = tfm_stream_size(tc, OUTPUT_STREAM);
		cplug->compress(get_coa(tc, cplug->h.id, tc->act),
				tfm_input_data(clust), tc->len,
				tfm_output_data(clust), &dst_len);
		assert("edward-1514",
		       dst_len <= tfm_stream_size(tc, OUTPUT_STREAM));
	}
	finish_check_compressibility(inode, clust, cur_hint);
	*compressible = data_is_compressible(dst_len,
					     inode_cluster_size(inode));
	return 0;
 error:
	reiser4_release_cluster_pages(clust);
	return result;
}

/* Cut disk cluster of index @idx */
static int cut_disk_cluster(struct inode * inode, cloff_t idx)
{
	reiser4_key from, to;
	assert("edward-1515", inode_file_plugin(inode) ==
	       file_plugin_by_id(CRYPTCOMPRESS_FILE_PLUGIN_ID));
	key_by_inode_cryptcompress(inode, clust_to_off(idx, inode), &from);
	to = from;
	set_key_offset(&to,
		       get_key_offset(&from) + inode_cluster_size(inode) - 1);
	return reiser4_cut_tree(reiser4_tree_by_inode(inode),
				&from, &to, inode, 0);
}

static int reserve_cryptcompress2unixfile(struct inode *inode)
{
	reiser4_block_nr unformatted_nodes;
	reiser4_tree *tree;

	tree = reiser4_tree_by_inode(inode);

	/* number of unformatted nodes which will be created */
	unformatted_nodes = cluster_nrpages(inode); /* N */

	/*
	 * space required for one iteration of extent->tail conversion:
	 *
	 *     1. kill ctail items
	 *
	 *     2. insert N unformatted nodes
	 *
	 *     3. insert N (worst-case single-block
	 *     extents) extent units.
	 *
	 *     4. drilling to the leaf level by coord_by_key()
	 *
	 *     5. possible update of stat-data
	 *
	 */
	grab_space_enable();
	return reiser4_grab_space
		(2 * tree->height +
		 unformatted_nodes  +
		 unformatted_nodes * estimate_one_insert_into_item(tree) +
		 1 + estimate_one_insert_item(tree) +
		 inode_file_plugin(inode)->estimate.update(inode),
		 BA_CAN_COMMIT);
}

/* clear flag that indicated conversion and update
   stat-data with new (unix-file - specific) info */
static int complete_file_conversion(struct inode *inode)
{
	int result;

	grab_space_enable();
	result =
	    reiser4_grab_space(inode_file_plugin(inode)->estimate.update(inode),
			       BA_CAN_COMMIT);
	if (result == 0) {
		reiser4_inode_clr_flag(inode, REISER4_FILE_CONV_IN_PROGRESS);
		result = reiser4_update_sd(inode);
	}
	if (result)
		warning("edward-1452",
			"Converting %llu to unix-file: update sd failed (%i)",
			(unsigned long long)get_inode_oid(inode), result);
	return 0;
}


/* do conversion */
int cryptcompress2unixfile(struct file *file, struct inode * inode,
			   reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	cryptcompress_info_t *cr_info;
	unix_file_info_t *uf_info;

	assert("edward-1516", clust->pages[0]->index == 0);
	assert("edward-1517", clust->hint != NULL);

	/* release all cryptcompress-specific recources */
	cr_info = cryptcompress_inode_data(inode);
	result = reserve_cryptcompress2unixfile(inode);
	if (result)
		goto out;
	reiser4_inode_set_flag(inode, REISER4_FILE_CONV_IN_PROGRESS);
	reiser4_unset_hint(clust->hint);
	result = cut_disk_cluster(inode, 0);
	if (result)
		goto out;
	/* captured jnode of cluster and assotiated resources (pages,
	   reserved disk space) were released by ->kill_hook() method
	   of the item plugin */

	up_write(&cr_info->lock);
	result = __cryptcompress2unixfile(file, inode);
	if (result)
		goto out;
	/* At this point file is managed by unix file plugin */

	uf_info = unix_file_inode_data(inode);
	//	get_exclusive_access(uf_info);

	assert("edward-1518",
	       ergo(jprivate(clust->pages[0]),
		    !jnode_is_cluster_page(jprivate(clust->pages[0]))));
	for(i = 0; i < clust->nr_pages; i++) {
		assert("edward-1519", clust->pages[i]);
		assert("edward-1520", PageUptodate(clust->pages[i]));

		result = find_or_create_extent(clust->pages[i]);
		if (result)
			break;
	}
	if (!result) {
		uf_info->container = UF_CONTAINER_EXTENTS;
		complete_file_conversion(inode);
	}
	//	drop_exclusive_access(uf_info);
 out:
	all_grabbed2free();
	if (result)
		warning("edward-1453", "Failed to convert file %llu: %i",
			(unsigned long long)get_inode_oid(inode), result);
	return result;
}

/* Check, then perform or disable conversion if needed */
int write_conversion_hook(struct file *file, struct inode * inode, loff_t pos,
			  reiser4_cluster_t * clust, int * progress)
{
	int result;
	int check_compress = 0;
	int compressible = 0;

	if (!conversion_enabled(inode))
		return 0;
	result = check_position(inode, pos, clust, &check_compress);
	if (result || !check_compress)
		return result;
	result = read_check_compressibility(inode, clust, &compressible);
	if (result)
		return result;

	/* At this point page cluster is grabbed and uptodate */
	if (!compressible) {
		result = cryptcompress2unixfile(file, inode, clust);
		if (result == 0)
			*progress = 1;
	}
	else
		result = disable_conversion(inode);

	reiser4_release_cluster_pages(clust);
	return result;
}

static int setattr_conversion_hook(struct inode * inode, struct iattr *attr)
{
	return (attr->ia_valid & ATTR_SIZE ? disable_conversion(inode) : 0);
}

/* Protected methods of cryptcompress file plugin constructed
   by the macros above */

/* Wrappers with active protection for:
   . write_cryptcompress;
   . setattr_cryptcompress;
*/

ssize_t prot_write_cryptcompress(struct file *file, const char __user *buf,
				 size_t count, loff_t *off)
{
	int prot = 0;
	int conv = 0;
	ssize_t written_cr = 0;
	ssize_t written_uf = 0;
	struct inode * inode = file->f_dentry->d_inode;
	struct rw_semaphore * guard = &reiser4_inode_data(inode)->conv_sem;

	if (should_protect(inode)) {
		prot = 1;
		down_write(guard);
	}
	written_cr = write_cryptcompress(file, buf, count, off, &conv);
	if (prot)
		up_write(guard);
	if (written_cr < 0)
		return written_cr;
	if (conv)
		written_uf = write_unix_file(file, buf + written_cr,
					     count - written_cr, off);
	return written_cr + (written_uf < 0 ? 0 : written_uf);
}

int prot_setattr_cryptcompress(struct dentry *dentry, struct iattr *attr)
{
	struct inode * inode = dentry->d_inode;
	return PROT_ACTIVE(int, setattr, (dentry, attr),
			   setattr_conversion_hook(inode, attr));
}

/* Wrappers with passive protection for:
   . read_cryptcomperess;
   . mmap_cryptcompress;
   . release_cryptcompress;
   . sendfile_cryptcompress;
   . delete_object_cryptcompress.
*/
ssize_t prot_read_cryptcompress(struct file * file, char __user * buf,
				size_t size, loff_t * off)
{
	struct inode * inode = file->f_dentry->d_inode;
	return PROT_PASSIVE(ssize_t, read, (file, buf, size, off));
}

int prot_mmap_cryptcompress(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_dentry->d_inode;
	return PROT_PASSIVE(int, mmap, (file, vma));
}

int prot_release_cryptcompress(struct inode *inode, struct file *file)
{
	return PROT_PASSIVE(int, release, (inode, file));
}

ssize_t prot_sendfile_cryptcompress(struct file *file, loff_t *ppos,
				    size_t count, read_actor_t actor,
				    void *target)
{
	struct inode * inode = file->f_dentry->d_inode;
	return PROT_PASSIVE(ssize_t, sendfile,
			    (file, ppos, count, actor, target));
}

int prot_delete_object_cryptcompress(struct inode *inode)
{
	return PROT_PASSIVE(int, delete_object, (inode));
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
