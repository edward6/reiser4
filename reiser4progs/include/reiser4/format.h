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

extern reiser4_format_t *reiser4_format_open(aal_device_t *device, 
    reiser4_id_t pid);

extern reiser4_format_t *reiser4_format_reopen(reiser4_format_t *format, 
    aal_device_t *device);

extern errno_t reiser4_format_valid(reiser4_format_t *format, 
    int flags);

extern void reiser4_format_close(reiser4_format_t *format);
extern int reiser4_format_confirm(reiser4_format_t *format);
extern blk_t reiser4_format_get_root(reiser4_format_t *format);
extern count_t reiser4_format_get_len(reiser4_format_t *format);
extern count_t reiser4_format_get_free(reiser4_format_t *format);
extern uint16_t reiser4_format_get_height(reiser4_format_t *format);

#ifndef ENABLE_COMPACT

extern errno_t reiser4_format_sync(reiser4_format_t *format);

extern reiser4_format_t *reiser4_format_create(aal_device_t *device,
    count_t len, uint16_t drop_policy, reiser4_id_t pid);

extern void reiser4_format_set_root(reiser4_format_t *format, blk_t root);
extern void reiser4_format_set_len(reiser4_format_t *format, count_t blocks);
extern void reiser4_format_set_free(reiser4_format_t *format, count_t blocks);
extern void reiser4_format_set_height(reiser4_format_t *format, uint8_t height);

extern errno_t reiser4_format_mark(reiser4_format_t *format, 
    reiser4_alloc_t *alloc);

extern errno_t reiser4_format_mark_journal(reiser4_format_t *format, 
    reiser4_alloc_t *alloc);

#endif

extern const char *reiser4_format_name(reiser4_format_t *format);
extern blk_t reiser4_format_offset(reiser4_format_t *format);

extern reiser4_id_t reiser4_format_journal_pid(reiser4_format_t *format);
extern reiser4_id_t reiser4_format_alloc_pid(reiser4_format_t *format);
extern reiser4_id_t reiser4_format_oid_pid(reiser4_format_t *format);

#endif

