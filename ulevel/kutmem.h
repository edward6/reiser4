/* Author: Joshua MacDonald
 * Copyright (C) 2001, 2002 Hans Reiser.  All rights reserved.
 */

#ifndef __KUTMEM_H__
#define __KUTMEM_H__

extern int printf(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

extern void *calloc(long nmemb, long size);
extern void *malloc(long size);
extern void free(void *ptr);

#define __malloc_and_calloc_defined
#define KMALLOC malloc
#define KFREE   free

#ifndef NULL
#define NULL ((void*) 0)
#endif

#define min(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

#endif /* __KUTMEM_H__ */
