/*
	tree.h -- reiserfs balanced tree functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef TREE_H
#define TREE_H

#include <reiserfs/reiserfs.h>

#define REISERFS_NODE_LEAF		1
#define REISERFS_NODE_INTERNAL		2

struct reiserfs_node_head {
    unsigned int plugin_id;
};

typedef struct reiserfs_node_head reiserfs_node_head_t;

#define get_nh_plugin_id(nh)		get_le16(nh, plugin_id)
#define set_nh_plugin_id(nh, val)	set_le16(nh, plugin_id, val)

extern int reiserfs_tree_open(reiserfs_fs_t *fs);
extern int reiserfs_tree_create(reiserfs_fs_t *fs);
extern void reiserfs_tree_close(reiserfs_fs_t *fs, int sync);

#endif

