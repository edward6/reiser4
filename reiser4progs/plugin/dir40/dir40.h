/*
    dir40.h -- reiser4 hashed directory plugin structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DIR40_H
#define DIR40_H

/* Compaund directory structure */
struct reiserfs_dir40 {
    /* 
	Poiter to the instance of internal libreiser4 b*tree, dir opened on stored here 
	for lookup and modiying purposes. It is passed by libreiser4 durring initialization
	of the directory instance.
    */
    const void *tree;

    /* 
	The key of stat data (or just first item if stat data doesn't exists) for this
	directory.
    */
    reiserfs_key_t key;

    /* Coords of stat data are stored here */
    reiserfs_place_t place;

    /* 
	Statdata item of the dir. It is used for passing it to statdata plugin in order to
	get or set someone field.
    */
    struct {
	void *data;
	uint32_t len;
    } statdata;

    /* Current position in the directory */
    uint32_t pos;

    reiserfs_plugin_t *statdata_plugin;
    reiserfs_plugin_t *direntry_plugin;
    reiserfs_plugin_t *hash_plugin;
};

typedef struct reiserfs_dir40 reiserfs_dir40_t;

#endif

