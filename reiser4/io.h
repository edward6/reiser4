/* -*- c -*- */

/* fs/reiser4/io.h */

/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/* 
 * Interface to io.
 */

/* $Id$ */

#if !defined( __REISER4_IO_H__ )
#define __REISER4_IO_H__

/** structrure used as handle to do io in reiser4. Think of it as
    device. Later it can be expanded to accomodate for multiple devices,
    networking whatever. */
typedef struct reiser4_io {
	reiser4_disk_addr min_block;
	reiser4_disk_addr max_block;
} reiser4_io;

extern int block_nr_is_correct( reiser4_disk_addr *block, reiser4_io *io );

/* __REISER4_IO_H__ */
#endif

/*
 * $Log$
 * Revision 4.3  2001/10/14 21:39:05  reiser
 * It really annoys me when you add layers of abstraction without bothering to have a seminar first.
 *
 * Revision 4.2  2001/10/05 08:50:46  god
 * corrections after compilation in user-level with stricter
 * compiler checks.
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 4.1  2001/09/27 18:30:12  god
 * moving toward compilability. Please hold on.
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 */
/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
