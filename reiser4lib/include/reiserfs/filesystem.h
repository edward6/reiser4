/*
	filesystem.h -- reiserfs filesystem structures and macroses.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <dal/dal.h>
#include <reiserfs/plugin.h>

struct reiserfs_super {
	reiserfs_layout_plugin_t *entity;
};

typedef struct reiserfs_super reiserfs_super_t;

struct reiserfs_journal {
	dal_t *dal;
	reiserfs_journal_plugin_t *entity;
};

typedef struct reiserfs_journal reiserfs_journal_t;

struct reiserfs_alloc {
	reiserfs_alloc_plugin_t *entity;
};

typedef struct reiserfs_alloc reiserfs_alloc_t;

struct reiserfs_tree {
};

typedef struct reiserfs_tree reiserfs_tree_t;

struct reiserfs_fs {
	dal_t *dal;
	reiserfs_super_t *super;
	reiserfs_journal_t *journal;
	reiserfs_alloc_t *alloc;
	reiserfs_tree_t *tree;
};

typedef struct reiserfs_fs reiserfs_fs_t;

#endif

