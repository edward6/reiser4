#ifndef DST_H
#define DST_H

#include "fsx32.h"

union reiser4_dcx {
	struct fsx32_dcx fsx32;
};

int fix_data_reservation(const coord_t *coord, lock_handle *lh,
			 struct inode *inode, loff_t pos, jnode *node,
			 int count, int truncate);

#endif /* DST_H */

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
