/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* ctails (aka "crypto tails") are items for cryptcompress objects */

/* DESCRIPTION:

Each cryptcompress object is stored on disk as a set of clusters sliced
into ctails.

Internal on-disk structure:

        HEADER   (4)  Here stored disk cluster size in bytes (this is original cluster size
                      translated by ->scale() method of crypto plugin)
	BODY
*/

#include "../../forward.h"
#include "../../debug.h"
#include "../../dformat.h"
#include "../../kassign.h"
#include "../../key.h"
#include "../../coord.h"
#include "item.h"
#include "../node/node.h"
#include "../plugin.h"
#include "../../znode.h"
#include "../../carry.h"
#include "../../tree.h"
#include "../../inode.h"

#include <linux/fs.h>	

/* return body of ctail item at @coord */
static ctail_item_format *
formatted_at(const coord_t * coord)
{
	assert("edward-xx", coord != NULL);
	return item_body_by_coord(coord);
}

unsigned
cluster_size_by_coord(const coord_t * coord) {
	return d32tocpu(&formatted_at(coord)->disk_cluster_size);
}

/* plugin->u.item.b.max_key_inside :
   tail_max_key_inside */

/* plugin->u.item.b.can_contain_key :
   tail_can_contain_key */

/* plugin->u.item.b.mergeable
   c-tails of different clusters are not mergeable */
int
ctail_mergeable(const coord_t * p1, const coord_t * p2)
{
	reiser4_key key1, key2;

	assert("edward-xx", item_type_by_coord(p1) == ORDINARY_FILE_METADATA_TYPE);
	assert("edward-xx", item_id_by_coord(p1) == CTAIL_ID);

	if (item_id_by_coord(p2) != CTAIL_ID) {
		/* second item is of another type */
		return 0;
	}

	item_key_by_coord(p1, &key1);
	item_key_by_coord(p2, &key2);
	if (get_key_locality(&key1) != get_key_locality(&key2) ||
	    get_key_objectid(&key1) != get_key_objectid(&key2) ||
	    get_key_type(&key1) != get_key_type(&key2)) {
		/* items of different objects */
		return 0;
	}
	if ((get_key_offset(&key1) + ctail_nr_units(p1) != get_key_offset(&key2)) ||
	    ctail_nr_units(p1) == cluster_size_by_coord(p1)) {
		/* items of different clusters */
		return 0;
	}
	return 1;
}

/* plugin->u.item.b.nr_units */
unsigned
ctail_nr_units(const coord_t * coord)
{
	return (item_length_by_coord(coord) - sizeof(formatted_at(coord)->disk_cluster_size));
}

/* plugin->u.item.b.unit_key: 
   tail_unit_key */

/* estimate how much space is necessary in item to insert/paste set of entries
   described in @data. */
int
ctail_estimate(const coord_t * coord /* coord of item */,
	     const reiser4_item_data * data /* parameters for new item */)
{
	if (coord == NULL)
		/* insert */
		return (sizeof(ctail_item_format) + data->length);
	else
		/* paste */
		return data->length;	
}

#if REISER4_DEBUG_OUTPUT
/* ->print() method for this item plugin. */
void
ctail_print(const char *prefix /* prefix to print */ ,
	  coord_t * coord /* coord of item to print */ )
{
	assert("edward-xx", prefix != NULL);
	assert("edward-xx", coord != NULL);

	if (item_length_by_coord(coord) < (int) sizeof (ctail_item_format)) 
		printk("%s: wrong size: %i < %i\n", prefix, item_length_by_coord(coord), sizeof (ctail_item_format));
	else
		printk("%s: disk cluster size: %i\n", prefix, cluster_size_by_coord(coord));
}
#endif

/* plugin->u.item.b.lookup :
   tail_lookup */

/* plugin->u.item.b.check */

/* plugin->u.item.b.paste */
int
ctail_paste(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG)
{
	unsigned old_nr_units;
	char *item;

	/* number of units the item had before resizing has been performed */
	old_nr_units = ctail_nr_units(coord) - data->length;

	/* tail items never get pasted in the middle */
	assert("edward-xx",
	       (coord->unit_pos == 0 && coord->between == BEFORE_UNIT) ||
	       (coord->unit_pos == old_nr_units - 1 &&
		coord->between == AFTER_UNIT) ||
	       (coord->unit_pos == 0 && old_nr_units == 0 && coord->between == AT_UNIT));

	item = item_body_by_coord(coord) + sizeof (ctail_item_format);
	if (coord->unit_pos == 0)
		/* make space for pasted data when pasting at the beginning of
		   the item */
		xmemmove(item + data->length, item, old_nr_units);

	if (coord->between == AFTER_UNIT)
		coord->unit_pos++;

	if (data->data) {
		assert("edward-xx", data->user == 0 || data->user == 1);
		if (data->user) {
			assert("edward-xx", schedulable());
			/* copy from user space */
			__copy_from_user(item + coord->unit_pos, data->data, (unsigned) data->length);
		} else
			/* copy from kernel space */
			xmemcpy(item + coord->unit_pos, data->data, (unsigned) data->length);
	} else {
		xmemset(item + coord->unit_pos, 0, (unsigned) data->length);
	}
	return 0;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
