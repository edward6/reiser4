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

  FIXME-EDWARD: Cipher and hash key-id by the secret key
  (open method requires armored identification of the key */

 __attribute__((unused)) static int crc_file_create(struct inode *object, struct inode *parent, reiser4_object_create_data * data)
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





