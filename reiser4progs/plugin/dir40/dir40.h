/*
    dir40.h -- reiser4 hashed directory plugin structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DIR40_H
#define DIR40_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

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
    struct {
	void *data;
	uint32_t len;
    } statdata;

    /* Reference to the current direntry item */
    void *direntry;
    
    /* Current position in the directory */
    uint32_t pos;

    reiser4_plugin_t *statdata_plugin;
    reiser4_plugin_t *direntry_plugin;
    reiser4_plugin_t *hash_plugin;
};

typedef struct dir40 dir40_t;

#endif

