/*
	dal.c -- device abstraction API
	Copyright (C) 1996 - 2002 Hans Reiser.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

#include <dal/dal.h>

static int power_of_two(unsigned long value) {
	return (value & -value) == value;
}

void *libdal_malloc(size_t size) {
	void *mem;

	mem = (void *)malloc(size);
	if (!mem) {
		fprintf(stderr, "Out of memory.\n");
		return NULL;
	}

	return mem;
}

int libdal_realloc(void **old, size_t size) {
	void *mem;

	mem = (void *)realloc(*old, size);
	if (!mem) {
		fprintf(stderr, "Out of memory.\n");
		return 0;
	}
	*old = mem;
	return 1;
}

void libdal_free(void* ptr) {
	free(ptr);
}

dal_t *dal_open(struct dal_ops *ops, const void *dev, 
	size_t blocksize, int flags, void *data) 
{
	dal_t *dal;
	
	if (!ops) return NULL;
	
	if (!power_of_two(blocksize)) {
		fprintf(stderr, "Block size isn't power of two.\n");
		return NULL;
	}	
	
	if (!(dal = (dal_t *)libdal_malloc(sizeof(*dal))))
		return NULL;

	memset(dal, 0, sizeof(*dal));
	
	dal->ops = ops;
	dal->dev = dev;
	dal->data = data;
	dal->flags = flags;
	dal->blocksize = blocksize;
	
	return dal;
}

void dal_close(dal_t *dal) {
	
	if (!dal) return;
	
	dal->ops = NULL;
	dal->dev = NULL;
	dal->data = NULL;
	libdal_free(dal);
}

int dal_set_blocksize(dal_t *dal, size_t blocksize) {

	if (!dal) return 0;
	
	if (!power_of_two(blocksize))
		return 0;
	
	dal->blocksize = blocksize;
	
	return 1;
}

size_t dal_blocksize(dal_t *dal) {

	if (!dal) return 0;

	return dal->blocksize;
}

int dal_read(dal_t *dal, void *buff, blk_t block, blk_t count) {

	if (!dal) return 0;

	if (dal->ops->read)
		return dal->ops->read(dal, buff, block, count);
	
	return 0;
}

int dal_write(dal_t *dal, void *buff, blk_t block, blk_t count) {

	if (!dal) return 0;
	
	if (dal->ops->write)
		return dal->ops->write(dal, buff, block, count);
		
	return 0;
}
	
int dal_sync(dal_t *dal) {

	if (!dal) return 0;

	if (dal->ops->sync)
		return dal->ops->sync(dal);
	
	return 0;	
}

int dal_flags(dal_t *dal) {

	if (!dal) return 0;

	if (dal->ops->flags)
		return dal->ops->flags(dal);
	
	return 0;
}

int dal_equals(dal_t *dal1, dal_t *dal2) {
	
	if (!dal1 || !dal2) return 0;

	if (dal1->ops->equals)
		return dal1->ops->equals(dal1, dal2);
	
	return 0;	
}

int dal_stat(dal_t *dal, struct stat* stat) {

	if (!dal) return 0;
	
	if (dal->ops->stat)
		return dal->ops->stat(dal, stat);
	
	return 0;
}

blk_t dal_len(dal_t *dal) {
	
	if (!dal) 
		return 0;

	if (dal->ops->len)
		return dal->ops->len(dal);

	return 0;
}

