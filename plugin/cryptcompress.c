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
  . attach key to cryptcompress inode data
  . make fingerprint of the pair (key, keyid) and attach it to the inode's crypto info

  FIXME-EDWARD: Cipher and hash key-id by the secret key
  (open method requires armored identification of the key */

#if 0
 __attribute__((unused)) static int cryptcompress_create(struct inode *object, struct inode *parent, reiser4_object_create_data * data)
{
	int result;
	scint_t *extmask;
	reiser4_inode * info;
	cryptcompress_info_t * crc_info;
	crypto_plugin * cplug;
	digest_plugin * dplug;
	compression_plugin *coplug;
	cryptcompress_data_t * crc_data;
	crypto_stat_t stat;
	__u8 fip[dplug->digestsize];
	void * digest_ctx;
	
	
	assert("edward-23", object != NULL);
	assert("edward-24", parent != NULL);
	assert("edward-25", data != NULL);
	assert("edward-26", inode_get_flag(object, REISER4_NO_SD));
	assert("edward-27", data->id = CRC_FILE_PLUGIN_ID);
	
	crc_data = data->crc;
	info = reiser4_inode_data(object);
	crc_info = cryptcompress_inode_data(object);
	
	assert("edward-28", crc_data != NULL);
	assert("edward-xx", crc_data->keyid_size);
	assert("edward-29", info != NULL);
	assert("edward-xx", crc_info = NULL);
	assert("edward-30", info->pset->crypto = NULL);
	assert("edward-xx", info->pset->digest = NULL);
	assert("edward-31", info->pset->compression = NULL);
	
	cplug = crypto_plugin_by_id(crc_data->cra);
	plugin_set_crypto(&info->pset, cplug);

	dplug = digest_plugin_by_id(crc_data->dia);
	plugin_set_digest(&info->pset, dplug);

	coplug = compression_plugin_by_id(crc_data->coa);
	plugin_set_compression(&info->pset, coplug);

	info->plugin_mask |= (1 << REISER4_FILE_PLUGIN_TYPE) |
		(1 << REISER4_CRYPTO_PLUGIN_TYPE) |
		(1 << REISER4_DIGEST_PLUGIN_TYPE) |
		(1 << REISER4_COMPRESSION_PLUGIN_TYPE);
	extmask = &info->extmask;
	scint_pack(extmask, scint_unpack(extmask) |
		   (1 << PLUGIN_STAT) |
		   (1 << CLUSTER_STAT) |
		   (1 << CRYPTO_STAT), GFP_ATOMIC);

	/* alloc memory for expkey */
	crc_info->expkey = reiser4_kmalloc((cplug->keysize)*sizeof(__u32), GFP_KERNEL);
	if (!crc_info->expkey)
		return RETERR(-ENOMEM);
	/* load expkey */
	result = cplug->set_key(crc_info->expkey, crc_data->key);
	if (result)
		goto destroy_key;
	assert ("edward-34", !inode_get_flag(object, REISER4_SECRET_KEY_INSTALLED));
	inode_set_flag(object, REISER4_SECRET_KEY_INSTALLED);
		
	/* fingerprint creation of the pair (@key, @keyid) includes two steps: */
	/* 1. encrypt keyid by key: */
	/* FIXME-EDWARD: add encryption of keyid */

	/* 2. make digest of encrypted keyid */
	result = dplug->alloc(digest_ctx);
	if (result)
		goto destroy_key;
	dplug->init(digest_ctx);
	dplug->update(digest_ctx, crc_data->keyid, crc_data->keyid_size);
	dplug->final(digest_ctx, fip);
	dplug->free(digest_ctx);
	
	/* add temporary crypto_info to the inode */
	stat.keysize = crc_data->keysize;
	stat.fip = fip;
	info->crypt = &stat;
	
	result = common_file_save(object);
	if (!result)
		return 0;
	if (info->crypt == &stat) 
		goto destroy_key;

	/* now the pointer was updated to kmalloced data, but save() method
	   for some another sd-extension failed */
	
	assert("edward-32", !memcmp(info->crypt->keyid, stat.keyid, dplug->digestsize));

	reiser4_kfree(info->keyid, sizeof (reiser4_keyid_stat));
	inode_clr_flag(object, REISER4_KEYID_LOADED);
	
 destroy_key:
	xmemset(crc_info->expkey, 0, (cplug->keysize)*sizeof(__u32));
	reiser4_kfree(crc_info->expkey, (cplug->keysize)*sizeof(__u32));
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}
#endif
