/*
    journal40.h -- reiser4 default journal plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef JOURNAL40_H
#define JOURNAL40_h

#include <aal/aal.h>
#include <reiser4/plugin.h>

struct journal40 {
    reiser4_plugin_t *plugin;
    reiser4_entity_t *format;
 
    aal_device_t *device;

    aal_block_t *header;
    aal_block_t *footer;
};

typedef struct journal40 journal40_t;

struct journal40_header {
    uint64_t jh_last_commited;
};

typedef struct journal40_header journal40_header_t;

#define get_jh_last_commited(jh)		aal_get_le64(jh, jh_last_commited)
#define set_jh_last_commited(jh, val)		aal_set_le64(jh, jh_last_commited, val)

struct journal40_footer {
    uint64_t jf_last_flushed;
};

typedef struct journal40_footer journal40_footer_t;

#define get_jf_last_flushed(jf)			aal_get_le64(jf, jf_last_flushed)
#define set_jf_last_flushed(jf, val)		aal_set_le64(jf, jf_last_flushed, val)

#endif

