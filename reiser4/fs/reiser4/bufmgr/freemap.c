/* Copyright 2001 by Hans Reiser, licensing governed by reiserfs/README
 */

/* are you reusing code from bitmap.c?  If not, why not? -Hans @@ new comment: */

/* The point of this skeleton code is not bitmap allocation, it is to explore the
 * interface between the transaction manager and the space manager, nothing more.  I
 * copied a little bit of code out of the old bitmap.c, but the important point here is to
 * get the calls to place function calls txnmgr_block_capture() and bufmgr_blkput() and to
 * make assertions. */
#include "bufmgr.h"

static __inline__ int
freemap_get_bit_address (super_block      *super,
			 bm_blockno        blkno,
			 int              *bmapnr,
			 int              *bytoff)
{
  int bitoff;
  assert ("jmacd-32", blkno > 0 && blkno < super->s_block_count);

  *bmapnr = (blkno / (super->s_blocksize << 3));
  bitoff  = (blkno % (super->s_blocksize << 3));
  *bytoff = (bitoff >> 3);

  return 1 << (bitoff % 8);
}

u_int32_t
freemap_blocks_needed (super_block *s)
{
  return s->s_block_count / (s->s_blocksize << 3);
}

void
freemap_deallocate (txn_handle       *handle,
		    bm_blockid const *block)
{
  super_block *super = block->_super;
  bm_blockno   blkno = block->_blkno;
  int bmapnr, byteoff, mask;
  bm_blkref  freeref;
  bm_blockid freeid;
  u_int8_t  *string;

  mask = freemap_get_bit_address (super, blkno, & bmapnr, & byteoff);

  freeid._super = super;
  freeid._blkno = super->_freemap_base + bmapnr;

  txnmgr_block_capture (handle, & freeid, TXN_CAPTURE_WRITE, & freeref);

  string = freeref._frame->_buffer->_contents;

  assert ("jmacd-100", (string[byteoff] & mask) != 0);

  super->_freeblk_count += 1;

  string[byteoff] &= ~mask;

  bufmgr_blkput (freeref._frame);
}

void
freemap_allocate (txn_handle       *handle,
		  bm_blockid       *block)
{
  super_block *super = handle->_super;
  bm_blkref    freeref;
  bm_blockid   freeid;
  u_int8_t    *string;
  int i, j, k, m, c;

  block->_super = super;

  freeid._super = super;
  freeid._blkno = super->_freemap_base;

  for (i = 0; i < super->_freemap_count; i += 1, freeid._blkno += 1)
    {
      /* @@ This is sketchy... Obviously this causes atom fusion.  Perhaps we want a
       * bounce-mode capture or something to let us find un-captured blocks.  Or we want
       * to first capture read and then upgrade write.  Also we update the superblock
       * here....  Or do we defer that computation? */
      txnmgr_block_capture (handle, & freeid, TXN_CAPTURE_WRITE, & freeref);

      string = freeref._frame->_buffer->_contents;

      for (j = 0; j < super->s_blocksize; j += 1)
	{
	  if ((c = string[j]) != 255)
	    {
	      for (k = 0, m = 1; m < 256; m <<= 1, k += 1)
		{
		  if ((c & m) == 0)
		    {
		      block->_blkno = (i * (super->s_blocksize << 3)) + (j << 3) + k;

		      string[j] |= m;

		      super->_freeblk_count -= 1;

		      bufmgr_blkput (freeref._frame);
		      return;
		    }
		}
	    }
	}

      bufmgr_blkput (freeref._frame);
    }

  rpanic ("jmacd-120", "no free blocks found");
}
