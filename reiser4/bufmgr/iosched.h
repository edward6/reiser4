/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#ifndef __REISER4_IOSCHED_H__
#define __REISER4_IOSCHED_H__

/* This file provides user-level versions of wait_queue_head_t, sleep_on, wake_up, etc.
 * Based on POSIX threads.  It will be straight-forward to port. */

typedef struct _usched_queue_head_t* wait_queue_head_t;

TS_LIST_DECLARE(iosched);

extern void	  wait_queue_head_init (wait_queue_head_t        *queue,
					spinlock_t               *lock);

extern int        wait_queue_isempty   (wait_queue_head_t const  *queue);

extern void       wait_queue_sleep     (wait_queue_head_t        *queue,
					int                       relock);

extern void       wait_queue_signal    (wait_queue_head_t        *queue);

extern void       wait_queue_broadcast (wait_queue_head_t        *queue);

extern void       iosched_init         (void);

extern u_int32_t  iosched_count        (void);

extern void       iosched_read         (znode                    *frame,
					bm_blockid const         *block);

extern void       iosched_write        (znode                    *frame,
					bm_blockid const         *block);

#endif /* __REISER4_IOSCHED_H__ */
