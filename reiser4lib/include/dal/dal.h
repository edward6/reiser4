/*
	dal.h -- device abstraction API
	Copyright (C) 1996 - 2002 Hans Reiser.
*/

#ifndef DAL_H
#define DAL_H

#include <sys/stat.h>
#include <sys/types.h>

typedef unsigned long blk_t;

struct dal_ops;

struct dal {
	const void *dev;
	size_t blocksize;
	struct dal_ops *ops;
	
	int flags;
	void *data;
};

typedef struct dal dal_t;

struct dal_ops {
	int (*read)(dal_t *, void *, blk_t, blk_t);
	int (*write)(dal_t *, void *, blk_t, blk_t);
	int (*sync)(dal_t *);
	int (*flags)(dal_t *);
	int (*equals)(dal_t *, dal_t *);
	int (*stat)(dal_t *, struct stat *);
	blk_t (*len)(dal_t *);
};

extern void *libdal_malloc(size_t size);
extern int libdal_realloc(void **old, size_t size);
extern void libdal_free(void *ptr);

extern dal_t *dal_open(struct dal_ops *ops, const void *dev, size_t blocksize, 
	int flags, void *data);
extern void dal_close(dal_t *dal);

extern int dal_set_blocksize(dal_t *dal, size_t blocksize);
extern size_t dal_blocksize(dal_t *dal);

extern int dal_read(dal_t *dal, void *buff, blk_t block, blk_t count);
extern int dal_write(dal_t *dal, void *buff, blk_t block, blk_t count);
extern int dal_sync(dal_t *dal);
extern int dal_flags(dal_t *dal);
extern int dal_equals(dal_t *dal1, dal_t *dal2);
extern int dal_stat(dal_t *dal, struct stat *stat);
extern blk_t dal_len(dal_t *dal);

#endif

