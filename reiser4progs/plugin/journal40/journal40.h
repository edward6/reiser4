/*
    journal40.h -- reiser4 default journal plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef JOURNAL40_H
#define JOURNAL40_h

#include <aal/aal.h>

#define REISERFS_JOURNAL40_HEADER   (4096 * 19)
#define REISERFS_JOURNAL40_FOOTER   (4096 * 20)

struct reiserfs_journal40 {
    aal_device_t *device;

    aal_block_t *header;
    aal_block_t *footer;
};

typedef struct reiserfs_journal40 reiserfs_journal40_t;

struct reiserfs_journal40_header {
    uint64_t jh_last_commited;
};

typedef struct reiserfs_journal40_header reiserfs_journal40_header_t;

#define get_jh_last_commited(jh)		aal_get_le64(jh, jh_last_commited)
#define set_jh_last_commited(jh, val)		aal_set_le64(jh, jh_last_commited, val)

struct reiserfs_journal40_footer {
    uint64_t jf_last_flushed;
};

typedef struct reiserfs_journal40_footer reiserfs_journal40_footer_t;

#define get_jf_last_flushed(jf)			aal_get_le64(jf, jf_last_flushed)
#define set_jf_last_flushed(jf, val)		aal_set_le64(jf, jf_last_flushed, val)

#endif

