/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#ifndef __REISER4_LOGMGR_H__
#define __REISER4_LOGMGR_H__

/****************************************************************************************
				    TYPE DECLARATIONS
 ****************************************************************************************/

typedef struct _log_anchor        log_anchor;
typedef struct _log_entry         log_entry;
typedef struct _log_mgr           log_mgr;

/****************************************************************************************
				     TYPE DEFINITIONS
 ****************************************************************************************/

struct _log_anchor
{
  u_int32_t   _blocks;
  u_int32_t   _device;
  u_int32_t   _blocksize;

  /* etc... */
};

struct _log_entry
{

};

struct _log_mgr
{

};

/****************************************************************************************
				  FUNCTION DECLARATIONS
 ****************************************************************************************/

extern int       logmgr_init                  (log_mgr    *mgr);

/* This interface is called by the flush/alloc plugin during transaction/atom commit for
 * overwrite blocks in the transaction.
 */
extern void      logmgr_set_journal_block     (txn_atom   *atom,
					       znode      *frame);

extern void      logmgr_prepare_journal       (txn_atom   *atom);

#endif /* __REISER4_LOGMGR_H__ */
