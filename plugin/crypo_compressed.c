/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* The object plugin of reiser4 crypto-compressed (crc-)files. */

/* We store all the crc-specific attributes as following
   non-default plugins in plugin stat-data extension:
   1) file plugin
   2) crypto plugin
   3) compression plugin
*/
   

#include "../debug.h"
#include "../inode.h"
#include "../jnode.h"
#include "plugin.h"

extern int common_file_save(struct inode *inode);

/* plugin->create() method for crypto-compressed files 

  . install plugins
  . set bits in appropriate masks
  . attach secret key
  . attach key-id

  FIXME-EDWARD: cipher key-id, or make sure that user level provides it */

static int crc_file_create(struct inode *object, struct inode *parent, reiser4_object_create_data * data)
{
	int result;
	scint_t *extmask;
	reiser4_inode * info;
	crypto_plugin * cplug;
	crc_object_create_data * crc_data;
		
	assert("edward-23", object != NULL);
	assert("edward-24", parent != NULL);
	assert("edward-25", data != NULL);
	assert("edward-26", inode_get_flag(object, REISER4_NO_SD));
	assert("edward-27", data->id = CRC_FILE_PLUGIN_ID);
	
	crc_data = data->crc;
	assert("edward-28", crc_data != NULL);
	
	info = reiser4_inode_data(object);
	assert("edward-29", info != NULL);
		
	cplug = crypto_plugin_by_id(crc_data->cra);
	
	assert("edward-30", info->pset->crypto = NULL);
	plugin_set_crypto(&info->pset, cplug);

	assert("edward-31", info->pset->compression = NULL);
	plugin_set_compression(&info->pset, 
			       compression_plugin_by_id(crc_data->coa));

	info->plugin_mask |= (1 << REISER4_FILE_PLUGIN_TYPE) |
		(1 << REISER4_CRYPTO_PLUGIN_TYPE) |
		(1 << REISER4_COMPRESSION_PLUGIN_TYPE);
	extmask = &info->extmask;
	scint_pack(extmask, scint_unpack(extmask) |
		   (1 << PLUGIN_STAT) |
		   (1 << KEY_ID_STAT), GFP_ATOMIC);

	info->expkey = reiser4_kmalloc(cplug->keysize, GFP_KERNEL);
	if (!info->expkey)
		return -ENOMEM;
	result = cplug->set_key(info->expkey, crc_data->key);
	if (result)
		goto destroy_key;
	assert ("edward-34", !inode_get_flag(object, REISER4_SECRET_KEY_INSTALLED));
	inode_set_flag(object, REISER4_SECRET_KEY_INSTALLED);
		
	/* set temporary pointer for the key-id */
	info->keyid = crc_data->keyid;
	result = common_file_save(object);
	if (!result)
		return 0;
	if (info->keyid == crc_data->keyid) 
		goto destroy_key;

        /* the pointer was updated to kmalloced data, but save() method
	   for some another sd-extension failed */
	assert("edward-32", !memcmp(info->keyid, crc_data->keyid, sizeof (reiser4_keyid_stat)));

	reiser4_kfree(info->keyid, sizeof (reiser4_keyid_stat));
	inode_clr_flag(object, REISER4_KEYID_LOADED);
	
 destroy_key:
	xmemset(info->expkey, 0, cplug->keysize);
	reiser4_kfree(info->expkey, cplug->keysize);
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}

static void cluster_pos_init (cluster_pos *pos)
{
	assert("edward-49", pos != NULL);
	xmemset(pos, 0, sizeof *pos);
}

/* Looks for the position in the cluster cache
   associated with @node. Search results are in @pos */
static void lookup_cluster_pos (jnode *node, struct inode *inode, off_t off, cluster_pos *pos)
{
	file_plugin *fplug = inode_file_plugin(inode);
	cluster_head *next;

	assert("edward-42", node != NULL);
	assert("edward-43", inode != NULL);
	assert("edward-44", pos != NULL);
	
	fplug -> key_by_inode(inode, off, &pos->key);
	next = node->c_cache;
	
	while (next != NULL) {
		file_plugin *next_fplug;
		next_fplug = inode_file_plugin(next->c_inode);
		next_fplug -> key_by_inode(next->c_inode, next->c_offset, &pos->next_key);
		if (keyle(&pos->key, &pos->next_key)) {
			break;
		}
		pos->prev = next;
		next = next->c_next;
	}
	pos->next=next;
}

/* looks for the cluster in the @node cluster cache by @inode and @offset;
   returns pointer to the cluster, if it was found. If it wasn't, inserts
   in the current position new allocated cluster filled by @inode and @offest.
   Returns an error if allocation was failed. 
*/
static cluster_head *find_or_alloc_cluster (jnode *node, struct inode *inode, off_t offset)
{
	cluster_head *new = NULL;
	cluster_pos pos;
	
	assert("edward-39", node != NULL);
	assert("edward-40", inode != NULL);
	
	cluster_pos_init(&pos);
	lookup_cluster_pos (node, inode, offset, &pos);
	
	if (pos.next != NULL && keyeq(&pos.key, &pos.next_key))
		/* cluster was found */
		return pos.next;
	
	/* cluster was not found, allocate a new one */
	new = reiser4_kmalloc(sizeof(cluster_head), GFP_KERNEL);
	if (!new)
		return (ERR_PTR(-ENOMEM));
	xmemset(new, 0, sizeof *new);
	new->c_data = reiser4_kmalloc(MIN_CLUSTER_SIZE, GFP_KERNEL);
	if (new->c_data == NULL) {
		reiser4_kfree(new, sizeof(cluster_head));
		return (ERR_PTR(-ENOMEM));
	}
	new->c_inode = inode;
	new->c_offset = offset;

        /* insert @new into found position in the list */
	if (pos.prev == NULL)
		/* @new is first cluster in the list */
		node->c_cache = new;
	else
		pos.prev->c_next = new;
	new->c_next = pos.next;
	return new;
}

/* inserts @clust into @node cluster cache. */
			    
static void insert_cluster (jnode *node, cluster_head *clust)
{
	cluster_pos pos;
	
	assert("edward-45", node != NULL);
	assert("edward-46", clust != NULL);

	cluster_pos_init(&pos);
	lookup_cluster_pos (node, clust->c_inode, clust->c_offset, &pos);
	if (pos.next != NULL)
		assert("edward-47", !keyeq(&pos.key, &pos.next_key));
	        /* cluster already exists in the cache */
	if (pos.prev == NULL)
		node->c_cache = clust;
	else
		pos.prev->c_next = clust;
	clust->c_next = pos.next;
}

/* free @node cluster cache */
void release_node_clusters (jnode * node)
{
	cluster_head *clust, *next;
	clust = node->c_cache;
	
	for (; clust != NULL; clust = next) {
		next = clust->c_next;
		reiser4_kfree(clust->c_data, MIN_CLUSTER_SIZE);
		reiser4_kfree(clust, sizeof *clust);
	}
}

/* looks for the cluster in @from by (@inode, @offset)
   and moves it to @to cluster cache */

static void move_cluster (struct inode *inode, loff_t offset, jnode *from, jnode *to)
{
	cluster_pos pos;
	
	assert("edward-41", inode != NULL);
	assert("edward-42", from != NULL);
	assert("edward-43", to != NULL);

	cluster_pos_init(&pos);
	lookup_cluster_pos (from, inode, offset, &pos);

	if (pos.next != NULL)
		assert("edward-48", keyeq(&pos.key, &pos.next_key));
        	/* there is no such cluster in the @from cache */
	/* delete @pos.next from the first cache */
	if (pos.prev == NULL)
		from->c_cache = pos.next->c_next;
	else
		pos.prev->c_next = pos.next->c_next;

	insert_cluster (to, pos.next);
}



