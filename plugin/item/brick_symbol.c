/*
  Copyright (c) 2019-2020 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "../../forward.h"
#include "../../debug.h"
#include "../../dformat.h"
#include "../../kassign.h"
#include "../../coord.h"
#include "../../tree.h"
#include "../../lock.h"
#include "../../super.h"

#include "brick_symbol.h"
#include "item.h"
#include "../plugin.h"

int store_brick_symbol(const reiser4_key *key, void *data, int length)
{
	int ret;
	reiser4_item_data idata;
	coord_t coord;
	lock_handle lh;

	memset(&idata, 0, sizeof idata);

	idata.data = data;
	idata.user = 0;
	idata.length = length;
	idata.iplug = item_plugin_by_id(BRICK_SYMBOL_ID);

	init_lh(&lh);
	ret = insert_by_key(meta_subvol_tree(), key,
			    &idata, &coord, &lh, LEAF_LEVEL, CBK_UNIQUE);
	assert("edward-2296",
	       ergo(ret == 0,
		    WITH_COORD(&coord,
			       item_length_by_coord(&coord) == length)));
	done_lh(&lh);
	return ret;
}

int load_brick_symbol(const reiser4_key *key, void *data,
		      int length, int exact)
{
	int ret;
	coord_t coord;
	lock_handle lh;

	init_lh(&lh);
	ret = coord_by_key(meta_subvol_tree(), key,
			   &coord, &lh, ZNODE_READ_LOCK,
			   exact ? FIND_EXACT : FIND_MAX_NOT_MORE_THAN,
			   LEAF_LEVEL, LEAF_LEVEL, CBK_UNIQUE, NULL);
	if (ret == 0) {
		ret = zload(coord.node);
		if (ret == 0) {
			int ilen = item_length_by_coord(&coord);
			if (ilen == length)
				memcpy(data, item_body_by_coord(&coord), ilen);
			else {
				warning("edward-2297",
					"Wrong brick symbol length: %i != %i",
					ilen, length);
				ret = RETERR(-EIO);
			}
		}
		zrelse(coord.node);
	}
	done_lh(&lh);
	return ret;
}

int kill_brick_symbol(const reiser4_key *key)
{
	return reiser4_cut_tree(meta_subvol_tree(), key, key, NULL, 1);
}

typedef struct brick_symbol {
	d64 id; /* internal brick ID, AKA index in the array of slots */
} brick_symbol_t;


static oid_t brick_symbol_locality(void)
{
	return get_key_objectid(get_meta_subvol()->df_plug->
				root_dir_key(NULL)) + 2;
}

/**
 * convert 1-st 8 bytes of brick's UUID to a 64-bit number
 */
static u64 brick_symbol_fulloid(reiser4_subvol *subv)
{
	return le64_to_cpu(get_unaligned((u64 *)subv->uuid));
}

/**
 * convert 2-nd 8 bytes of brick's UUID to a 64-bit number
 */
static u64 brick_symbol_offset(reiser4_subvol *subv)
{
	return le64_to_cpu(get_unaligned((u64 *)&subv->uuid[8]));
}

/*
  Construct a key for brick symbol item. Key has the following format:

|        60     | 4 |        64        |         64        |          64       |
+---------------+---+------------------+-------------------+-------------------+
|   locality    | 0 |        0         | 1-st part of uuid | 2-nd part of uuid |
+---------------+---+------------------+-------------------+-------------------+
|                   |                  |                   |                   |
|     8 bytes       |     8 bytes      |      8 bytes      |      8 bytes      |

   This is in large keys format. In small keys format second 8 byte chunk is
   out. Locality is a constant returned by safe_link_locality().
   UUID is external ID of the brick for which we construct the key.
*/

static reiser4_key *build_brick_symbol_key(reiser4_key *key,
					   reiser4_subvol *subv)
{
	reiser4_key_init(key);
	set_key_locality(key, brick_symbol_locality());
	set_key_fulloid(key, brick_symbol_fulloid(subv));
	set_key_offset(key, brick_symbol_offset(subv));
	return key;
}

int brick_symbol_add(reiser4_subvol *subv)
{
	reiser4_key key;
	brick_symbol_t bs;

	put_unaligned(cpu_to_le64(subv->id), &bs.id);
	build_brick_symbol_key(&key, subv);

	return store_brick_symbol(&key, &bs, sizeof bs);
}

int brick_symbol_del(reiser4_subvol *subv)
{
	reiser4_key key;

	return kill_brick_symbol(build_brick_symbol_key(&key, subv));
}

int brick_identify(reiser4_subvol *subv)
{
	int ret;
	reiser4_key key;
	brick_symbol_t bs;

	ret = load_brick_symbol(build_brick_symbol_key(&key, subv),
				&bs, sizeof bs, 1 /* exact */);
	if (ret)
		return 0;
	return bs.id == subv->id;
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  End:
*/
