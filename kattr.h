/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to sysfs' attributes. See kattr.c for comments */

#if !defined( __REISER4_KATTR_H__ )
#define __REISER4_KATTR_H__

extern int reiser4_sysfs_init_all(void);
extern void reiser4_sysfs_done_all(void);

extern int  reiser4_sysfs_init(struct super_block *super);
extern void reiser4_sysfs_done(struct super_block *super);

/* __REISER4_KATTR_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
