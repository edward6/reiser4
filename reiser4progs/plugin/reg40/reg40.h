/*
    dir40.h -- reiser4 hashed directory plugin structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef REG40_H
#define REG40_H

#include <aal/aal.h>
#include <reiser4/plugin.h>

struct reg40_item {
    reiser4_body_t *body;
    reiser4_plugin_t *plugin;
};

typedef struct reg40_item reg40_item_t;

/* Compaund directory structure */
struct reg40 {
    reiser4_plugin_t *plugin;
    
    /* 
	Poiter to the instance of internal libreiser4 tree, file opened on 
	stored here for lookup and modiying purposes. It is passed by reiser4 
	library durring initialization of the fileinstance.
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
    reg40_item_t statdata;

    /* Current position in the directory */
    uint64_t offset;
};

typedef struct reg40 reg40_t;

#endif

