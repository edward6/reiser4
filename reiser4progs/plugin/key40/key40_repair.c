/*
    key40_repair.c -- reiser4 default key plugin recovery methods.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <aal/aal.h>
#include <aux/aux.h>

#include "key40.h"

extern const char *key40_m2n(key40_minor_t type);

errno_t key40_print(reiser4_body_t *body, char *buff, 
    uint32_t n, uint16_t options) 
{
    key40_t *key = (key40_t *)body;
    
    aal_assert("vpf-191", key != NULL, return -1);

    if (!buff) return -1;

    reiser4_aux_strcat(buff, n, "[key40: %llu:%u:%llu:%llu:%llu %s]", 
	k40_get_locality(key), k40_get_minor(key),  k40_get_band(key),
	k40_get_objectid(key), k40_get_offset(key), key40_m2n(k40_get_minor(key)));

    return 0;
}

