/*
    key40_repair.c -- reiser4 default key plugin recovery methods.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>
#include <comm/misc.h>

#include "key40.h"

static const char *__type_name(unsigned int key_type) {
    switch(key_type) {
        case KEY40_FILENAME_MINOR:
            return "file name";
        case KEY40_STATDATA_MINOR:
            return "stat data";
        case KEY40_ATTRNAME_MINOR:
            return "attr name";
        case KEY40_ATTRBODY_MINOR:
            return "attr body";
        case KEY40_BODY_MINOR:
            return "file body";
        default:
            return "unknown";
    }
}


errno_t key40_print(reiser4_body_t *body, char *buff, 
    uint32_t n, uint16_t options) 
{
    key40_t *key = (key40_t *)body;
    
    aal_assert("vpf-191", key != NULL, return -1);

    if (!buff) return -1;

    reiser4_comm_strcat(buff, n, "[key40: %llu:%u:%llu:%llu:%llu %s]", 
	k40_get_locality(key), k40_get_type(key),  k40_get_band(key),
	k40_get_objectid(key), k40_get_offset(key), __type_name(k40_get_type(key)));

    return 0;
}

