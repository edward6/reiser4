/*
    key.h -- reiserfs key structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef KEY_H
#define KEY_H

#include <aal/aal.h>

/* maximal possible key size */
#define MAX_KEY_SIZE 24

/*
    FIXME-VITALY: We should change these names to plugin independent
    style. Pass these names to key plugin where they will be converted
    to plugin-specific names.
*/
typedef enum {
    /* File name key type */
    KEY40_FILE_NAME_MINOR = 0,
    /* Stat-data key type */
    KEY40_SD_MINOR	  = 1,
    /* File attribute name */
    KEY40_ATTR_NAME_MINOR = 2,
    /* File attribute value */
    KEY40_ATTR_BODY_MINOR = 3,
    /* File body (tail or extent) */
    KEY40_BODY_MINOR	  = 4
} reiserfs_key40_minor;

#endif

