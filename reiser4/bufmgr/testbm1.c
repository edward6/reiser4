/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#include "bufmgr.h"
#include "zipf.h"

#include <stdlib.h>
#include <asm/msr.h>
#include <unistd.h>

/****************************************************************************************
				     TEST PARAMETERS
 ****************************************************************************************/

#define PROC_COUNT      (10)
#define LARGE_NUMBER    (1<<27)

#define BLOCK_COUNT     (1<<18)
#define FILE_COUNT      (1<<15)
#define BUFFER_COUNT    (1<<15)

#define WRITE_PROB      (0.3)
#define SCAN_PROB       (0.3)

#define FILE_ZIPF_ALPHA (6)

#define HASH_FILL       (1.0)

#define DISKWAIT_SCALE  (100)
#define DISKWAIT_MIN    (5000)
#define DISKWAIT_MAX    (15000)

/****************************************************************************************
				 DERIVED TEST PARAMETERS
 ****************************************************************************************/

#define NOSCAN_PROB        (1.0-SCAN_PROB)
#define READ_PROB          (1.0-WRITE_PROB)

#define WRITE_SCAN_PROB    (SCAN_PROB   * WRITE_PROB)
#define WRITE_RAND_PROB    (NOSCAN_PROB * WRITE_PROB)
#define READ_SCAN_PROB     (SCAN_PROB   * READ_PROB)
#define READ_RAND_PROB     (NOSCAN_PROB * READ_PROB)

/****************************************************************************************
				     TEST STRUCTURES
 ****************************************************************************************/

typedef struct _test_proc   test_proc;
typedef struct _test_file   test_file;
typedef struct _test_sim    test_sim;

struct _test_proc
{
  bm_blkref       _bref;
  u_int32_t       _pid;
  u_int32_t       _gets;
  u_int32_t       _scanpgs;
  bm_blockno      _blkno;
  int             _writing;
  test_file      *_file;
  pthread_t       _pthread;
};

struct _test_file
{
  bm_blockno          _block_offset;
  u_int32_t           _block_count;
};

struct _test_sim
{
  super_block         _super;
  spinlock_t          _barrier_lock;
  int                 _barrier_count;
  wait_queue_head_t   _barrier_wait;

  zipf_table         *_file_zipf;
  test_proc          *_procs;
  test_file          *_files;

  u_int32_t           _get_count;
  u_int64_t           _scan_count;
  u_int64_t           _noscan_count;
  u_int64_t           _write_count;
  u_int64_t           _read_count;

  u_int64_t           _start_cycle;
  u_int64_t           _end_cycle;
};

static test_sim _the_sim;

/****************************************************************************************
				     CACHE SIMULATOR
 ****************************************************************************************/

void
die (const char *str)
{
  printf ("Bufmgr testing abnormal failure: %s\n", str);
  abort ();
}

void
test_stat ()
{
  u_int64_t tothits   = _the_mgr._blk_hits + _the_mgr._blk_hit_page_waits;
  u_int64_t totmisses = _the_mgr._blk_misses;
  u_int64_t tottries  = tothits + totmisses;

  printf ("\n");
  printf ("HITS\tMISSES\tHITRATE\tPWAIT\tHRACE\tMRACE\n");
  printf ("%.2fM\t" "%.2fM\t" "%.2f%%\t" "%qd\t" "%qd\t" "%qd\n",
	  (double) tothits / 1e6,
	  (double) totmisses / 1e6,
	  100.0 * (double) tothits / (double) tottries,
	  _the_mgr._blk_hit_page_waits,
	  _the_mgr._blk_hit_races,
	  _the_mgr._blk_miss_races);

  printf ("\n");
  printf ("CYCLES\tOPS\tIOCOUNT\tPRIME?\tRFILL\tAFILL\n");
  printf ("%.2fB\t" "%.2fM\t" "%.2fM\t"
	  "%s\t" "%.2f\t" "%.2f\n",
	  (double) (_the_sim._end_cycle - _the_sim._start_cycle) / 1e9,
	  (double) LARGE_NUMBER / 1e6,
	  (double) iosched_count () / 1e6,
#if BM_HASH_PRIME
	  "yes",
#else
	  "no",
#endif
	  _the_mgr._bufhash_fill_req,
	  _the_mgr._bufhash_fill_act
	  );

  printf ("\n");
  printf ("READ\tWRITE\tWPCT\tNOSCAN\tSCAN\tSPCT\tBLOCKS\n");
  printf ("%.2fM\t" "%.2fM\t" "%.2f%%\t"
	  "%.2fM\t" "%.2fM\t" "%.2f%%\t"
	  "%qu\n",
	  (double) _the_sim._read_count / 1e6,
	  (double) _the_sim._write_count / 1e6,
	  100.0 * _the_sim._write_count / (_the_sim._write_count + _the_sim._read_count),
	  (double) _the_sim._noscan_count / 1e6,
	  (double) _the_sim._scan_count / 1e6,
	  100.0 * _the_sim._scan_count / (_the_sim._scan_count + _the_sim._noscan_count),
	  _the_sim._super.s_block_count);


  printf ("\n");
}

void
test_use_page (test_proc *proc)
{
  if (proc->_writing)
    {
      /* ... */
    }

  proc->_gets += 1;
}

void
test_put_page (test_proc *proc)
{
  bufmgr_blkput (proc->_bref._frame);
}

void
test_get_page (test_proc *proc)
{
  int ret;
  bm_blockid blkid;

  if (proc->_scanpgs > 0)
    {
      proc->_blkno         += 1;
      proc->_scanpgs       -= 1;

      _the_sim._scan_count += 1;
    }
  else
    {
      u_int32_t total_blocks;

      proc->_file = & _the_sim._files[zipf_choose_elt (_the_sim._file_zipf)];

      total_blocks = proc->_file->_block_count;

      if ((sys_drand () * total_blocks) < SCAN_PROB)
	{
	  proc->_scanpgs = total_blocks;
	  proc->_blkno   = proc->_file->_block_offset;

	  _the_sim._scan_count += 1;
	}
      else
	{
	  proc->_blkno = proc->_file->_block_offset + sys_lrand (total_blocks);

	  _the_sim._noscan_count += 1;
	}
    }

  _the_sim._get_count += 1;

  if ((proc->_writing = (sys_drand () < WRITE_PROB)))
    {
      _the_sim._write_count += 1;
    }
  else
    {
      _the_sim._read_count += 1;
    }

  blkid._super = & _the_sim._super;
  blkid._blkno = proc->_blkno;

  if ((ret = bufmgr_blkget (& blkid, & proc->_bref)))
    {
      die ("blkget failed");
    }

  /* @@ because of changes for block_capture */
  spin_unlock (& proc->_bref._frame->_frame_lock);

  test_use_page   (proc);
  test_put_page   (proc);
}

void*
test_handler (void* arg)
{
  test_proc *proc = (test_proc*) arg;

  spin_lock (& _the_sim._barrier_lock);

  _the_sim._barrier_count += 1;

  wait_queue_sleep (& _the_sim._barrier_wait, 0);

  while (_the_sim._get_count < LARGE_NUMBER)
    {
      test_get_page (proc);
    }

  return NULL;
}

void
test_init ()
{
  int i;
  u_int32_t mean_blocks = (BLOCK_COUNT / FILE_COUNT)-1;

  _the_sim._file_zipf   = zipf_permute_table  (FILE_COUNT, FILE_ZIPF_ALPHA);
  _the_sim._procs       = (test_proc*) calloc (sizeof (test_proc), PROC_COUNT);
  _the_sim._files       = (test_file*) calloc (sizeof (test_file), FILE_COUNT);

  _the_sim._super.s_blocksize   = (1<<4);
  _the_sim._super.s_block_count = 0;

  spin_lock_init       (& _the_sim._barrier_lock);
  wait_queue_head_init (& _the_sim._barrier_wait, & _the_sim._barrier_lock);

  bufmgr_init (BUFFER_COUNT, HASH_FILL);

  iosched_init ();

  for (i = 0; i < FILE_COUNT; i += 1)
    {
      _the_sim._files[i]._block_count   = 1 + sys_erand (mean_blocks);
      _the_sim._files[i]._block_offset  = _the_sim._super.s_block_count;
      _the_sim._super.s_block_count    += _the_sim._files[i]._block_count;
    }

  for (i = 0; i < PROC_COUNT; i += 1)
    {
      test_proc *proc = & _the_sim._procs[i];

      proc->_pid = i;

      pthread_create (& proc->_pthread, NULL, test_handler, proc);
    }
}

int
main(int argc, char **argv)
{
  double tot_prob = WRITE_SCAN_PROB + READ_SCAN_PROB + WRITE_RAND_PROB + READ_RAND_PROB;
  int i;

  assert ("jmacd-13", tot_prob == 1.0);

  sys_rand_init ();
  test_init ();

  for (;;)
    {
      int cnt;

      spin_lock (& _the_sim._barrier_lock);

      cnt = _the_sim._barrier_count;

      spin_unlock (& _the_sim._barrier_lock);

      if (cnt == PROC_COUNT)
	{
	  break;
	}

      usleep (50);
    }

  rdtscll (_the_sim._start_cycle);

  spin_lock (& _the_sim._barrier_lock);

  wait_queue_broadcast (& _the_sim._barrier_wait);

  spin_unlock (& _the_sim._barrier_lock);

  for (i = 0; i < PROC_COUNT; i += 1)
    {
      test_proc *proc = & _the_sim._procs[i];
      void      *rval;

      pthread_join (proc->_pthread, & rval);
    }

  rdtscll (_the_sim._end_cycle);

  test_stat ();

  return 0;
}
