/*
	file_dal.c -- standard file device abstraction layer
	Copyright (C) 2001, 2002 Yury Umanets <torque@ukrpost.net>, see COPYING for 
	licensing and copyright details.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#include <dal/dal.h>

static int file_dal_read(dal_t *dal, void *buff, blk_t block, blk_t count) {
	loff_t off, blocklen;
	
	if (!dal || !buff)
		return 0;
	
	off = (loff_t)block * (loff_t)dal->blocksize;
	
	if (lseek64((int)dal->dev, off, SEEK_SET) == -1)
		return 0;

	blocklen = (loff_t)count * (loff_t)dal->blocksize;
	
	if (read((int)dal->dev, buff, blocklen) <= 0)
		return 0;
	
	return 1;
}

static int file_dal_write(dal_t *dal, void *buff, blk_t block, blk_t count) {
	loff_t off, blocklen;
	
	if (!dal || !buff)
		return 0;
	
	off = (loff_t)block * (loff_t)dal->blocksize;
	
	if (lseek64((int)dal->dev, off, SEEK_SET) == -1)
		return 0;

	blocklen = (loff_t)count * (loff_t)dal->blocksize;
	
	if (write((int)dal->dev, buff, blocklen) <= 0)
		return 0;
	
	return 1;
}

static int file_dal_sync(dal_t *dal) {

	if (!dal) return 0;
	
	return !fsync((int)dal->dev);
}

static int file_dal_flags(dal_t *dal) {

	if (!dal) return 0;
		
	return dal->flags;
}

static int file_dal_equals(dal_t *dal1, dal_t *dal2) {

	if (!dal1 || !dal2)
	  return 0;
	  
	return !strcmp((char *)dal1->data, (char *)dal2->data);
}

static int file_dal_stat(dal_t *dal, struct stat *st) {
	
	if (!dal || !st)
		return 0;
	
	if (stat((char *)dal->data, st))
		return 0;

	return 1;
}

static blk_t file_dal_len(dal_t *dal) {
	loff_t max_off = 0;
	
	if (!dal) return 0;
	
	if ((max_off = lseek64((int)dal->dev, 0, SEEK_END)) == (loff_t)-1)
		return 0;
	
	return max_off / dal->blocksize;
}

static struct dal_ops ops = {
	file_dal_read, 
	file_dal_write, 
	file_dal_sync, 
	file_dal_flags, 
	file_dal_equals, 
	file_dal_stat, 
	file_dal_len
};

dal_t *file_dal_open(const char *dev, size_t blocksize, int flags) {
	int fd;
	
	if (!dev) return NULL;
	
	if ((fd = open(dev, flags | O_LARGEFILE)) == -1)
		return NULL;
	
	return dal_open(&ops, (void *)fd, blocksize, flags, (void *)dev);
}

int file_dal_reopen(dal_t *dal, int flags) {
	int fd;
	
	if (!dal) return 0;

	close((int)dal->dev);
	
	if ((fd = open((char *)dal->data, flags | O_LARGEFILE)) == -1)
		return 0;
	
	dal->dev = (void *)fd;
	dal->flags = flags;
	
	return 1;
}

void file_dal_close(dal_t *dal) {

	if (!dal) return;

	close((int)dal->dev);
	dal_close(dal);
}

