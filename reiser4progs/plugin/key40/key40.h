/*
    key40.h -- reiser4 default key structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef KEY40_H
#define KEY40_H

#include <aal/aal.h>
#include <reiser4/plugin.h>
#include <reiser4/key.h>

typedef enum {
    /* Major "locale", aka dirid. Sits in 1st element */
    KEY40_LOCALITY_INDEX  = 0,
    /* Minor "locale", aka item type. Sits in 1st element */
    KEY40_TYPE_INDEX	  = 0,
    /* Object band. Sits in 2nd element */
    KEY40_BAND_INDEX	  = 1,
    /* Object id. Sits in 2nd element */
    KEY40_OBJECTID_INDEX  = 1,
    /* Offset. Sits in 3rd element */
    KEY40_OFFSET_INDEX	  = 2,
    /* Name hash. Sits in 3rd element */
    KEY40_HASH_INDEX	  = 2,
    KEY40_LAST_INDEX	  = 3
} key40_field_t;

union key40 {
    uint64_t el[KEY40_LAST_INDEX];
    int pad;
};

typedef union key40 key40_t;

typedef enum {
    /* 
	Major locality occupies higher 60 bits of 
	the first element.
    */
    KEY40_LOCALITY_MASK    = 0xfffffffffffffff0ull,
    /* 
	Minor locality occupies lower 4 bits of 
	the first element.
    */
    KEY40_TYPE_MASK        = 0xfull,
    /* 
	Controversial band occupies higher 4 bits 
	of the 2nd element.
    */
    KEY40_BAND_MASK        = 0xf000000000000000ull,
    /* 
	Objectid occupies lower 60 bits of the 2nd 
	element.
    */
    KEY40_OBJECTID_MASK    = 0x0fffffffffffffffull,
    
    /* Offset is just 3rd L.M.Nt itself */
    KEY40_OFFSET_MASK      = 0xffffffffffffffffull,
    
    /* Hash occupies 56 higher bits of 3rd element */
    KEY40_HASH_MASK        = 0xffffffffffffff00ull,
    
    /* 
	Generation counter occupies lower 8 bits of 
	3rd element.
    */
    KEY40_GEN_MASK         = 0xffull,
} key40_mask_t;

#define OID_CHARS (sizeof(uint64_t) - 1)

typedef enum {
    KEY40_LOCALITY_SHIFT   = 4,
    KEY40_TYPE_SHIFT       = 0,
    KEY40_BAND_SHIFT       = 60,
    KEY40_OBJECTID_SHIFT   = 0,
    KEY40_OFFSET_SHIFT     = 0,
    KEY40_HASH_SHIFT       = 8,
    KEY40_GEN_SHIFT        = 0,
} key40_shift_t;

static inline uint64_t k40_get_el(const key40_t *key,
    key40_field_t off)
{
    aal_assert("vpf-029", key != NULL,  return 0);
    aal_assert("vpf-030", off < KEY40_LAST_INDEX, return 0);
    return LE64_TO_CPU(key->el[off]);
}

static inline void k40_set_el(key40_t *key,
    key40_field_t off, uint64_t value)
{
    aal_assert("vpf-031", key != NULL, return);
    aal_assert("vpf-032", off < KEY40_LAST_INDEX, return);
    key->el[off] = CPU_TO_LE64(value);
}

static inline int k40_comp_el(void *k1, void *k2, int off) {
    uint64_t e1;
    uint64_t e2;

    e1 = k40_get_el(k1, off);
    e2 = k40_get_el(k2, off);

    return (e1 < e2 ? -1 : (e1 == e2 ? 0 : 1));
}

/* 
    Macro to define getter and setter functions for 
    field F with type T.
*/

#define DECLARE_KEY40_FIELD(L, U, T)				    \
extern inline T k40_get_##L (const key40_t *key);		    \
extern inline void k40_set_##L(key40_t *key, T loc);
								    
#define DEFINE_KEY40_FIELD(L, U, T)				    \
static inline T k40_get_##L (const key40_t *key) {		    \
    aal_assert("vpf-036", key != NULL, return 0);		    \
    return (T) ((k40_get_el(key, KEY40_##U##_INDEX) &		    \
	KEY40_##U##_MASK) >> KEY40_##U##_SHIFT);		    \
}								    \
								    \
static inline void k40_set_##L(key40_t *key, T loc) {		    \
    uint64_t el;						    \
								    \
    aal_assert("vpf-033", key != NULL, return);			    \
								    \
    el = k40_get_el(key, KEY40_##U##_INDEX);			    \
								    \
    el &= ~KEY40_##U##_MASK;					    \
								    \
    aal_assert("vpf-034", ((loc << KEY40_##U##_SHIFT) &		    \
        ~KEY40_##U##_MASK) == 0, return);			    \
								    \
    el |= (loc << KEY40_##U##_SHIFT);				    \
    k40_set_el(key, KEY40_##U##_INDEX, el);			    \
}

DEFINE_KEY40_FIELD(locality, LOCALITY, uint64_t);
DEFINE_KEY40_FIELD(type, TYPE, reiser4_key40_minor_t);
DEFINE_KEY40_FIELD(band, BAND, uint64_t);
DEFINE_KEY40_FIELD(objectid, OBJECTID, uint64_t);
DEFINE_KEY40_FIELD(offset, OFFSET, uint64_t);
DEFINE_KEY40_FIELD(hash, HASH, uint64_t);

#endif

