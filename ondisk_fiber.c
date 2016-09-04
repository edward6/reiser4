/*
  Copyright (c) 2014 Eduard Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * On-disk fiber is a set of blocks, which contains a portion of
 * hash space segments, which belong to a subvolume. Whenever we
 * perform a volume operation (add subvolume, remove subvolume,
 * split, etc), fibers get updated, so they participate in transactions.
 */

#include "debug.h"
#include "super.h"

#define FIBER40_MAGIC "FiBeR_40"
#define FIBER40_MAGIC_SIZE (8)

struct fiber_head40 {
	char magic[FIBER40_MAGIC_SIZE];
	d64 next; /* location of the next fiber node */
	char body[0]; /* fiber body */
};

static jnode *fiber_node_alloc(reiser4_block_nr *loc, reiser4_subvol *subv)
{
	jnode *node;

	node = reiser4_alloc_io_head(loc, subv);
	if (node != NULL)
		jref(node);
	return node;
}

static void fiber_node_drop(jnode *node)
{
	assert("edward-xxx", jnode_get_type(node) == JNODE_IO_HEAD);

	jput(node);
	jdrop(node);
}

static void fiber_nodes_drop(struct reiser4_subvol *subvol)
{
	u64 i;
	if (!subvol->fiber_nodes)
		return;
	for (i = 0; i < subvol->fiber_len; i++)
		fiber_node_drop(subvol->fiber_nodes[i]);
}

static int check_fiber40(const jnode *node)
{
	struct fiber_head40 *header = (struct fiber_head40 *)jdata(node);

	if (memcmp(header->magic, FIBER40_MAGIC, FIBER40_MAGIC_SIZE) != 0) {
		warning("edward-xxx",
			"Fiber head at block %s corrupted\n",
			sprint_address(jnode_get_block(node)));
		return RETERR(-EIO);
	}
	return 0;
}

/*
 * Unpack fiber block of index @idx at location @loc;
 * Find location of the next fiber block and store it in @loc;
 * Update number of segments to extract;
 * Return number of extracted segments, or an error.
 */
static int fiber_block_unpack(u32 idx, /* serial number of fiber's node */
			      u64 *nsegs, /* number of segments to extract */
			      reiser4_subvol *subv, /* subvolume of the fiber */
			      reiser4_block_nr *loc, /* location on disk */
			      void *result,
			      bool pin_jnodes)
{
	int ret;
	int block_size;
	int to_node;
	jnode *node;
	struct fiber_head40 *header;
	distribution_plugin *dplug = current_dist_plug();
	u64 fib_len_bytes = *nsegs * dplug->seg_size;

	node = fiber_node_alloc(loc, subv);
	if (!node)
		return RETERR(-ENOMEM);

	ret = jload(node);
	if (ret < 0)
		return ret;
	ret = check_fiber40(node);
	if (ret) {
		jrelse(node);
		return ret;
	}
	block_size = current_blocksize;
	header = (struct fiber_head40 *)jdata(node);

	if (fib_len_bytes <= block_size - sizeof(*header)) {
		/*
		 * last block in the on-disk fiber
		 */
		to_node = fib_len_bytes;
		*loc = 0;
		goto unpack;
	}
	/*
	 * not the last block, extract location
	 * of the next block in the on-disk fiber
	 */
	to_node = block_size - sizeof(*header);
	*loc = le64_to_cpu(get_unaligned(&header->next));

	assert("edward-xxx", to_node % dplug->seg_size == 0);

 unpack:
	dplug->unpack(result, header->body, to_node/dplug->seg_size);
	*nsegs -= (to_node/dplug->seg_size);

	jrelse(node);
	if (pin_jnodes)
		subv->fiber_nodes[idx] = node;
	else
		fiber_node_drop(node);
	return to_node;
}

/**
 * Read fiber from disk
 *
 * @len: number of segments in the fiber
 * @loc: location of the fiber's first block
 */
int reiser4_fiber_load(reiser4_subvol *subv, u64 len,
		       reiser4_block_nr loc, int pin_jnodes)
{
	u32 i;
	int ret = -ENOMEM;
	char *fib;
	jnode **nodes = NULL;
	distribution_plugin *dplug;

	if (loc == 0)
		/*
		 * There is no fiber on disk,
		 * it may indicate foirmat 4.X.Y
		 */
		return 0;
	assert("edward-xxx", len > 0);

	dplug = current_dist_plug();
	fib = kzalloc(len * dplug->seg_size, reiser4_ctx_gfp_mask_get());
	if (!fib)
		return RETERR(-ENOMEM);
	if (pin_jnodes) {
		nodes = kzalloc(len * sizeof(subv->fiber_nodes),
				reiser4_ctx_gfp_mask_get());
		if (!nodes)
			goto error;
		subv->fiber_nodes = nodes;
	}
	for (i = 0; len > 0; i++) {
		ret = fiber_block_unpack(i, &len, subv, &loc, fib, pin_jnodes);
		if (ret < 0)
			goto error;
		fib += ret * dplug->seg_size;
	}
	subv->fiber = fib;
	return 0;
 error:
	if (fib)
		kfree(fib);
	if (nodes)
		kfree(nodes);
	return ret;
}

void reiser4_fiber_done(struct reiser4_subvol *subv)
{
	if (subv->fiber) {
		kfree(subv->fiber);
		fiber_nodes_drop(subv);
	}
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
