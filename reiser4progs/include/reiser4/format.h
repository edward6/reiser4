/*
    format.h -- format's functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FORMAT_H
#define FORMAT_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/plugin.h>
#include <reiser4/filesystem.h>

extern reiserfs_format_t *reiserfs_format_open(aal_device_t *device, 
    reiserfs_id_t pid);

extern reiserfs_format_t *reiserfs_format_reopen(reiserfs_format_t *format, 
    aal_device_t *device);

extern void reiserfs_format_close(reiserfs_format_t *format);
extern int reiserfs_format_confirm(reiserfs_format_t *format);
extern blk_t reiserfs_format_get_root(reiserfs_format_t *format);
extern count_t reiserfs_format_get_blocks(reiserfs_format_t *format);
extern count_t reiserfs_format_get_free(reiserfs_format_t *format);
extern uint16_t reiserfs_format_get_height(reiserfs_format_t *format);

#ifndef ENABLE_COMPACT

extern errno_t reiserfs_format_sync(reiserfs_format_t *format);
extern errno_t reiserfs_format_check(reiserfs_format_t *format, int flags);

extern reiserfs_format_t *reiserfs_format_create(aal_device_t *device,
    count_t len, uint16_t tail_policy, reiserfs_id_t pid);

extern void reiserfs_format_set_root(reiserfs_format_t *format, blk_t root);
extern void reiserfs_format_set_blocks(reiserfs_format_t *format, count_t blocks);
extern void reiserfs_format_set_free(reiserfs_format_t *format, count_t blocks);
extern void reiserfs_format_set_height(reiserfs_format_t *format, uint16_t height);

#endif

extern const char *reiserfs_format_format(reiserfs_format_t *format);
extern blk_t reiserfs_format_offset(reiserfs_format_t *format);

extern reiserfs_id_t reiserfs_format_journal_pid(reiserfs_format_t *format);
extern reiserfs_id_t reiserfs_format_alloc_pid(reiserfs_format_t *format);
extern reiserfs_id_t reiserfs_format_oid_pid(reiserfs_format_t *format);

#endif

