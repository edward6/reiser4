/* Copyright 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Safe-links. */

#include "safe_link.h"
#include "debug.h"
#include "inode.h"

#include "plugin/item/blackbox.h"

#include <linux/fs.h>

typedef struct safelink {
	reiser4_key sdkey;
	d64 size;
} safelink_t;

static oid_t
safe_link_locality(reiser4_tree *tree)
{
	return get_inode_oid(tree->super->s_root->d_inode) + 1;
}

static reiser4_key *
build_link_key(struct inode *inode, reiser4_safe_link_t link, reiser4_key *key)
{
	key_init(key);
	set_key_locality(key, safe_link_locality(tree_by_inode(inode)));
	set_key_objectid(key, get_inode_oid(inode));
	set_key_offset(key, link);
	return key;
}

reiser4_internal __u64 safe_link_tograb(reiser4_tree *tree)
{
	return
		/* insert safe link */
		estimate_one_insert_item(tree) +
		/* remove safe link */
		estimate_one_item_removal(tree) +
		/* drill to the leaf level during insertion */
		1 + estimate_one_insert_item(tree) +
		/*
		 * possible update of existing safe-link. Actually, if
		 * safe-link existed already (we failed to remove it), then no
		 * insertion is necessary, so this term is already "covered",
		 * but for simplicity let's left it.
		 */
		1;
}

reiser4_internal int safe_link_grab(reiser4_tree *tree, reiser4_ba_flags_t flags)
{
	int   result;

	grab_space_enable();
	/* The sbinfo->delete semaphore can be taken here.
	 * safe_link_release() should be called before leaving reiser4
	 * context. */
	result = reiser4_grab_reserved(tree->super, safe_link_tograb(tree), flags);
	grab_space_enable();
	return result;
}

reiser4_internal void safe_link_release(reiser4_tree * tree)
{
	reiser4_release_reserved(tree->super);
}

reiser4_internal int safe_link_add(struct inode *inode, reiser4_safe_link_t link)
{
	reiser4_key key;
	safelink_t sl;
	int length;
	int result;
	reiser4_tree *tree;

	build_sd_key(inode, &sl.sdkey);
	length = sizeof sl.sdkey;

	if (link == SAFE_TRUNCATE) {
		length += sizeof(sl.size);
		cputod64(inode->i_size, &sl.size);
	}
	tree = tree_by_inode(inode);
	build_link_key(inode, link, &key);

	result = store_black_box(tree, &key, &sl, length);
	if (result == -EEXIST)
		result = update_black_box(tree, &key, &sl, length);
	return result;
}

reiser4_internal int safe_link_del(struct inode *inode, reiser4_safe_link_t link)
{
	reiser4_key key;

	return kill_black_box(tree_by_inode(inode),
			      build_link_key(inode, link, &key));
}

typedef struct {
	reiser4_tree       *tree;
	reiser4_key         key;
	reiser4_key         sdkey;
	reiser4_safe_link_t link;
	oid_t               oid;
	__u64               size;
} safe_link_context;

static void safe_link_iter_begin(reiser4_tree *tree, safe_link_context *ctx)
{
	ctx->tree = tree;
	key_init(&ctx->key);
	set_key_locality(&ctx->key, safe_link_locality(tree));
	set_key_objectid(&ctx->key, get_key_objectid(max_key()));
	set_key_offset(&ctx->key, get_key_offset(max_key()));
}

static int safe_link_iter_next(safe_link_context *ctx)
{
	int result;
	safelink_t sl;

	result = load_black_box(ctx->tree,
				&ctx->key, &sl, sizeof sl, 0);
	if (result == 0) {
		ctx->oid = get_key_objectid(&ctx->key);
		ctx->link = get_key_offset(&ctx->key);
		ctx->sdkey = sl.sdkey;
		if (ctx->link == SAFE_TRUNCATE)
			ctx->size = d64tocpu(&sl.size);
	}
	return result;
}

static int safe_link_iter_finished(safe_link_context *ctx)
{
	return get_key_locality(&ctx->key) != safe_link_locality(ctx->tree);
}


static void safe_link_iter_end(safe_link_context *ctx)
{
	/* nothing special */
}

static int process_safelink(struct super_block *super, reiser4_safe_link_t link,
			    reiser4_key *sdkey, oid_t oid, __u64 size)
{
	struct inode *inode;
	int result;

	inode = reiser4_iget(super, sdkey, 1);
	if (!IS_ERR(inode)) {
		file_plugin *fplug;

		fplug = inode_file_plugin(inode);
		assert("nikita-3428", fplug != NULL);
		if (fplug->safelink != NULL)
			result = fplug->safelink(inode, link, size);
		else {
			warning("nikita-3430",
				"Cannot handle safelink for %lli", oid);
			print_key("key", sdkey);
			print_inode("inode", inode);
			result = 0;
		}
		if (result != 0) {
			warning("nikita-3431",
				"Error processing safelink for %lli: %i",
				oid, result);
		}
		reiser4_iget_complete(inode);
		iput(inode);
		if (result == 0) {
			result = safe_link_grab(tree_by_inode(inode),
						BA_CAN_COMMIT);
			if (result == 0)
				result = safe_link_del(inode, link);
			safe_link_release(tree_by_inode(inode));
			if (result == 0)
				txn_restart_current();
		}
	} else
		result = PTR_ERR(inode);
	return result;
}

reiser4_internal int process_safelinks(struct super_block *super)
{
	safe_link_context ctx;
	int result;
	
	if (rofs_super(super))
		return 0;
	safe_link_iter_begin(&get_super_private(super)->tree, &ctx);
	result = 0;
	do {
		result = safe_link_iter_next(&ctx);
		if (safe_link_iter_finished(&ctx) || result == -ENOENT) {
			result = 0;
			break;
		}
		if (result == 0)
			result = process_safelink(super, ctx.link,
						  &ctx.sdkey, ctx.oid, ctx.size);
	} while (result == 0);
	safe_link_iter_end(&ctx);
	return result;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
