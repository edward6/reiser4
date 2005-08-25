/* Copyright 2005 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "inode.h"
#include "plugin/plugin.h"


/*
 * Supported file-handle types
 */
typedef enum {
	FH_WITH_PARENT = 0x10,	/* file handle with parent */
	FH_WITHOUT_PARENT = 0x11	/* file handle without parent */
} reiser4_fhtype;

#define NFSERROR (255)

/* initialize place-holder for object */
static void object_on_wire_init(reiser4_object_on_wire *o)
{
	o->plugin = NULL;
}

/* finish with @o */
static void object_on_wire_done(reiser4_object_on_wire *o)
{
	if (o->plugin != NULL)
		o->plugin->wire.done(o);
}

/*
 * read serialized object identity from @addr and store information about
 * object in @obj. This is dual to encode_inode().
 */
static char *decode_inode(struct super_block *s, char *addr,
			  reiser4_object_on_wire * obj)
{
	file_plugin *fplug;

	/* identifier of object plugin is stored in the first two bytes,
	 * followed by... */
	fplug = file_plugin_by_disk_id(get_tree(s), (d16 *) addr);
	if (fplug != NULL) {
		addr += sizeof(d16);
		obj->plugin = fplug;
		assert("nikita-3520", fplug->wire.read != NULL);
		/* plugin specific encoding of object identity. */
		addr = fplug->wire.read(addr, obj);
	} else
		addr = ERR_PTR(RETERR(-EINVAL));
	return addr;
}

/**
 * reiser4_decode_fh - decode_fh of export operations
 * @super: super block
 * @fh: nfsd file handle
 * @len: length of file handle
 * @fhtype: type of file handle
 * @acceptable: acceptability testing function
 * @context: argument for @acceptable
 *
 * Returns dentry referring to the same file as @fh.
 */
static struct dentry *reiser4_decode_fh(struct super_block *super, __u32 *fh,
					int len, int fhtype,
					int (*acceptable) (void *context,
							   struct dentry *de),
					void *context)
{
	reiser4_context *ctx;
	reiser4_object_on_wire object;
	reiser4_object_on_wire parent;
	char *addr;
	int with_parent;

	ctx = init_context(super);
	if (IS_ERR(ctx))
		return (struct dentry *)ctx;

	assert("vs-1482",
	       fhtype == FH_WITH_PARENT || fhtype == FH_WITHOUT_PARENT);

	with_parent = (fhtype == FH_WITH_PARENT);

	addr = (char *)fh;

	object_on_wire_init(&object);
	object_on_wire_init(&parent);

	addr = decode_inode(super, addr, &object);
	if (!IS_ERR(addr)) {
		if (with_parent)
			addr = decode_inode(super, addr, &parent);
		if (!IS_ERR(addr)) {
			struct dentry *d;
			typeof(super->s_export_op->find_exported_dentry) fn;

			fn = super->s_export_op->find_exported_dentry;
			assert("nikita-3521", fn != NULL);
			d = fn(super, &object, with_parent ? &parent : NULL,
			       acceptable, context);
			if (d != NULL && !IS_ERR(d))
				/* FIXME check for -ENOMEM */
			  	reiser4_get_dentry_fsdata(d)->stateless = 1;
			addr = (char *)d;
		}
	}

	object_on_wire_done(&object);
	object_on_wire_done(&parent);

	reiser4_exit_context(ctx);
	return (void *)addr;
}

/*
 * Object serialization support.
 *
 * To support knfsd file system provides export_operations that are used to
 * construct and interpret NFS file handles. As a generalization of this,
 * reiser4 object plugins have serialization support: it provides methods to
 * create on-wire representation of identity of reiser4 object, and
 * re-create/locate object given its on-wire identity.
 *
 */

/*
 * return number of bytes that on-wire representation of @inode's identity
 * consumes.
 */
static int encode_inode_size(struct inode *inode)
{
	assert("nikita-3514", inode != NULL);
	assert("nikita-3515", inode_file_plugin(inode) != NULL);
	assert("nikita-3516", inode_file_plugin(inode)->wire.size != NULL);

	return inode_file_plugin(inode)->wire.size(inode) + sizeof(d16);
}

/*
 * store on-wire representation of @inode's identity at the area beginning at
 * @start.
 */
static char *encode_inode(struct inode *inode, char *start)
{
	assert("nikita-3517", inode != NULL);
	assert("nikita-3518", inode_file_plugin(inode) != NULL);
	assert("nikita-3519", inode_file_plugin(inode)->wire.write != NULL);

	/*
	 * first, store two-byte identifier of object plugin, then
	 */
	save_plugin_id(file_plugin_to_plugin(inode_file_plugin(inode)),
		       (d16 *) start);
	start += sizeof(d16);
	/*
	 * call plugin to serialize object's identity
	 */
	return inode_file_plugin(inode)->wire.write(inode, start);
}

/* this returns number of 32 bit long numbers encoded in @lenp. 255 is
 * returned if file handle can not be stored */
/**
 * reiser4_encode_fh - encode_fh of export operations
 * @dentry:
 * @fh:
 * @lenp:
 * @need_parent:
 *
 */
static int
reiser4_encode_fh(struct dentry *dentry, __u32 *fh, int *lenp,
		  int need_parent)
{
	struct inode *inode;
	struct inode *parent;
	char *addr;
	int need;
	int delta;
	int result;
	reiser4_context *ctx;

	/*
	 * knfsd asks as to serialize object in @dentry, and, optionally its
	 * parent (if need_parent != 0).
	 *
	 * encode_inode() and encode_inode_size() is used to build
	 * representation of object and its parent. All hard work is done by
	 * object plugins.
	 */
	inode = dentry->d_inode;
	parent = dentry->d_parent->d_inode;

	addr = (char *)fh;

	need = encode_inode_size(inode);
	if (need < 0)
		return NFSERROR;
	if (need_parent) {
		delta = encode_inode_size(parent);
		if (delta < 0)
			return NFSERROR;
		need += delta;
	}

	ctx = init_context(dentry->d_inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (need <= sizeof(__u32) * (*lenp)) {
		addr = encode_inode(inode, addr);
		if (need_parent)
			addr = encode_inode(parent, addr);

		/* store in lenp number of 32bit words required for file
		 * handle. */
		*lenp = (need + sizeof(__u32) - 1) >> 2;
		result = need_parent ? FH_WITH_PARENT : FH_WITHOUT_PARENT;
	} else
		/* no enough space in file handle */
		result = NFSERROR;
	reiser4_exit_context(ctx);
	return result;
}

/**
 * reiser4_get_dentry_parent - get_parent of export operations
 * @child:
 *
 */
static struct dentry *reiser4_get_dentry_parent(struct dentry *child)
{
	struct inode *dir;
	dir_plugin *dplug;

	assert("nikita-3527", child != NULL);
	/* see comment in reiser4_get_dentry() about following assertion */
	assert("nikita-3528", is_in_reiser4_context());

	dir = child->d_inode;
	assert("nikita-3529", dir != NULL);
	dplug = inode_dir_plugin(dir);
	assert("nikita-3531", ergo(dplug != NULL, dplug->get_parent != NULL));
	if (dplug != NULL)
		return dplug->get_parent(dir);
	else
		return ERR_PTR(RETERR(-ENOTDIR));
}

/**
 * reiser4_get_dentry - get_dentry of export operations
 * @super:
 * @data:
 *
 *
 */
static struct dentry *reiser4_get_dentry(struct super_block *super, void *data)
{
	reiser4_object_on_wire *o;

	assert("nikita-3522", super != NULL);
	assert("nikita-3523", data != NULL);
	/*
	 * this is only supposed to be called by
	 *
	 *     reiser4_decode_fh->find_exported_dentry
	 *
	 * so, reiser4_context should be here already.
	 */
	assert("nikita-3526", is_in_reiser4_context());

	o = (reiser4_object_on_wire *)data;
	assert("nikita-3524", o->plugin != NULL);
	assert("nikita-3525", o->plugin->wire.get != NULL);

	return o->plugin->wire.get(super, o);
}

struct export_operations reiser4_export_operations = {
	.encode_fh = reiser4_encode_fh,
	.decode_fh = reiser4_decode_fh,
	.get_parent = reiser4_get_dentry_parent,
	.get_dentry = reiser4_get_dentry
};

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 79
 * End:
 */
