/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Compound directory item. See cde.c for description. */

#if !defined( __FS_REISER4_PLUGIN_COMPRESSED_DE_H__ )
#define __FS_REISER4_PLUGIN_COMPRESSED_DE_H__

#include "../../forward.h"
#include "../../kassign.h"
#include "../../dformat.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/dcache.h>	/* for struct dentry, etc  */

typedef struct cde_unit_header {
	de_id hash;
	d16 offset;
} cde_unit_header;

typedef struct cde_item_format {
	d16 num_of_entries;
	cde_unit_header entry[0];
} cde_item_format;

typedef struct cde_entry {
	const struct inode *dir;
	const struct inode *obj;
	const struct qstr *name;
} cde_entry;

typedef struct cde_entry_data {
	int num_of_entries;
	cde_entry *entry;
} cde_entry_data;

/* plugin->item.b.* */
reiser4_key *cde_max_key_inside(const coord_t * coord, reiser4_key * result, void *);
int cde_can_contain_key(const coord_t * coord, const reiser4_key * key, const reiser4_item_data *);
int cde_mergeable(const coord_t * p1, const coord_t * p2);
unsigned cde_nr_units(const coord_t * coord);
reiser4_key *cde_unit_key(const coord_t * coord, reiser4_key * key);
int cde_estimate(const coord_t * coord, const reiser4_item_data * data);
void cde_print(const char *prefix, coord_t * coord);
int cde_init(coord_t * coord, reiser4_item_data * data);
lookup_result cde_lookup(const reiser4_key * key, lookup_bias bias, coord_t * coord);
int cde_paste(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG);
int cde_can_shift(unsigned free_space, coord_t * coord,
		  znode * target, shift_direction pend, unsigned *size, unsigned want);
void cde_copy_units(coord_t * target, coord_t * source,
		    unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space);
int cde_cut_units(coord_t * coord, unsigned *from, unsigned *to,
		  const reiser4_key * from_key, const reiser4_key * to_key, reiser4_key * smallest_removed, void *);
void cde_print(const char *prefix, coord_t * coord);
int cde_check(const coord_t * coord, const char **error);

/* plugin->u.item.s.dir.* */
int cde_extract_key(const coord_t * coord, reiser4_key * key);
int cde_update_key(const coord_t * coord, const reiser4_key * key, lock_handle * lh);
char *cde_extract_name(const coord_t * coord, char *buf);
int cde_add_entry(struct inode *dir, coord_t * coord,
		  lock_handle * lh, const struct dentry *name, reiser4_dir_entry_desc * entry);
int cde_rem_entry(struct inode *dir, const struct qstr * name, coord_t * coord, lock_handle * lh, reiser4_dir_entry_desc * entry);
int cde_max_name_len(const struct inode *dir);

/* __FS_REISER4_PLUGIN_COMPRESSED_DE_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
