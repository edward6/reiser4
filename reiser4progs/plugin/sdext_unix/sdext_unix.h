/*
    sdext_unix.h -- stat data exception plugin, that implements unix stat data 
    fields.
    
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef SDEXT_UNIX_H
#define SDEXT_UNIX_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

struct sdext_unix {
    d32_t uid;
    d32_t gid;
    d32_t atime;
    d32_t mtime;
    d32_t ctime;
    d32_t rdev;
    d64_t bytes;
};

typedef struct sdext_unix sdext_unix_t;

#define sdext_unix_get_uid(ext)		aal_get_le32(ext, uid)
#define sdext_unix_set_uid(ext, val)	aal_set_le32(ext, uid, val)

#define sdext_unix_get_gid(ext)		aal_get_le32(ext, gid)
#define sdext_unix_set_gid(ext, val)	aal_set_le32(ext, gid, val)

#define sdext_unix_get_atime(ext)	aal_get_le32(ext, atime)
#define sdext_unix_set_atime(ext, val)	aal_set_le32(ext, atime, val)

#define sdext_unix_get_mtime(ext)	aal_get_le32(ext, mtime)
#define sdext_unix_set_mtime(ext, val)	aal_set_le32(ext, mtime, val)

#define sdext_unix_get_ctime(ext)	aal_get_le32(ext, ctime)
#define sdext_unix_set_ctime(ext, val)	aal_set_le32(ext, ctime, val)

#define sdext_unix_get_rdev(ext)	aal_get_le32(ext, rdev)
#define sdext_unix_set_rdev(ext, val)	aal_set_le32(ext, rdev, val)

#define sdext_unix_get_bytes(ext)	aal_get_le64(ext, bytes)
#define sdext_unix_set_bytes(ext, val)	aal_set_le64(ext, bytes, val)

#endif

