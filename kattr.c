/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Interface to sysfs' attributes */

/*
 * Reiser4 exports some of its internal data through sysfs.
 *
 * For details on sysfs see fs/sysfs, include/linux/sysfs.h,
 * include/linux/kobject.h. Roughly speaking, one embeds struct kobject into
 * some kernel data type. Objects of this type will be represented as
 * _directories_ somewhere below /sys. Attributes can be registered for
 * kobject and they will be visible as files within corresponding
 * directory. Each attribute is represented by struct kattr. How given
 * attribute reacts to read and write is determined by ->show and ->store
 * operations that are properties of its parent kobject.
 *
 * Reiser4 exports following stuff through sysfs:
 *
 *    path                                              kobject or attribute
 *
 * /sys/fs/reiser4/
 *                 <dev>/                               sbinfo->kobj
 *                       sb-fields                      def_attrs[]
 *                       stats/                         sbinfo->stats_kobj
 *                             stat-cnts                reiser4_stat_defs[]
 *                             level-NN/                sbinfo->level[].kobj
 *                                      stat-cnts       reiser4_stat_level_defs[]
 *
 * (For some reasons we also add /sys/fs and /sys/fs/reiser4 manually, but
 * this is supposed to be done by core.)
 *
 * Shouldn't struct kobject be renamed to struct knobject?
 *
 */

#include "debug.h"
#include "super.h"
#include "kattr.h"
#include "prof.h"

#include <linux/kobject.h>     /* struct kobject */
#include <linux/fs.h>          /* struct super_block */

#if REISER4_USE_SYSFS

/* convert @attr to reiser4_kattr object it is embedded in */
typedef struct {
	ptrdiff_t   offset;
	const char *format;
} super_field_cookie;

#define DEFINE_SUPER_RO(aname, afield, aformat, asize)		\
static super_field_cookie __cookie_ ## aname = {		\
	.offset = offsetof(reiser4_super_info_data, afield),	\
	.format = aformat "\n"					\
};								\
								\
static reiser4_kattr kattr_super_ro_ ## aname = {		\
	.attr = {						\
		.kattr = {					\
			.name = (char *) #afield,		\
			.mode = 0440   /* r--r----- */		\
		},						\
		.show = show_ro_ ## asize			\
	},							\
	.cookie = &__cookie_ ## aname				\
}

#define getat(ptr, offset, type) *(type *)(((char *)(ptr)) + (offset))

static inline void *
getcookie(struct fs_kattr *attr)
{
	return container_of(attr, reiser4_kattr, attr)->cookie;
}

static ssize_t
show_ro_32(struct super_block * s,
	   struct fs_kobject *o, struct fs_kattr * kattr, char * buf)
{
	char *p;
	super_field_cookie *cookie;
	__u32 val;

	cookie = getcookie(kattr);
	val = getat(get_super_private(s), cookie->offset, __u32);
	p = buf;
	KATTR_PRINT(p, buf, cookie->format, (unsigned long long)val);
	return (p - buf);
}

static ssize_t show_ro_64(struct super_block * s, struct fs_kobject *o,
			  struct fs_kattr * kattr, char * buf)
{
	char *p;
	super_field_cookie *cookie;
	__u64 val;

	cookie = getcookie(kattr);
	val = getat(get_super_private(s), cookie->offset, __u64);
	p = buf;
	KATTR_PRINT(p, buf, cookie->format, (unsigned long long)val);
	return (p - buf);
}

#undef getat

#define SHOW_OPTION(p, buf, option)			\
	if (option)					\
		KATTR_PRINT((p), (buf), #option "\n")

static ssize_t
show_options(struct super_block * s,
	     struct fs_kobject *o, struct fs_kattr * kattr, char * buf)
{
	char *p;

	p = buf;

	SHOW_OPTION(p, buf, REISER4_DEBUG);
	SHOW_OPTION(p, buf, REISER4_DEBUG_MODIFY);
	SHOW_OPTION(p, buf, REISER4_DEBUG_MEMCPY);
	SHOW_OPTION(p, buf, REISER4_DEBUG_NODE);
	SHOW_OPTION(p, buf, REISER4_ZERO_NEW_NODE);
	SHOW_OPTION(p, buf, REISER4_TRACE);
	SHOW_OPTION(p, buf, REISER4_LOG);
	SHOW_OPTION(p, buf, REISER4_STATS);
	SHOW_OPTION(p, buf, REISER4_DEBUG_OUTPUT);
	SHOW_OPTION(p, buf, REISER4_LOCKPROF);
	SHOW_OPTION(p, buf, REISER4_LARGE_KEY);
	SHOW_OPTION(p, buf, REISER4_PROF);
	SHOW_OPTION(p, buf, REISER4_COPY_ON_CAPTURE);
	return (p - buf);
}

static reiser4_kattr compile_options = {
	.attr = {
		.kattr = {
			 .name = (char *) "options",
			 .mode = 0444   /* r--r--r-- */
		 },
		.show = show_options,
	},
	.cookie = NULL
};

static ssize_t
show_device(struct super_block * s,
	    struct fs_kobject *o, struct fs_kattr * kattr, char * buf)
{
	char *p;

	p = buf;
	KATTR_PRINT(p, buf, "%lu\n", (unsigned long)s->s_dev);
	return (p - buf);
}

static reiser4_kattr device = {
	.attr = {
		.kattr = {
			 .name = (char *) "device",
			 .mode = 0444   /* r--r--r-- */
		 },
		.show = show_device,
	},
	.cookie = NULL
};

/* this is used to define similar reiser4_kattr's: log_flags and trace_flags */
#define DEFINE_KATTR_FLAGS(XXX)						\
static ssize_t								\
show_##XXX##_flags(struct super_block * s,				\
		   struct fs_kobject *o, struct fs_kattr * kattr, char * buf) \
{									\
	char *p;							\
									\
	p = buf;							\
	KATTR_PRINT(p, buf, "%#x\n", get_super_private(s)-> XXX##_flags); \
	return (p - buf);						\
}									\
									\
ssize_t store_##XXX##_flags(struct super_block * s, struct fs_kobject *fsko, \
			    struct fs_kattr *ka, const char *buf,	\
			    size_t size)				\
{									\
	__u32 flags;							\
									\
	if (sscanf(buf, "%i", &flags) == 1)				\
		get_super_private(s)-> XXX##_flags = flags;		\
	else								\
		size = RETERR(-EINVAL);					\
	return size;							\
}									\
									\
static reiser4_kattr XXX##_flags = {					\
	.attr = {							\
		.name = (char *) #XXX "_flags",				\
		.mode = 0644   /* rw-r--r-- */				\
	},								\
	.cookie = NULL,							\
	.store = store_##XXX##_flags,					\
	.show  = show_##XXX##_flags					\
};

DEFINE_KATTR_FLAGS(log);
DEFINE_KATTR_FLAGS(trace);

#if REISER4_DEBUG
ssize_t store_bugme(struct super_block * s, struct fs_kobject *o,
		    struct fs_kattr *ka, const char *buf, size_t size)
{
	DEBUGON(1);
	return size;
}

static reiser4_kattr bugme = {
	.attr = {
		.kattr = {
			 .name = (char *) "bugme",
			 .mode = 0222   /* -w--w--w- */
		 },
		.store = store_bugme,
	},
	.cookie = NULL
};

/* REISER4_DEBUG */
#endif

DEFINE_SUPER_RO(01, mkfs_id, "%llx", 32);
DEFINE_SUPER_RO(02, block_count, "%llu", 64);
DEFINE_SUPER_RO(03, blocks_used, "%llu", 64);
DEFINE_SUPER_RO(04, blocks_free_committed, "%llu", 64);
DEFINE_SUPER_RO(05, blocks_grabbed, "%llu", 64);
DEFINE_SUPER_RO(06, blocks_fake_allocated_unformatted, "%llu", 64);
DEFINE_SUPER_RO(07, blocks_fake_allocated, "%llu", 64);
DEFINE_SUPER_RO(08, blocks_flush_reserved, "%llu", 64);
DEFINE_SUPER_RO(09, fsuid, "%llx", 32);
#if REISER4_DEBUG
DEFINE_SUPER_RO(10, eflushed, "%llu", 32);
#endif
DEFINE_SUPER_RO(11, blocknr_hint_default, "%lli", 64);
DEFINE_SUPER_RO(12, nr_files_committed, "%llu", 64);
DEFINE_SUPER_RO(13, tmgr.atom_count, "%llu", 32);
DEFINE_SUPER_RO(14, tmgr.id_count, "%llu", 32);
DEFINE_SUPER_RO(15, tmgr.atom_max_size, "%llu", 32);
DEFINE_SUPER_RO(16, tmgr.atom_max_age, "%llu", 32);

/* tree fields */
DEFINE_SUPER_RO(17, tree.root_block, "%llu", 64);
DEFINE_SUPER_RO(18, tree.height, "%llu", 32);
DEFINE_SUPER_RO(19, tree.znode_epoch, "%llu", 64);
DEFINE_SUPER_RO(20, tree.carry.new_node_flags, "%llx", 32);
DEFINE_SUPER_RO(21, tree.carry.new_extent_flags, "%llx", 32);
DEFINE_SUPER_RO(22, tree.carry.paste_flags, "%llx", 32);
DEFINE_SUPER_RO(23, tree.carry.insert_flags, "%llx", 32);

/* not very good. Should be done by the plugin in stead */
DEFINE_SUPER_RO(24, next_to_use, "%llu", 64);
DEFINE_SUPER_RO(25, oids_in_use, "%llu", 64);

DEFINE_SUPER_RO(26, entd.flushers, "%llu", 32);
DEFINE_SUPER_RO(27, entd.timeout, "%llu", 32);

#define ATTR_NO(n) &kattr_super_ro_ ## n .attr.kattr

static struct attribute * kattr_def_attrs[] = {
	ATTR_NO(01),
	ATTR_NO(02),
	ATTR_NO(03),
	ATTR_NO(04),
	ATTR_NO(05),
	ATTR_NO(06),
	ATTR_NO(07),
	ATTR_NO(08),
	ATTR_NO(09),
#if REISER4_DEBUG
	ATTR_NO(10),
#endif
	ATTR_NO(11),
	ATTR_NO(12),
	ATTR_NO(13),
	ATTR_NO(14),
	ATTR_NO(15),
	ATTR_NO(16),
	ATTR_NO(17),
	ATTR_NO(18),
	ATTR_NO(19),
	ATTR_NO(20),
	ATTR_NO(21),
	ATTR_NO(22),
	ATTR_NO(23),
	ATTR_NO(24),
	ATTR_NO(25),
	ATTR_NO(26),
	ATTR_NO(27),
	&compile_options.attr.kattr,
	&device.attr.kattr,
	&trace_flags.attr.kattr,
	&log_flags.attr.kattr,
#if REISER4_DEBUG
	&bugme.attr.kattr,
#endif
	NULL
};

struct kobj_type ktype_reiser4 = {
	.sysfs_ops	= &fs_attr_ops,
	.default_attrs	= kattr_def_attrs,
	.release	= NULL
};

#if REISER4_STATS

static struct kobj_type ktype_noattr = {
	.sysfs_ops	= &fs_attr_ops,
	.default_attrs	= NULL,
	.release        = NULL
};

static int register_level_attrs(reiser4_super_info_data *sbinfo, int i)
{
	struct fs_kobject *parent;
	struct fs_kobject *level;
	int result;

	parent = &sbinfo->stats_kobj;
	sbinfo->level[i].level = i;
	level = &sbinfo->level[i].kobj;
	level->kobj.parent = kobject_get(&parent->kobj);
	if (level->kobj.parent != NULL) {
		snprintf(level->kobj.name, KOBJ_NAME_LEN, "level-%2.2i", i);
		level->kobj.ktype = &ktype_noattr;
		result = fs_kobject_register(sbinfo->tree.super, level);
		if (result == 0)
			result = reiser4_populate_kattr_level_dir(&level->kobj);
	} else
		result = RETERR(-EBUSY);
	return result;
}
#endif

static decl_subsys(fs, NULL, NULL);
decl_subsys(reiser4, &ktype_reiser4, NULL);

reiser4_internal int
reiser4_sysfs_init_once(void)
{
	int result;

	result = subsystem_register(&fs_subsys);
	if (result == 0) {
		kset_set_kset_s(&reiser4_subsys, fs_subsys);
		result = subsystem_register(&reiser4_subsys);
		if (result == 0)
			result = init_prof_kobject();
	}
	return result;
}

reiser4_internal void
reiser4_sysfs_done_once(void)
{
	subsystem_unregister(&reiser4_subsys);
	subsystem_unregister(&fs_subsys);
	done_prof_kobject();
}

reiser4_internal int
reiser4_sysfs_init(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	struct fs_kobject *kobj;
	int result;
	ON_STATS(struct fs_kobject *stats_kobj);

	sbinfo = get_super_private(super);

	kobj = &sbinfo->kobj;

	snprintf(kobj->kobj.name, KOBJ_NAME_LEN, "%s", super->s_id);
	kobj_set_kset_s(&sbinfo->kobj, reiser4_subsys);
	result = fs_kobject_register(super, kobj);
	if (result != 0)
		return result;
#if REISER4_STATS
	/* add attributes representing statistical counters */
	stats_kobj = &sbinfo->stats_kobj;
	stats_kobj->kobj.parent = kobject_get(&kobj->kobj);
	snprintf(stats_kobj->kobj.name, KOBJ_NAME_LEN, "stats");
	stats_kobj->kobj.ktype = &ktype_noattr;
	result = fs_kobject_register(super, stats_kobj);
	if (result != 0)
		return result;
	result = reiser4_populate_kattr_dir(&stats_kobj->kobj);
	if (result == 0) {
		int i;

		for (i = 0; i < sizeof_array(sbinfo->level); ++i) {
			result = register_level_attrs(sbinfo, i);
			if (result != 0)
				break;
		}
	}
#else
	result = reiser4_populate_kattr_dir(kobj);
#endif

	return result;
}

reiser4_internal void
reiser4_sysfs_done(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	ON_STATS(int i);

	sbinfo = get_super_private(super);
#if REISER4_STATS
	for (i = 0; i < sizeof_array(sbinfo->level); ++i)
		fs_kobject_unregister(&sbinfo->level[i].kobj);
	fs_kobject_unregister(&sbinfo->stats_kobj);
#endif
	fs_kobject_unregister(&sbinfo->kobj);
}

/* REISER4_USE_SYSFS */
#else

reiser4_internal int
reiser4_sysfs_init(struct super_block *super)
{
	return 0;
}

reiser4_internal void
reiser4_sysfs_done(struct super_block *super)
{}

reiser4_internal int
reiser4_sysfs_init_once(void)
{
	return 0;
}

reiser4_internal void
reiser4_sysfs_done_once(void)
{}

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
