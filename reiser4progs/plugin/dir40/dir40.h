/*
    dir40.h -- reiser4 hashed directory plugin structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DIR40_H
#define DIR40_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

struct dir40_item {
    reiser4_body_t *body;
    reiser4_plugin_t *plugin;
};

typedef struct dir40_item dir40_item_t;

/* Compaund directory structure */
struct dir40 {
    reiser4_plugin_t *plugin;
    
    /* 
	Poiter to the instance of internal libreiser4 tree, dir opened on 
	stored here for lookup and modiying purposes. It is passed by reiser4 
	library durring initialization of the directory instance.
    */
    const void *tree;

    /* 
	The key of stat data (or just first item if stat data doesn't exists) 
	for this directory.
    */
    reiser4_key_t key;

    /* Coords of stat data are stored here */
    reiser4_place_t place;

    /* 
	Statdata item of the dir. It is used for passing it to statdata plugin 
	in order to get or set someone field.
    */
    dir40_item_t statdata;

    /* Current direntry item */
    dir40_item_t direntry;
    
    /* Current position in the directory */
    uint32_t pos;

    /* Hash plugin in use */
    reiser4_plugin_t *hash;
};

typedef struct dir40 dir40_t;

#endif

