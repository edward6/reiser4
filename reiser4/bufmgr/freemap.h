/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#ifndef __REISER4_FREEMAP_H__
#define __REISER4_FREEMAP_H__

/* Place holders and stub functions.  Not the real thing. */
typedef struct _super_block  super_block;

struct _super_block
{
  u_int32_t           s_blocksize;
  u_int64_t           s_block_count;

  /* Freemap state. */
  u_int32_t           _freemap_count;
  bm_blockno          _freemap_base;
  u_int64_t           _freeblk_count;

  /* Allocation state. */
  spinlock_t          _allocid_lock;
  bm_blockno          _allocid_next;
};

extern u_int32_t    freemap_blocks_needed         (super_block      *super);

extern void         freemap_deallocate            (txn_handle       *handle,
						   bm_blockid const *block);

extern void         freemap_allocate              (txn_handle       *handle,
						   bm_blockid       *block);


#endif
