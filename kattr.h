/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to sysfs' attributes. See kattr.c for comments */

#if !defined( __REISER4_KATTR_H__ )
#define __REISER4_KATTR_H__

#include <linux/types.h>
#include <linux/sysfs.h>

/* XXX make this CONFIG option */
#define REISER4_USE_SYSFS (1)

#define KATTR_LEFT(p, buf) (PAGE_SIZE - (p - buf) - 1)
#define KATTR_PRINT(p, buf, ...)				\
({ 								\
	p += snprintf(p, KATTR_LEFT(p, buf) , ## __VA_ARGS__); 	\
})

struct super_block;
struct reiser4_kattr;
typedef struct reiser4_kattr reiser4_kattr;

struct reiser4_kattr {
	struct attribute attr;
	void  *cookie;
	ssize_t (*show) (struct super_block * s,
			 reiser4_kattr *, void *opaque, char *buf);
	ssize_t (*store) (struct super_block * s,
			  reiser4_kattr *, void *opaque, const char *buf,
			  size_t size);
};

extern int reiser4_sysfs_init_once(void);
extern void reiser4_sysfs_done_once(void);

extern int  reiser4_sysfs_init(struct super_block *super);
extern void reiser4_sysfs_done(struct super_block *super);

extern struct kobj_type ktype_reiser4;

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
